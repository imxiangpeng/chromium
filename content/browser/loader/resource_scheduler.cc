// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/resource_scheduler.h"

#include <stdint.h>

#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/optional.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/supports_user_data.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "content/common/resource_messages.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/browser/resource_throttle.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_flags.h"
#include "net/base/request_priority.h"
#include "net/http/http_server_properties.h"
#include "net/nqe/network_quality_estimator.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "url/scheme_host_port.h"

namespace content {

namespace {

// When kPrioritySupportedRequestsDelayable is enabled, requests for
// H2/QUIC/SPDY resources can be delayed by the ResourceScheduler just as
// HTTP/1.1 resources are. Disabling this appears to have negative performance
// impact, see https://crbug.com/655585.
const base::Feature kPrioritySupportedRequestsDelayable{
    "PrioritySupportedRequestsDelayable", base::FEATURE_ENABLED_BY_DEFAULT};

// In the event that many resource requests are started quickly, this feature
// will periodically yield (e.g., delaying starting of requests) by posting a
// task and waiting for the task to run to resume. This allows other
// operations that rely on the IO thread (e.g., already running network
// requests) to make progress.
const base::Feature kNetworkSchedulerYielding{
    "NetworkSchedulerYielding", base::FEATURE_DISABLED_BY_DEFAULT};
const char kMaxRequestsBeforeYieldingParam[] = "MaxRequestsBeforeYieldingParam";
const int kMaxRequestsBeforeYieldingDefault = 5;

// When the effective connection type is detected to be lower than or equal to
// the parameter provided in the experiment configuration and greater than
// |EFFECTIVE_CONNECTION_TYPE_OFFLINE|, this feature will override the value of
// the maximum number of delayable requests allowed in flight. The number of
// delayable requests allowed in flight will be based on the BDP ranges and the
// corresponding number of delayable requests in flight specified in the
// experiment configuration.
const base::Feature kMaxDelayableRequestsNetworkOverride{
    "MaxDelayableRequestsNetworkOverride", base::FEATURE_DISABLED_BY_DEFAULT};

enum StartMode {
  START_SYNC,
  START_ASYNC
};

// Flags identifying various attributes of the request that are used
// when making scheduling decisions.
using RequestAttributes = uint8_t;
const RequestAttributes kAttributeNone = 0x00;
const RequestAttributes kAttributeInFlight = 0x01;
const RequestAttributes kAttributeDelayable = 0x02;
const RequestAttributes kAttributeLayoutBlocking = 0x04;

// Reasons why pending requests may be started.  For logging only.
enum class RequestStartTrigger {
  NONE,
  COMPLETION_PRE_BODY,
  COMPLETION_POST_BODY,
  BODY_REACHED,
  CLIENT_KILL,
  SPDY_PROXY_DETECTED,
  REQUEST_REPRIORITIZED,
  START_WAS_YIELDED,
};

const char* RequestStartTriggerString(RequestStartTrigger trigger) {
  switch (trigger) {
    case RequestStartTrigger::NONE:
      return "NONE";
    case RequestStartTrigger::COMPLETION_PRE_BODY:
      return "COMPLETION_PRE_BODY";
    case RequestStartTrigger::COMPLETION_POST_BODY:
      return "COMPLETION_POST_BODY";
    case RequestStartTrigger::BODY_REACHED:
      return "BODY_REACHED";
    case RequestStartTrigger::CLIENT_KILL:
      return "CLIENT_KILL";
    case RequestStartTrigger::SPDY_PROXY_DETECTED:
      return "SPDY_PROXY_DETECTED";
    case RequestStartTrigger::REQUEST_REPRIORITIZED:
      return "REQUEST_REPRIORITIZED";
    case RequestStartTrigger::START_WAS_YIELDED:
      return "START_WAS_YIELDED";
  }
  NOTREACHED();
  return "Unknown";
}

}  // namespace

// The maximum number of delayable requests to allow to be in-flight at any
// point in time (across all hosts).
static const size_t kDefaultMaxNumDelayableRequestsPerClient = 10;

// The maximum number of requests to allow be in-flight at any point in time per
// host.
static const size_t kMaxNumDelayableRequestsPerHostPerClient = 6;

// The maximum number of delayable requests to allow to be in-flight at any
// point in time while in the layout-blocking phase of loading.
static const size_t kMaxNumDelayableWhileLayoutBlockingPerClient = 1;

// The priority level above which resources are considered layout-blocking if
// the html_body has not started.
static const net::RequestPriority
    kLayoutBlockingPriorityThreshold = net::MEDIUM;

// The priority level below which resources are considered to be delayable.
static const net::RequestPriority
    kDelayablePriorityThreshold = net::MEDIUM;

// The number of in-flight layout-blocking requests above which all delayable
// requests should be blocked.
static const size_t kInFlightNonDelayableRequestCountPerClientThreshold = 1;

struct ResourceScheduler::RequestPriorityParams {
  RequestPriorityParams()
    : priority(net::DEFAULT_PRIORITY),
      intra_priority(0) {
  }

  RequestPriorityParams(net::RequestPriority priority, int intra_priority)
    : priority(priority),
      intra_priority(intra_priority) {
  }

  bool operator==(const RequestPriorityParams& other) const {
    return (priority == other.priority) &&
        (intra_priority == other.intra_priority);
  }

  bool operator!=(const RequestPriorityParams& other) const {
    return !(*this == other);
  }

  bool GreaterThan(const RequestPriorityParams& other) const {
    if (priority != other.priority)
      return priority > other.priority;
    return intra_priority > other.intra_priority;
  }

  net::RequestPriority priority;
  int intra_priority;
};

class ResourceScheduler::RequestQueue {
 public:
  typedef std::multiset<ScheduledResourceRequest*, ScheduledResourceSorter>
      NetQueue;

  RequestQueue() : fifo_ordering_ids_(0) {}
  ~RequestQueue() {}

  // Adds |request| to the queue with given |priority|.
  void Insert(ScheduledResourceRequest* request);

  // Removes |request| from the queue.
  void Erase(ScheduledResourceRequest* request) {
    PointerMap::iterator it = pointers_.find(request);
    CHECK(it != pointers_.end());
    queue_.erase(it->second);
    pointers_.erase(it);
  }

  NetQueue::iterator GetNextHighestIterator() {
    return queue_.begin();
  }

  NetQueue::iterator End() {
    return queue_.end();
  }

  // Returns true if |request| is queued.
  bool IsQueued(ScheduledResourceRequest* request) const {
    return base::ContainsKey(pointers_, request);
  }

  // Returns true if no requests are queued.
  bool IsEmpty() const { return queue_.size() == 0; }

 private:
  typedef std::map<ScheduledResourceRequest*, NetQueue::iterator> PointerMap;

  uint32_t MakeFifoOrderingId() {
    fifo_ordering_ids_ += 1;
    return fifo_ordering_ids_;
  }

  // Used to create an ordering ID for scheduled resources so that resources
  // with same priority/intra_priority stay in fifo order.
  uint32_t fifo_ordering_ids_;

  NetQueue queue_;
  PointerMap pointers_;
};

// This is the handle we return to the ResourceDispatcherHostImpl so it can
// interact with the request.
class ResourceScheduler::ScheduledResourceRequest : public ResourceThrottle {
 public:
  ScheduledResourceRequest(const ClientId& client_id,
                           net::URLRequest* request,
                           ResourceScheduler* scheduler,
                           const RequestPriorityParams& priority,
                           bool is_async)
      : client_id_(client_id),
        request_(request),
        ready_(false),
        deferred_(false),
        is_async_(is_async),
        attributes_(kAttributeNone),
        scheduler_(scheduler),
        priority_(priority),
        fifo_ordering_(0),
        peak_delayable_requests_in_flight_(0u),
        host_port_pair_(net::HostPortPair::FromURL(request->url())),
        weak_ptr_factory_(this) {
    DCHECK(!request_->GetUserData(kUserDataKey));
    request_->SetUserData(kUserDataKey, base::MakeUnique<UnownedPointer>(this));
  }

  ~ScheduledResourceRequest() override {
    if ((attributes_ & kAttributeLayoutBlocking) == kAttributeLayoutBlocking) {
      UMA_HISTOGRAM_COUNTS_100(
          "ResourceScheduler.PeakDelayableRequestsInFlight.LayoutBlocking",
          peak_delayable_requests_in_flight_);
    }
    if (!((attributes_ & kAttributeDelayable) == kAttributeDelayable)) {
      UMA_HISTOGRAM_COUNTS_100(
          "ResourceScheduler.PeakDelayableRequestsInFlight.NonDelayable",
          peak_delayable_requests_in_flight_);
    }
    request_->RemoveUserData(kUserDataKey);
    scheduler_->RemoveRequest(this);
  }

  static ScheduledResourceRequest* ForRequest(net::URLRequest* request) {
    UnownedPointer* pointer =
        static_cast<UnownedPointer*>(request->GetUserData(kUserDataKey));
    return pointer ? pointer->get() : nullptr;
  }

  // Starts the request. If |start_mode| is START_ASYNC, the request will not
  // be started immediately.
  void Start(StartMode start_mode) {
    DCHECK(!ready_);

    // If the request was cancelled, do nothing.
    if (!request_->status().is_success())
      return;

    // If the request was deferred, need to start it.  Otherwise, will just not
    // defer starting it in the first place, and the value of |start_mode|
    // makes no difference.
    if (deferred_) {
      // If can't start the request synchronously, post a task to start the
      // request.
      if (start_mode == START_ASYNC) {
        base::ThreadTaskRunnerHandle::Get()->PostTask(
            FROM_HERE,
            base::Bind(&ScheduledResourceRequest::Start,
                       weak_ptr_factory_.GetWeakPtr(),
                       START_SYNC));
        return;
      }
      deferred_ = false;
      Resume();
    }

    ready_ = true;
  }

  void UpdateDelayableRequestsInFlight(size_t delayable_requests_in_flight) {
    peak_delayable_requests_in_flight_ = std::max(
        peak_delayable_requests_in_flight_, delayable_requests_in_flight);
  }

  void set_request_priority_params(const RequestPriorityParams& priority) {
    priority_ = priority;
  }
  const RequestPriorityParams& get_request_priority_params() const {
    return priority_;
  }
  const ClientId& client_id() const { return client_id_; }
  net::URLRequest* url_request() { return request_; }
  const net::URLRequest* url_request() const { return request_; }
  bool is_async() const { return is_async_; }
  uint32_t fifo_ordering() const { return fifo_ordering_; }
  void set_fifo_ordering(uint32_t fifo_ordering) {
    fifo_ordering_ = fifo_ordering;
  }
  RequestAttributes attributes() const {
    return attributes_;
  }
  void set_attributes(RequestAttributes attributes) {
    attributes_ = attributes;
  }
  const net::HostPortPair& host_port_pair() const { return host_port_pair_; }

 private:
  class UnownedPointer : public base::SupportsUserData::Data {
   public:
    explicit UnownedPointer(ScheduledResourceRequest* pointer)
        : pointer_(pointer) {}

    ScheduledResourceRequest* get() const { return pointer_; }

   private:
    ScheduledResourceRequest* const pointer_;

    DISALLOW_COPY_AND_ASSIGN(UnownedPointer);
  };

  static const void* const kUserDataKey;

  // ResourceThrottle interface:
  void WillStartRequest(bool* defer) override {
    deferred_ = *defer = !ready_;
  }

  const char* GetNameForLogging() const override { return "ResourceScheduler"; }

  const ClientId client_id_;
  net::URLRequest* request_;
  bool ready_;
  bool deferred_;
  bool is_async_;
  RequestAttributes attributes_;
  ResourceScheduler* scheduler_;
  RequestPriorityParams priority_;
  uint32_t fifo_ordering_;

  // Maximum number of delayable requests in-flight when |this| was in-flight.
  size_t peak_delayable_requests_in_flight_;
  // Cached to excessive recomputation in ShouldKeepSearching.
  const net::HostPortPair host_port_pair_;

  base::WeakPtrFactory<ResourceScheduler::ScheduledResourceRequest>
      weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ScheduledResourceRequest);
};

const void* const ResourceScheduler::ScheduledResourceRequest::kUserDataKey =
    &ResourceScheduler::ScheduledResourceRequest::kUserDataKey;

bool ResourceScheduler::ScheduledResourceSorter::operator()(
    const ScheduledResourceRequest* a,
    const ScheduledResourceRequest* b) const {
  // Want the set to be ordered first by decreasing priority, then by
  // decreasing intra_priority.
  // ie. with (priority, intra_priority)
  // [(1, 0), (1, 0), (0, 100), (0, 0)]
  if (a->get_request_priority_params() != b->get_request_priority_params())
    return a->get_request_priority_params().GreaterThan(
        b->get_request_priority_params());

  // If priority/intra_priority is the same, fall back to fifo ordering.
  // std::multiset doesn't guarantee this until c++11.
  return a->fifo_ordering() < b->fifo_ordering();
}

void ResourceScheduler::RequestQueue::Insert(
    ScheduledResourceRequest* request) {
  DCHECK(!base::ContainsKey(pointers_, request));
  request->set_fifo_ordering(MakeFifoOrderingId());
  pointers_[request] = queue_.insert(request);
}

// Each client represents a tab.
class ResourceScheduler::Client {
 public:
  Client(bool priority_requests_delayable,
         bool yielding_scheduler_enabled,
         int max_requests_before_yielding,
         const net::NetworkQualityEstimator* const network_quality_estimator,
         ResourceScheduler* resource_scheduler)
      : is_loaded_(false),
        has_html_body_(false),
        using_spdy_proxy_(false),
        in_flight_delayable_count_(0),
        total_layout_blocking_count_(0),
        priority_requests_delayable_(priority_requests_delayable),
        num_skipped_scans_due_to_scheduled_start_(0),
        started_requests_since_yielding_(0),
        did_scheduler_yield_(false),
        yielding_scheduler_enabled_(yielding_scheduler_enabled),
        max_requests_before_yielding_(max_requests_before_yielding),
        network_quality_estimator_(network_quality_estimator),
        max_delayable_requests_(
            resource_scheduler->ComputeMaxDelayableRequestsNetworkOverride(
                network_quality_estimator)),
        resource_scheduler_(resource_scheduler),
        weak_ptr_factory_(this) {}

  ~Client() {}

  void ScheduleRequest(const net::URLRequest& url_request,
                       ScheduledResourceRequest* request) {
    SetRequestAttributes(request, DetermineRequestAttributes(request));
    ShouldStartReqResult should_start = ShouldStartRequest(request);
    if (should_start == START_REQUEST) {
      // New requests can be started synchronously without issue.
      StartRequest(request, START_SYNC, RequestStartTrigger::NONE);
    } else {
      pending_requests_.Insert(request);
      if (should_start == YIELD_SCHEDULER)
        did_scheduler_yield_ = true;
    }
  }

  void RemoveRequest(ScheduledResourceRequest* request) {
    if (pending_requests_.IsQueued(request)) {
      pending_requests_.Erase(request);
      DCHECK(!base::ContainsKey(in_flight_requests_, request));
    } else {
      EraseInFlightRequest(request);

      // Removing this request may have freed up another to load.
      LoadAnyStartablePendingRequests(
          has_html_body_ ? RequestStartTrigger::COMPLETION_POST_BODY
                         : RequestStartTrigger::COMPLETION_PRE_BODY);
    }
  }

  RequestSet StartAndRemoveAllRequests() {
    // First start any pending requests so that they will be moved into
    // in_flight_requests_. This may exceed the limits
    // kDefaultMaxNumDelayableRequestsPerClient and
    // kMaxNumDelayableRequestsPerHostPerClient, so this method must not do
    // anything that depends on those limits before calling
    // ClearInFlightRequests() below.
    while (!pending_requests_.IsEmpty()) {
      ScheduledResourceRequest* request =
          *pending_requests_.GetNextHighestIterator();
      pending_requests_.Erase(request);
      // Starting requests asynchronously ensures no side effects, and avoids
      // starting a bunch of requests that may be about to be deleted.
      StartRequest(request, START_ASYNC, RequestStartTrigger::CLIENT_KILL);
    }
    RequestSet unowned_requests;
    for (RequestSet::iterator it = in_flight_requests_.begin();
         it != in_flight_requests_.end(); ++it) {
      unowned_requests.insert(*it);
      (*it)->set_attributes(kAttributeNone);
    }
    ClearInFlightRequests();
    return unowned_requests;
  }

  bool is_loaded() const { return is_loaded_; }

  void OnLoadingStateChanged(bool is_loaded) {
    is_loaded_ = is_loaded;
  }

  void OnNavigate() {
    has_html_body_ = false;
    is_loaded_ = false;
    max_delayable_requests_ =
        resource_scheduler_->ComputeMaxDelayableRequestsNetworkOverride(
            network_quality_estimator_);
  }

  void OnWillInsertBody() {
    // Can be called multiple times per RVH in the case of out-of-process
    // iframes.
    if (has_html_body_)
      return;
    has_html_body_ = true;
    LoadAnyStartablePendingRequests(RequestStartTrigger::BODY_REACHED);
  }

  void OnReceivedSpdyProxiedHttpResponse() {
    if (!using_spdy_proxy_) {
      using_spdy_proxy_ = true;
      LoadAnyStartablePendingRequests(RequestStartTrigger::SPDY_PROXY_DETECTED);
    }
  }

  void ReprioritizeRequest(ScheduledResourceRequest* request,
                           RequestPriorityParams old_priority_params,
                           RequestPriorityParams new_priority_params) {
    request->url_request()->SetPriority(new_priority_params.priority);
    request->set_request_priority_params(new_priority_params);
    SetRequestAttributes(request, DetermineRequestAttributes(request));
    if (!pending_requests_.IsQueued(request)) {
      DCHECK(base::ContainsKey(in_flight_requests_, request));
      // Request has already started.
      return;
    }

    pending_requests_.Erase(request);
    pending_requests_.Insert(request);

    if (new_priority_params.priority > old_priority_params.priority) {
      // Check if this request is now able to load at its new priority.
      ScheduleLoadAnyStartablePendingRequests(
          RequestStartTrigger::REQUEST_REPRIORITIZED);
    }
  }

 private:
  enum ShouldStartReqResult {
    DO_NOT_START_REQUEST_AND_STOP_SEARCHING,
    DO_NOT_START_REQUEST_AND_KEEP_SEARCHING,
    START_REQUEST,
    YIELD_SCHEDULER
  };

  // Records the metrics related to number of requests in flight.
  void RecordRequestCountMetrics() const {
    UMA_HISTOGRAM_COUNTS_100("ResourceScheduler.RequestsCount.All",
                             in_flight_requests_.size());
    UMA_HISTOGRAM_COUNTS_100("ResourceScheduler.RequestsCount.Delayable",
                             in_flight_delayable_count_);
    UMA_HISTOGRAM_COUNTS_100(
        "ResourceScheduler.RequestsCount.NonDelayable",
        in_flight_requests_.size() - in_flight_delayable_count_);
    UMA_HISTOGRAM_COUNTS_100(
        "ResourceScheduler.RequestsCount.TotalLayoutBlocking",
        total_layout_blocking_count_);
  }

  void InsertInFlightRequest(ScheduledResourceRequest* request) {
    in_flight_requests_.insert(request);
    SetRequestAttributes(request, DetermineRequestAttributes(request));
    RecordRequestCountMetrics();

    if (RequestAttributesAreSet(request->attributes(), kAttributeDelayable)) {
      // Notify all in-flight with the new count of in-flight delayable
      // requests.
      for (RequestSet::const_iterator it = in_flight_requests_.begin();
           it != in_flight_requests_.end(); ++it) {
        (*it)->UpdateDelayableRequestsInFlight(in_flight_delayable_count_);
      }
    }

    if (RequestAttributesAreSet(request->attributes(),
                                kAttributeLayoutBlocking) ||
        !RequestAttributesAreSet(request->attributes(), kAttributeDelayable)) {
      // |request| is either a layout blocking or a non-delayable request.
      request->UpdateDelayableRequestsInFlight(in_flight_delayable_count_);
    }
  }

  void EraseInFlightRequest(ScheduledResourceRequest* request) {
    size_t erased = in_flight_requests_.erase(request);
    DCHECK_EQ(1u, erased);
    // Clear any special state that we were tracking for this request.
    SetRequestAttributes(request, kAttributeNone);
  }

  void ClearInFlightRequests() {
    in_flight_requests_.clear();
    in_flight_delayable_count_ = 0;
    total_layout_blocking_count_ = 0;
  }

  size_t CountRequestsWithAttributes(
      const RequestAttributes attributes,
      ScheduledResourceRequest* current_request) {
    size_t matching_request_count = 0;
    for (RequestSet::const_iterator it = in_flight_requests_.begin();
         it != in_flight_requests_.end(); ++it) {
      if (RequestAttributesAreSet((*it)->attributes(), attributes))
        matching_request_count++;
    }
    if (!RequestAttributesAreSet(attributes, kAttributeInFlight)) {
      bool current_request_is_pending = false;
      for (RequestQueue::NetQueue::const_iterator
           it = pending_requests_.GetNextHighestIterator();
           it != pending_requests_.End(); ++it) {
        if (RequestAttributesAreSet((*it)->attributes(), attributes))
          matching_request_count++;
        if (*it == current_request)
          current_request_is_pending = true;
      }
      // Account for the current request if it is not in one of the lists yet.
      if (current_request &&
          !base::ContainsKey(in_flight_requests_, current_request) &&
          !current_request_is_pending) {
        if (RequestAttributesAreSet(current_request->attributes(), attributes))
          matching_request_count++;
      }
    }
    return matching_request_count;
  }

  bool RequestAttributesAreSet(RequestAttributes request_attributes,
                               RequestAttributes matching_attributes) const {
    return (request_attributes & matching_attributes) == matching_attributes;
  }

  void SetRequestAttributes(ScheduledResourceRequest* request,
                            RequestAttributes attributes) {
    RequestAttributes old_attributes = request->attributes();
    if (old_attributes == attributes)
      return;

    if (RequestAttributesAreSet(old_attributes,
                                kAttributeInFlight | kAttributeDelayable)) {
      in_flight_delayable_count_--;
    }
    if (RequestAttributesAreSet(old_attributes, kAttributeLayoutBlocking))
      total_layout_blocking_count_--;

    if (RequestAttributesAreSet(attributes,
                                kAttributeInFlight | kAttributeDelayable)) {
      in_flight_delayable_count_++;
    }
    if (RequestAttributesAreSet(attributes, kAttributeLayoutBlocking))
      total_layout_blocking_count_++;

    request->set_attributes(attributes);
    DCHECK_EQ(CountRequestsWithAttributes(
        kAttributeInFlight | kAttributeDelayable, request),
        in_flight_delayable_count_);
    DCHECK_EQ(CountRequestsWithAttributes(kAttributeLayoutBlocking, request),
              total_layout_blocking_count_);
  }

  RequestAttributes DetermineRequestAttributes(
      ScheduledResourceRequest* request) {
    RequestAttributes attributes = kAttributeNone;

    if (base::ContainsKey(in_flight_requests_, request))
      attributes |= kAttributeInFlight;

    if (RequestAttributesAreSet(request->attributes(),
                                kAttributeLayoutBlocking)) {
      // If a request is already marked as layout-blocking make sure to keep the
      // attribute across redirects.
      attributes |= kAttributeLayoutBlocking;
    } else if (!has_html_body_ &&
               request->url_request()->priority() >
               kLayoutBlockingPriorityThreshold) {
      // Requests that are above the non_delayable threshold before the HTML
      // body has been parsed are inferred to be layout-blocking.
      attributes |= kAttributeLayoutBlocking;
    } else if (request->url_request()->priority() <
               kDelayablePriorityThreshold) {
      if (priority_requests_delayable_) {
        // Resources below the delayable priority threshold that are considered
        // delayable.
        attributes |= kAttributeDelayable;
      } else {
        // Resources below the delayable priority threshold that are being
        // requested from a server that does not support native prioritization
        // are considered delayable.
        url::SchemeHostPort scheme_host_port(request->url_request()->url());
        net::HttpServerProperties& http_server_properties =
            *request->url_request()->context()->http_server_properties();
        if (!http_server_properties.SupportsRequestPriority(scheme_host_port))
          attributes |= kAttributeDelayable;
      }
    }

    return attributes;
  }

  bool ShouldKeepSearching(
      const net::HostPortPair& active_request_host) const {
    size_t same_host_count = 0;
    for (RequestSet::const_iterator it = in_flight_requests_.begin();
         it != in_flight_requests_.end(); ++it) {
      if (active_request_host.Equals((*it)->host_port_pair())) {
        same_host_count++;
        if (same_host_count >= kMaxNumDelayableRequestsPerHostPerClient)
          return true;
      }
    }
    return false;
  }

  void StartRequest(ScheduledResourceRequest* request,
                    StartMode start_mode,
                    RequestStartTrigger trigger) {
    started_requests_since_yielding_ += 1;
    if (started_requests_since_yielding_ == 1) {
      // This is the first started request since last yielding. Post a task to
      // reset the counter and start any yielded tasks if necessary. We post
      // this now instead of when we first yield so that if there is a pause
      // between requests the counter is reset.
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&Client::ResumeIfYielded, weak_ptr_factory_.GetWeakPtr()));
    }

    // Only log on requests that were blocked by the ResourceScheduler.
    if (start_mode == START_ASYNC) {
      DCHECK_NE(RequestStartTrigger::NONE, trigger);
      request->url_request()->net_log().AddEvent(
          net::NetLogEventType::RESOURCE_SCHEDULER_REQUEST_STARTED,
          net::NetLog::StringCallback(
              "trigger", RequestStartTriggerString(trigger)));
    }
    InsertInFlightRequest(request);
    request->Start(start_mode);
  }

  // ShouldStartRequest is the main scheduling algorithm.
  //
  // Requests are evaluated on five attributes:
  //
  // 1. Non-delayable requests:
  //   * Synchronous requests.
  //   * Non-HTTP[S] requests.
  //
  // 2. Requests to request-priority-capable origin servers.
  //
  // 3. High-priority requests:
  //   * Higher priority requests (>= net::LOW).
  //
  // 4. Layout-blocking requests:
  //   * High-priority requests (> net::LOW) initiated before the renderer has
  //     a <body>.
  //
  // 5. Low priority requests
  //
  //  The following rules are followed:
  //
  //  All types of requests:
  //   * Non-delayable, High-priority and request-priority capable requests are
  //     issued immediately.
  //   * Low priority requests are delayable.
  //   * While kInFlightNonDelayableRequestCountPerClientThreshold
  //     layout-blocking requests are loading or the body tag has not yet been
  //     parsed, limit the number of delayable requests that may be in flight
  //     to kMaxNumDelayableWhileLayoutBlockingPerClient.
  //   * If no high priority or layout-blocking requests are in flight, start
  //     loading delayable requests.
  //   * Never exceed 10 delayable requests in flight per client.
  //   * Never exceed 6 delayable requests for a given host.

  ShouldStartReqResult ShouldStartRequest(
      ScheduledResourceRequest* request) const {
    const net::URLRequest& url_request = *request->url_request();
    // Syncronous requests could block the entire render, which could impact
    // user-observable Clients.
    if (!request->is_async())
      return START_REQUEST;

    // TODO(simonjam): This may end up causing disk contention. We should
    // experiment with throttling if that happens.
    if (!url_request.url().SchemeIsHTTPOrHTTPS())
      return START_REQUEST;

    const net::HostPortPair& host_port_pair = request->host_port_pair();

    if (!priority_requests_delayable_) {
      if (using_spdy_proxy_ && url_request.url().SchemeIs(url::kHttpScheme))
        return ShouldStartOrYieldRequest();

      url::SchemeHostPort scheme_host_port(url_request.url());

      net::HttpServerProperties& http_server_properties =
          *url_request.context()->http_server_properties();

      // TODO(willchan): We should really improve this algorithm as described in
      // crbug.com/164101. Also, theoretically we should not count a
      // request-priority capable request against the delayable requests limit.
      if (http_server_properties.SupportsRequestPriority(scheme_host_port))
        return ShouldStartOrYieldRequest();
    }

    // Non-delayable requests.
    if (!RequestAttributesAreSet(request->attributes(), kAttributeDelayable))
      return ShouldStartOrYieldRequest();

    // Delayable requests.
    if (in_flight_delayable_count_ >= max_delayable_requests_)
      return DO_NOT_START_REQUEST_AND_STOP_SEARCHING;

    if (ShouldKeepSearching(host_port_pair)) {
      // There may be other requests for other hosts that may be allowed,
      // so keep checking.
      return DO_NOT_START_REQUEST_AND_KEEP_SEARCHING;
    }

    // The in-flight requests consist of layout-blocking requests,
    // normal requests and delayable requests.  Everything except for
    // delayable requests is handled above here so this is deciding what to
    // do with a delayable request while we are in the layout-blocking phase
    // of loading.
    if (!has_html_body_ || total_layout_blocking_count_ != 0) {
      size_t non_delayable_requests_in_flight_count =
          in_flight_requests_.size() - in_flight_delayable_count_;
      if (non_delayable_requests_in_flight_count >
          kInFlightNonDelayableRequestCountPerClientThreshold) {
        // Too many higher priority in-flight requests to allow lower priority
        // requests through.
        return DO_NOT_START_REQUEST_AND_STOP_SEARCHING;
      }
      if (in_flight_requests_.size() > 0 &&
          (in_flight_delayable_count_ >=
           kMaxNumDelayableWhileLayoutBlockingPerClient)) {
        // Block the request if at least one request is in flight and the
        // number of in-flight delayable requests has hit the configured
        // limit.
        return DO_NOT_START_REQUEST_AND_STOP_SEARCHING;
      }
    }

    return ShouldStartOrYieldRequest();
  }

  // It is common for a burst of messages to come from the renderer which
  // trigger starting pending requests. Naively, this would result in O(n*m)
  // behavior for n pending requests and m <= n messages, as
  // LoadAnyStartablePendingRequest is O(n) for n pending requests. To solve
  // this, just post a task to the end of the queue to call the method,
  // coalescing the m messages into a single call to
  // LoadAnyStartablePendingRequests.
  // TODO(csharrison): Reconsider this if IPC batching becomes an easy to use
  // pattern.
  void ScheduleLoadAnyStartablePendingRequests(RequestStartTrigger trigger) {
    if (num_skipped_scans_due_to_scheduled_start_ == 0) {
      TRACE_EVENT0("loading", "ScheduleLoadAnyStartablePendingRequests");
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&Client::LoadAnyStartablePendingRequests,
                     weak_ptr_factory_.GetWeakPtr(), trigger));
    }
    num_skipped_scans_due_to_scheduled_start_ += 1;
  }

  void ResumeIfYielded() {
    bool yielded = did_scheduler_yield_;
    started_requests_since_yielding_ = 0;
    did_scheduler_yield_ = false;

    if (yielded)
      LoadAnyStartablePendingRequests(RequestStartTrigger::START_WAS_YIELDED);
  }

  // For a request that is ready to start, return START_REQUEST if the
  // scheduler doesn't need to yield, else YIELD_SCHEDULER.
  ShouldStartReqResult ShouldStartOrYieldRequest() const {
    DCHECK_GE(started_requests_since_yielding_, 0);

    if (!yielding_scheduler_enabled_ ||
        started_requests_since_yielding_ < max_requests_before_yielding_) {
      return START_REQUEST;
    }
    return YIELD_SCHEDULER;
  }

  void LoadAnyStartablePendingRequests(RequestStartTrigger trigger) {
    // We iterate through all the pending requests, starting with the highest
    // priority one. For each entry, one of three things can happen:
    // 1) We start the request, remove it from the list, and keep checking.
    // 2) We do NOT start the request, but ShouldStartRequest() signals us that
    //     there may be room for other requests, so we keep checking and leave
    //     the previous request still in the list.
    // 3) We do not start the request, same as above, but StartRequest() tells
    //     us there's no point in checking any further requests.
    TRACE_EVENT0("loading", "LoadAnyStartablePendingRequests");
    if (num_skipped_scans_due_to_scheduled_start_ > 0) {
      UMA_HISTOGRAM_COUNTS_1M("ResourceScheduler.NumSkippedScans.ScheduleStart",
                              num_skipped_scans_due_to_scheduled_start_);
    }
    num_skipped_scans_due_to_scheduled_start_ = 0;
    RequestQueue::NetQueue::iterator request_iter =
        pending_requests_.GetNextHighestIterator();

    while (request_iter != pending_requests_.End()) {
      ScheduledResourceRequest* request = *request_iter;
      ShouldStartReqResult query_result = ShouldStartRequest(request);

      if (query_result == START_REQUEST) {
        pending_requests_.Erase(request);
        StartRequest(request, START_ASYNC, trigger);

        // StartRequest can modify the pending list, so we (re)start evaluation
        // from the currently highest priority request. Avoid copying a singular
        // iterator, which would trigger undefined behavior.
        if (pending_requests_.GetNextHighestIterator() ==
            pending_requests_.End())
          break;
        request_iter = pending_requests_.GetNextHighestIterator();
      } else if (query_result == DO_NOT_START_REQUEST_AND_KEEP_SEARCHING) {
        ++request_iter;
        continue;
      } else if (query_result == YIELD_SCHEDULER) {
        did_scheduler_yield_ = true;
        break;
      } else {
        DCHECK(query_result == DO_NOT_START_REQUEST_AND_STOP_SEARCHING);
        break;
      }
    }
  }

  bool is_loaded_;
  // Tracks if the main HTML parser has reached the body which marks the end of
  // layout-blocking resources.
  bool has_html_body_;
  bool using_spdy_proxy_;
  RequestQueue pending_requests_;
  RequestSet in_flight_requests_;
  // The number of delayable in-flight requests.
  size_t in_flight_delayable_count_;
  // The number of layout-blocking in-flight requests.
  size_t total_layout_blocking_count_;

  // True if requests to servers that support priorities (e.g., H2/QUIC) can
  // be delayed.
  bool priority_requests_delayable_;

  // The number of LoadAnyStartablePendingRequests scans that were skipped due
  // to smarter task scheduling around reprioritization.
  int num_skipped_scans_due_to_scheduled_start_;

  // The number of started requests since the last ResumeIfYielded task was
  // run.
  int started_requests_since_yielding_;

  // If the scheduler had to yield the start of a request since the last
  // ResumeIfYielded task was run.
  bool did_scheduler_yield_;

  // Whether or not to periodically yield when starting lots of requests.
  bool yielding_scheduler_enabled_;

  // The number of requests that can start before yielding.
  int max_requests_before_yielding_;

  // Network quality estimator for network aware resource scheudling. This may
  // be null.
  const net::NetworkQualityEstimator* const network_quality_estimator_;

  // The value of the maximum number of delayable requests in flight. This gets
  // recalculated every time an |OnNavigate| event is triggered.
  size_t max_delayable_requests_;

  // A pointer to the resource scheduler which contains the resource scheduling
  // configuration.
  ResourceScheduler* resource_scheduler_;

  base::WeakPtrFactory<ResourceScheduler::Client> weak_ptr_factory_;
};

ResourceScheduler::ResourceScheduler()
    : priority_requests_delayable_(
          base::FeatureList::IsEnabled(kPrioritySupportedRequestsDelayable)),
      yielding_scheduler_enabled_(
          base::FeatureList::IsEnabled(kNetworkSchedulerYielding)),
      max_requests_before_yielding_(base::GetFieldTrialParamByFeatureAsInt(
          kNetworkSchedulerYielding,
          kMaxRequestsBeforeYieldingParam,
          kMaxRequestsBeforeYieldingDefault)),
      max_requests_for_bdp_ranges_(GetMaxDelayableRequestsExperimentConfig()),
      max_delayable_requests_threshold_(
          GetMaxDelayableRequestsExperimentMaxECT()) {}

ResourceScheduler::~ResourceScheduler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(unowned_requests_.empty());
  DCHECK(client_map_.empty());
}

std::unique_ptr<ResourceThrottle> ResourceScheduler::ScheduleRequest(
    int child_id,
    int route_id,
    bool is_async,
    net::URLRequest* url_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);
  std::unique_ptr<ScheduledResourceRequest> request(
      new ScheduledResourceRequest(
          client_id, url_request, this,
          RequestPriorityParams(url_request->priority(), 0), is_async));

  ClientMap::iterator it = client_map_.find(client_id);
  if (it == client_map_.end()) {
    // There are several ways this could happen:
    // 1. <a ping> requests don't have a route_id.
    // 2. Most unittests don't send the IPCs needed to register Clients.
    // 3. The tab is closed while a RequestResource IPC is in flight.
    unowned_requests_.insert(request.get());
    request->Start(START_SYNC);
    return std::move(request);
  }

  Client* client = it->second;
  client->ScheduleRequest(*url_request, request.get());
  return std::move(request);
}

void ResourceScheduler::RemoveRequest(ScheduledResourceRequest* request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (base::ContainsKey(unowned_requests_, request)) {
    unowned_requests_.erase(request);
    return;
  }

  ClientMap::iterator client_it = client_map_.find(request->client_id());
  if (client_it == client_map_.end()) {
    return;
  }

  Client* client = client_it->second;
  client->RemoveRequest(request);
}

void ResourceScheduler::OnClientCreated(
    int child_id,
    int route_id,
    const net::NetworkQualityEstimator* const network_quality_estimator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);
  DCHECK(!base::ContainsKey(client_map_, client_id));

  Client* client = new Client(
      priority_requests_delayable_, yielding_scheduler_enabled_,
      max_requests_before_yielding_, network_quality_estimator, this);
  client_map_[client_id] = client;
}

void ResourceScheduler::OnClientDeleted(int child_id, int route_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);
  ClientMap::iterator it = client_map_.find(client_id);
  DCHECK(it != client_map_.end());

  Client* client = it->second;
  // ResourceDispatcherHost cancels all requests except for cross-renderer
  // navigations, async revalidations and detachable requests after
  // OnClientDeleted() returns.
  RequestSet client_unowned_requests = client->StartAndRemoveAllRequests();
  for (RequestSet::iterator it = client_unowned_requests.begin();
       it != client_unowned_requests.end(); ++it) {
    unowned_requests_.insert(*it);
  }

  delete client;
  client_map_.erase(it);
}

void ResourceScheduler::OnLoadingStateChanged(int child_id,
                                              int route_id,
                                              bool is_loaded) {
  Client* client = GetClient(child_id, route_id);
  DCHECK(client);
  client->OnLoadingStateChanged(is_loaded);
}

void ResourceScheduler::OnNavigate(int child_id, int route_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);

  ClientMap::iterator it = client_map_.find(client_id);
  if (it == client_map_.end()) {
    // The client was likely deleted shortly before we received this IPC.
    return;
  }

  Client* client = it->second;
  client->OnNavigate();
}

void ResourceScheduler::OnWillInsertBody(int child_id, int route_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);

  ClientMap::iterator it = client_map_.find(client_id);
  if (it == client_map_.end()) {
    // The client was likely deleted shortly before we received this IPC.
    return;
  }

  Client* client = it->second;
  client->OnWillInsertBody();
}

void ResourceScheduler::OnReceivedSpdyProxiedHttpResponse(
    int child_id,
    int route_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ClientId client_id = MakeClientId(child_id, route_id);

  ClientMap::iterator client_it = client_map_.find(client_id);
  if (client_it == client_map_.end()) {
    return;
  }

  Client* client = client_it->second;
  client->OnReceivedSpdyProxiedHttpResponse();
}

bool ResourceScheduler::HasLoadingClients() const {
  for (const auto& client : client_map_) {
    if (!client.second->is_loaded())
      return true;
  }
  return false;
}

ResourceScheduler::Client* ResourceScheduler::GetClient(int child_id,
                                                        int route_id) {
  ClientId client_id = MakeClientId(child_id, route_id);
  ClientMap::iterator client_it = client_map_.find(client_id);
  if (client_it == client_map_.end()) {
    return NULL;
  }
  return client_it->second;
}

void ResourceScheduler::ReprioritizeRequest(net::URLRequest* request,
                                            net::RequestPriority new_priority,
                                            int new_intra_priority_value) {
  if (request->load_flags() & net::LOAD_IGNORE_LIMITS) {
    // Requests with the IGNORE_LIMITS flag must stay at MAXIMUM_PRIORITY.
    return;
  }

  auto* scheduled_resource_request =
      ScheduledResourceRequest::ForRequest(request);

  // Downloads don't use the resource scheduler.
  if (!scheduled_resource_request) {
    request->SetPriority(new_priority);
    return;
  }

  RequestPriorityParams new_priority_params(new_priority,
      new_intra_priority_value);
  RequestPriorityParams old_priority_params =
      scheduled_resource_request->get_request_priority_params();

  if (old_priority_params == new_priority_params)
    return;

  ClientMap::iterator client_it =
      client_map_.find(scheduled_resource_request->client_id());
  if (client_it == client_map_.end()) {
    // The client was likely deleted shortly before we received this IPC.
    request->SetPriority(new_priority_params.priority);
    scheduled_resource_request->set_request_priority_params(
        new_priority_params);
    return;
  }

  Client* client = client_it->second;
  client->ReprioritizeRequest(scheduled_resource_request, old_priority_params,
                              new_priority_params);
}

void ResourceScheduler::ReprioritizeRequest(net::URLRequest* request,
                                            net::RequestPriority new_priority) {
  int current_intra_priority = 0;
  auto* existing_request = ScheduledResourceRequest::ForRequest(request);
  if (existing_request) {
    current_intra_priority =
        existing_request->get_request_priority_params().intra_priority;
  }
  ReprioritizeRequest(request, new_priority, current_intra_priority);
}

ResourceScheduler::ClientId ResourceScheduler::MakeClientId(
    int child_id, int route_id) {
  return (static_cast<ResourceScheduler::ClientId>(child_id) << 32) | route_id;
}

ResourceScheduler::MaxRequestsForBDPRanges
ResourceScheduler::GetMaxDelayableRequestsExperimentConfig() {
  static const char kMaxBDPKbitsBase[] = "MaxBDPKbits";
  static const char kMaxDelayableRequestsBase[] = "MaxDelayableRequests";

  MaxRequestsForBDPRanges result;
  if (!base::FeatureList::IsEnabled(kMaxDelayableRequestsNetworkOverride))
    return result;

  int config_param_index = 1;
  while (true) {
    int64_t max_bdp_kbits;
    size_t max_delayable_requests;

    if (!base::StringToInt64(
            base::GetFieldTrialParamValueByFeature(
                kMaxDelayableRequestsNetworkOverride,
                kMaxBDPKbitsBase + base::IntToString(config_param_index)),
            &max_bdp_kbits)) {
      DCHECK_LE(result.size(), 20u);
      return result;
    }
    if (!base::StringToSizeT(base::GetFieldTrialParamValueByFeature(
                                 kMaxDelayableRequestsNetworkOverride,
                                 kMaxDelayableRequestsBase +
                                     base::IntToString(config_param_index)),
                             &max_delayable_requests)) {
      DCHECK_LE(result.size(), 20u);
      return result;
    }
    // Check that the previous bandwidth delay product is strictly less than the
    // current bandwidth delay product.
    DCHECK(result.empty() || result.back().max_bdp_kbits < max_bdp_kbits);
    result.push_back({max_bdp_kbits, max_delayable_requests});
    config_param_index++;
  }
}

net::EffectiveConnectionType
ResourceScheduler::GetMaxDelayableRequestsExperimentMaxECT() {
  static const char kMaxEffectiveConnectionType[] =
      "MaxEffectiveConnectionType";

  if (!base::FeatureList::IsEnabled(kMaxDelayableRequestsNetworkOverride))
    return net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN;

  net::EffectiveConnectionType ect;
  if (!net::GetEffectiveConnectionTypeForName(
          base::GetFieldTrialParamValueByFeature(
              kMaxDelayableRequestsNetworkOverride,
              kMaxEffectiveConnectionType),
          &ect)) {
    return net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  return ect;
}

size_t ResourceScheduler::ComputeMaxDelayableRequestsNetworkOverride(
    const net::NetworkQualityEstimator* network_quality_estimator) const {
  if (max_requests_for_bdp_ranges_.empty() || !network_quality_estimator)
    return kDefaultMaxNumDelayableRequestsPerClient;

  if (network_quality_estimator->GetEffectiveConnectionType() <=
          max_delayable_requests_threshold_ &&
      network_quality_estimator->GetEffectiveConnectionType() >
          net::EFFECTIVE_CONNECTION_TYPE_OFFLINE) {
    base::Optional<int32_t> bandwidth_delay_product =
        network_quality_estimator->GetBandwidthDelayProductKbits();
    if (bandwidth_delay_product) {
      return GetNumberOfDelayableRequestsForBDP(
          bandwidth_delay_product.value());
    }
  }
  return kDefaultMaxNumDelayableRequestsPerClient;
}

int ResourceScheduler::GetNumberOfDelayableRequestsForBDP(
    int64_t bdp_in_kbits) const {
  for (const auto& range : max_requests_for_bdp_ranges_) {
    if (bdp_in_kbits <= range.max_bdp_kbits)
      return range.max_requests;
  }
  return kDefaultMaxNumDelayableRequestsPerClient;
}

}  // namespace content
