// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/protocol/network_handler.h"

#include <stddef.h>
#include <stdint.h>

#include "base/barrier_closure.h"
#include "base/base64.h"
#include "base/command_line.h"
#include "base/containers/hash_tables.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "content/browser/devtools/devtools_session.h"
#include "content/browser/devtools/devtools_url_interceptor_request_job.h"
#include "content/browser/devtools/protocol/page.h"
#include "content/browser/devtools/protocol/security.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/common/navigation_params.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/resource_devtools_info.h"
#include "content/public/common/resource_request.h"
#include "content/public/common/resource_request_completion_status.h"
#include "content/public/common/resource_response.h"
#include "net/base/net_errors.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/cookies/cookie_store.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

namespace content {
namespace protocol {
namespace {

using ProtocolCookieArray = Array<Network::Cookie>;
using GetCookiesCallback = Network::Backend::GetCookiesCallback;
using GetAllCookiesCallback = Network::Backend::GetAllCookiesCallback;
using SetCookieCallback = Network::Backend::SetCookieCallback;
using DeleteCookieCallback = Network::Backend::DeleteCookieCallback;
using ClearBrowserCookiesCallback =
    Network::Backend::ClearBrowserCookiesCallback;

net::URLRequestContext* GetRequestContextOnIO(
    ResourceContext* resource_context,
    net::URLRequestContextGetter* context_getter,
    const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  net::URLRequestContext* context =
      GetContentClient()->browser()->OverrideRequestContextForURL(
          url, resource_context);
  if (!context)
    context = context_getter->GetURLRequestContext();
  return context;
}

class CookieRetriever : public base::RefCountedThreadSafe<CookieRetriever> {
  public:
    CookieRetriever(std::unique_ptr<GetCookiesCallback> callback)
        : callback_(std::move(callback)),
          all_callback_(nullptr) {}

    CookieRetriever(std::unique_ptr<GetAllCookiesCallback> callback)
        : callback_(nullptr),
          all_callback_(std::move(callback)) {}

    void RetrieveCookiesOnIO(
        ResourceContext* resource_context,
        net::URLRequestContextGetter* context_getter,
        const std::vector<GURL>& urls) {
      DCHECK_CURRENTLY_ON(BrowserThread::IO);
      callback_count_ = urls.size();

      if (callback_count_ == 0) {
        GotAllCookies();
        return;
      }

      for (const GURL& url : urls) {
        net::URLRequestContext* request_context =
            GetRequestContextOnIO(resource_context, context_getter, url);
        request_context->cookie_store()->GetAllCookiesForURLAsync(url,
            base::Bind(&CookieRetriever::GotCookies, this));
      }
    }

    void RetrieveAllCookiesOnIO(
        ResourceContext* resource_context,
        net::URLRequestContextGetter* context_getter) {
      DCHECK_CURRENTLY_ON(BrowserThread::IO);
      callback_count_ = 1;

      net::URLRequestContext* request_context =
          context_getter->GetURLRequestContext();
      request_context->cookie_store()->GetAllCookiesAsync(
          base::Bind(&CookieRetriever::GotCookies, this));
    }
  protected:
    virtual ~CookieRetriever() {}

    void GotCookies(const net::CookieList& cookie_list) {
      DCHECK_CURRENTLY_ON(BrowserThread::IO);
      for (const net::CanonicalCookie& cookie : cookie_list) {
        std::string key = base::StringPrintf(
            "%s::%s::%s::%d", cookie.Name().c_str(), cookie.Domain().c_str(),
            cookie.Path().c_str(), cookie.IsSecure());
        cookies_[key] = cookie;
      }

      --callback_count_;
      if (callback_count_ == 0)
        GotAllCookies();
    }

    void GotAllCookies() {
      net::CookieList master_cookie_list;
      for (const auto& pair : cookies_)
        master_cookie_list.push_back(pair.second);

      BrowserThread::PostTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&CookieRetriever::SendCookiesResponseOnUI,
                     this,
                     master_cookie_list));
    }

    void SendCookiesResponseOnUI(const net::CookieList& cookie_list) {
      DCHECK_CURRENTLY_ON(BrowserThread::UI);
      std::unique_ptr<ProtocolCookieArray> cookies =
          ProtocolCookieArray::create();

      for (const net::CanonicalCookie& cookie : cookie_list) {
       std::unique_ptr<Network::Cookie> devtools_cookie =
            Network::Cookie::Create()
            .SetName(cookie.Name())
            .SetValue(cookie.Value())
            .SetDomain(cookie.Domain())
            .SetPath(cookie.Path())
            .SetExpires(cookie.ExpiryDate().ToDoubleT() * 1000)
            .SetSize(cookie.Name().length() + cookie.Value().length())
            .SetHttpOnly(cookie.IsHttpOnly())
            .SetSecure(cookie.IsSecure())
            .SetSession(!cookie.IsPersistent())
            .Build();

       switch (cookie.SameSite()) {
         case net::CookieSameSite::STRICT_MODE:
           devtools_cookie->SetSameSite(Network::CookieSameSiteEnum::Strict);
           break;
         case net::CookieSameSite::LAX_MODE:
           devtools_cookie->SetSameSite(Network::CookieSameSiteEnum::Lax);
           break;
         case net::CookieSameSite::NO_RESTRICTION:
           break;
       }

       cookies->addItem(std::move(devtools_cookie));
      }

      if (callback_) {
        callback_->sendSuccess(std::move(cookies));
      } else {
        DCHECK(all_callback_);
        all_callback_->sendSuccess(std::move(cookies));
      }
    }

    std::unique_ptr<GetCookiesCallback> callback_;
    std::unique_ptr<GetAllCookiesCallback> all_callback_;
    int callback_count_ = 0;
    std::unordered_map<std::string, net::CanonicalCookie> cookies_;

  private:
    friend class base::RefCountedThreadSafe<CookieRetriever>;
};

void ClearedCookiesOnIO(std::unique_ptr<ClearBrowserCookiesCallback> callback,
                        uint32_t num_deleted) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
                          base::Bind(&ClearBrowserCookiesCallback::sendSuccess,
                                     base::Passed(std::move(callback))));
}

void ClearCookiesOnIO(ResourceContext* resource_context,
                      net::URLRequestContextGetter* context_getter,
                      std::unique_ptr<ClearBrowserCookiesCallback> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  net::URLRequestContext* request_context =
      context_getter->GetURLRequestContext();
  request_context->cookie_store()->DeleteAllAsync(
      base::Bind(&ClearedCookiesOnIO, base::Passed(std::move(callback))));
}

void DeletedCookieOnIO(std::unique_ptr<DeleteCookieCallback> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&DeleteCookieCallback::sendSuccess,
                 base::Passed(std::move(callback))));
}

void DeleteCookieOnIO(
    ResourceContext* resource_context,
    net::URLRequestContextGetter* context_getter,
    const GURL& url,
    const std::string& cookie_name,
    std::unique_ptr<DeleteCookieCallback> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  net::URLRequestContext* request_context =
      GetRequestContextOnIO(resource_context, context_getter, url);
  request_context->cookie_store()->DeleteCookieAsync(
      url, cookie_name, base::Bind(&DeletedCookieOnIO,
                                   base::Passed(std::move(callback))));
}

void CookieSetOnIO(std::unique_ptr<SetCookieCallback> callback, bool success) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(&SetCookieCallback::sendSuccess,
                 base::Passed(std::move(callback)),
                 success));
}

void SetCookieOnIO(
    ResourceContext* resource_context,
    net::URLRequestContextGetter* context_getter,
    const GURL& url,
    const std::string& name,
    const std::string& value,
    const std::string& domain,
    const std::string& path,
    bool secure,
    bool http_only,
    net::CookieSameSite same_site,
    base::Time expires,
    std::unique_ptr<SetCookieCallback> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  net::URLRequestContext* request_context =
      GetRequestContextOnIO(resource_context, context_getter, url);

  request_context->cookie_store()->SetCookieWithDetailsAsync(
      url, name, value, domain, path,
      base::Time(),
      expires,
      base::Time(),
      secure,
      http_only,
      same_site,
      net::COOKIE_PRIORITY_DEFAULT,
      base::Bind(&CookieSetOnIO, base::Passed(std::move(callback))));
}

std::vector<GURL> ComputeCookieURLs(RenderFrameHostImpl* frame_host,
                                    Maybe<Array<String>>& protocol_urls) {
  std::vector<GURL> urls;

  if (protocol_urls.isJust()) {
    std::unique_ptr<Array<std::string>> actual_urls = protocol_urls.takeJust();

    for (size_t i = 0; i < actual_urls->length(); i++)
      urls.push_back(GURL(actual_urls->get(i)));
  } else {
    std::queue<FrameTreeNode*> queue;
    queue.push(frame_host->frame_tree_node());
    while (!queue.empty()) {
      FrameTreeNode* node = queue.front();
      queue.pop();

      urls.push_back(node->current_url());
      for (size_t i = 0; i < node->child_count(); ++i)
        queue.push(node->child_at(i));
    }
  }

  return urls;
}

String resourcePriority(net::RequestPriority priority) {
  switch (priority) {
    case net::MINIMUM_PRIORITY:
    case net::IDLE:
      return Network::ResourcePriorityEnum::VeryLow;
    case net::LOWEST:
      return Network::ResourcePriorityEnum::Low;
    case net::LOW:
      return Network::ResourcePriorityEnum::Medium;
    case net::MEDIUM:
      return Network::ResourcePriorityEnum::High;
    case net::HIGHEST:
      return Network::ResourcePriorityEnum::VeryHigh;
  }
  NOTREACHED();
  return Network::ResourcePriorityEnum::Medium;
}

String referrerPolicy(blink::WebReferrerPolicy referrer_policy) {
  switch (referrer_policy) {
    case blink::kWebReferrerPolicyAlways:
      return Network::Request::ReferrerPolicyEnum::UnsafeUrl;
    case blink::kWebReferrerPolicyDefault:
      if (base::CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kReducedReferrerGranularity)) {
        return Network::Request::ReferrerPolicyEnum::
            StrictOriginWhenCrossOrigin;
      } else {
        return Network::Request::ReferrerPolicyEnum::NoReferrerWhenDowngrade;
      }
    case blink::kWebReferrerPolicyNoReferrerWhenDowngrade:
      return Network::Request::ReferrerPolicyEnum::NoReferrerWhenDowngrade;
    case blink::kWebReferrerPolicyNever:
      return Network::Request::ReferrerPolicyEnum::NoReferrer;
    case blink::kWebReferrerPolicyOrigin:
      return Network::Request::ReferrerPolicyEnum::Origin;
    case blink::kWebReferrerPolicyOriginWhenCrossOrigin:
      return Network::Request::ReferrerPolicyEnum::OriginWhenCrossOrigin;
    case blink::kWebReferrerPolicySameOrigin:
      return Network::Request::ReferrerPolicyEnum::SameOrigin;
    case blink::kWebReferrerPolicyStrictOrigin:
      return Network::Request::ReferrerPolicyEnum::StrictOrigin;
    case blink::kWebReferrerPolicyNoReferrerWhenDowngradeOriginWhenCrossOrigin:
      return Network::Request::ReferrerPolicyEnum::StrictOriginWhenCrossOrigin;
  }
  NOTREACHED();
  return Network::Request::ReferrerPolicyEnum::NoReferrerWhenDowngrade;
}

String referrerPolicy(net::URLRequest::ReferrerPolicy referrer_policy) {
  switch (referrer_policy) {
    case net::URLRequest::CLEAR_REFERRER_ON_TRANSITION_FROM_SECURE_TO_INSECURE:
      return Network::Request::ReferrerPolicyEnum::NoReferrerWhenDowngrade;
    case net::URLRequest::
        REDUCE_REFERRER_GRANULARITY_ON_TRANSITION_CROSS_ORIGIN:
      return Network::Request::ReferrerPolicyEnum::StrictOriginWhenCrossOrigin;
    case net::URLRequest::ORIGIN_ONLY_ON_TRANSITION_CROSS_ORIGIN:
      return Network::Request::ReferrerPolicyEnum::OriginWhenCrossOrigin;
    case net::URLRequest::NEVER_CLEAR_REFERRER:
      return Network::Request::ReferrerPolicyEnum::Origin;
    case net::URLRequest::ORIGIN:
      return Network::Request::ReferrerPolicyEnum::Origin;
    case net::URLRequest::NO_REFERRER:
      return Network::Request::ReferrerPolicyEnum::NoReferrer;
    default:
      break;
  }
  NOTREACHED();
  return Network::Request::ReferrerPolicyEnum::NoReferrerWhenDowngrade;
}

String securityState(const GURL& url, const net::CertStatus& cert_status) {
  if (!url.SchemeIsCryptographic())
    return Security::SecurityStateEnum::Neutral;
  if (net::IsCertStatusError(cert_status) &&
      !net::IsCertStatusMinorError(cert_status)) {
    return Security::SecurityStateEnum::Insecure;
  }
  return Security::SecurityStateEnum::Secure;
}

net::Error NetErrorFromString(const std::string& error, bool* ok) {
  *ok = true;
  if (error == Network::ErrorReasonEnum::Failed)
    return net::ERR_FAILED;
  if (error == Network::ErrorReasonEnum::Aborted)
    return net::ERR_ABORTED;
  if (error == Network::ErrorReasonEnum::TimedOut)
    return net::ERR_TIMED_OUT;
  if (error == Network::ErrorReasonEnum::AccessDenied)
    return net::ERR_ACCESS_DENIED;
  if (error == Network::ErrorReasonEnum::ConnectionClosed)
    return net::ERR_CONNECTION_CLOSED;
  if (error == Network::ErrorReasonEnum::ConnectionReset)
    return net::ERR_CONNECTION_RESET;
  if (error == Network::ErrorReasonEnum::ConnectionRefused)
    return net::ERR_CONNECTION_REFUSED;
  if (error == Network::ErrorReasonEnum::ConnectionAborted)
    return net::ERR_CONNECTION_ABORTED;
  if (error == Network::ErrorReasonEnum::ConnectionFailed)
    return net::ERR_CONNECTION_FAILED;
  if (error == Network::ErrorReasonEnum::NameNotResolved)
    return net::ERR_NAME_NOT_RESOLVED;
  if (error == Network::ErrorReasonEnum::InternetDisconnected)
    return net::ERR_INTERNET_DISCONNECTED;
  if (error == Network::ErrorReasonEnum::AddressUnreachable)
    return net::ERR_ADDRESS_UNREACHABLE;
  *ok = false;
  return net::ERR_FAILED;
}

double timeDelta(base::TimeTicks time,
                 base::TimeTicks start,
                 double invalid_value = -1) {
  return time.is_null() ? invalid_value : (time - start).InMillisecondsF();
}

std::unique_ptr<Network::ResourceTiming> getTiming(
    const net::LoadTimingInfo& load_timing) {
  const base::TimeTicks kNullTicks;
  return Network::ResourceTiming::Create()
      .SetRequestTime((load_timing.request_start - kNullTicks).InSecondsF())
      .SetProxyStart(
          timeDelta(load_timing.proxy_resolve_start, load_timing.request_start))
      .SetProxyEnd(
          timeDelta(load_timing.proxy_resolve_end, load_timing.request_start))
      .SetDnsStart(timeDelta(load_timing.connect_timing.dns_start,
                             load_timing.request_start))
      .SetDnsEnd(timeDelta(load_timing.connect_timing.dns_end,
                           load_timing.request_start))
      .SetConnectStart(timeDelta(load_timing.connect_timing.connect_start,
                                 load_timing.request_start))
      .SetConnectEnd(timeDelta(load_timing.connect_timing.connect_end,
                               load_timing.request_start))
      .SetSslStart(timeDelta(load_timing.connect_timing.ssl_start,
                             load_timing.request_start))
      .SetSslEnd(timeDelta(load_timing.connect_timing.ssl_end,
                           load_timing.request_start))
      .SetWorkerStart(-1)
      .SetWorkerReady(-1)
      .SetSendStart(
          timeDelta(load_timing.send_start, load_timing.request_start))
      .SetSendEnd(timeDelta(load_timing.send_end, load_timing.request_start))
      .SetPushStart(
          timeDelta(load_timing.push_start, load_timing.request_start, 0))
      .SetPushEnd(timeDelta(load_timing.push_end, load_timing.request_start, 0))
      .SetReceiveHeadersEnd(
          timeDelta(load_timing.receive_headers_end, load_timing.request_start))
      .Build();
}

std::unique_ptr<Object> getHeaders(const base::StringPairs& pairs) {
  std::unique_ptr<DictionaryValue> headers_dict(DictionaryValue::create());
  for (const auto& pair : pairs) {
    headers_dict->setString(pair.first, pair.second);
  }
  return Object::fromValue(headers_dict.get(), nullptr);
}

String getProtocol(const GURL& url, const ResourceResponseHead& head) {
  std::string protocol = head.alpn_negotiated_protocol;
  if (protocol.empty() || protocol == "unknown") {
    if (head.was_fetched_via_spdy) {
      protocol = "spdy";
    } else if (url.SchemeIsHTTPOrHTTPS()) {
      protocol = "http";
      if (head.headers->GetHttpVersion() == net::HttpVersion(0, 9))
        protocol = "http/0.9";
      else if (head.headers->GetHttpVersion() == net::HttpVersion(1, 0))
        protocol = "http/1.0";
      else if (head.headers->GetHttpVersion() == net::HttpVersion(1, 1))
        protocol = "http/1.1";
    } else {
      protocol = url.scheme();
    }
  }
  return protocol;
}

class NetworkNavigationThrottle : public content::NavigationThrottle {
 public:
  NetworkNavigationThrottle(
      base::WeakPtr<protocol::NetworkHandler> network_handler,
      content::NavigationHandle* navigation_handle)
      : content::NavigationThrottle(navigation_handle),
        network_handler_(network_handler) {}

  ~NetworkNavigationThrottle() override {}

  // content::NavigationThrottle implementation:
  NavigationThrottle::ThrottleCheckResult WillProcessResponse() override {
    if (network_handler_ && network_handler_->ShouldCancelNavigation(
                                navigation_handle()->GetGlobalRequestID())) {
      return ThrottleCheckResult::CANCEL_AND_IGNORE;
    }
    return ThrottleCheckResult::PROCEED;
  }

  const char* GetNameForLogging() override {
    return "DevToolsNetworkNavigationThrottle";
  }

 private:
  base::WeakPtr<protocol::NetworkHandler> network_handler_;
  DISALLOW_COPY_AND_ASSIGN(NetworkNavigationThrottle);
};

}  // namespace

NetworkHandler::NetworkHandler()
    : DevToolsDomainHandler(Network::Metainfo::domainName),
      host_(nullptr),
      enabled_(false),
      interception_enabled_(false),
      weak_factory_(this) {}

NetworkHandler::~NetworkHandler() {
}

// static
std::vector<NetworkHandler*> NetworkHandler::ForAgentHost(
    DevToolsAgentHostImpl* host) {
  return DevToolsSession::HandlersForAgentHost<NetworkHandler>(
      host, Network::Metainfo::domainName);
}

void NetworkHandler::Wire(UberDispatcher* dispatcher) {
  frontend_.reset(new Network::Frontend(dispatcher->channel()));
  Network::Dispatcher::wire(dispatcher, this);
}

void NetworkHandler::SetRenderFrameHost(RenderFrameHostImpl* host) {
  host_ = host;
}

Response NetworkHandler::Enable(Maybe<int> max_total_size,
                                Maybe<int> max_resource_size) {
  enabled_ = true;
  return Response::FallThrough();
}

Response NetworkHandler::Disable() {
  enabled_ = false;
  user_agent_ = std::string();
  SetRequestInterceptionEnabled(false);
  return Response::FallThrough();
}

Response NetworkHandler::ClearBrowserCache() {
  if (host_) {
    content::BrowsingDataRemover* remover =
        content::BrowserContext::GetBrowsingDataRemover(
            host_->GetSiteInstance()->GetProcess()->GetBrowserContext());
    remover->Remove(base::Time(), base::Time::Max(),
                    content::BrowsingDataRemover::DATA_TYPE_CACHE,
                    content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB);
  }

  return Response::OK();
}

void NetworkHandler::ClearBrowserCookies(
    std::unique_ptr<ClearBrowserCookiesCallback> callback) {
  if (!host_) {
    callback->sendFailure(Response::InternalError());
    return;
  }

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&ClearCookiesOnIO,
                 base::Unretained(host_->GetSiteInstance()
                                      ->GetBrowserContext()
                                      ->GetResourceContext()),
                 base::Unretained(host_->GetProcess()
                                      ->GetStoragePartition()
                                      ->GetURLRequestContext()),
                 base::Passed(std::move(callback))));
}

void NetworkHandler::GetCookies(Maybe<Array<String>> protocol_urls,
                                std::unique_ptr<GetCookiesCallback> callback) {
  if (!host_) {
    callback->sendFailure(Response::InternalError());
    return;
  }

  std::vector<GURL> urls = ComputeCookieURLs(host_, protocol_urls);
  scoped_refptr<CookieRetriever> retriever =
      new CookieRetriever(std::move(callback));

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&CookieRetriever::RetrieveCookiesOnIO,
                 retriever,
                 base::Unretained(host_->GetSiteInstance()
                                       ->GetBrowserContext()
                                       ->GetResourceContext()),
                 base::Unretained(host_->GetProcess()
                                       ->GetStoragePartition()
                                       ->GetURLRequestContext()),
                 urls));
}

void NetworkHandler::GetAllCookies(
    std::unique_ptr<GetAllCookiesCallback> callback) {
  if (!host_) {
    callback->sendFailure(Response::InternalError());
    return;
  }

  scoped_refptr<CookieRetriever> retriever =
      new CookieRetriever(std::move(callback));

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&CookieRetriever::RetrieveAllCookiesOnIO,
                 retriever,
                 base::Unretained(host_->GetSiteInstance()
                                       ->GetBrowserContext()
                                       ->GetResourceContext()),
                 base::Unretained(host_->GetProcess()
                                       ->GetStoragePartition()
                                       ->GetURLRequestContext())));
}

void NetworkHandler::SetCookie(
    const std::string& url,
    const std::string& name,
    const std::string& value,
    Maybe<std::string> domain,
    Maybe<std::string> path,
    Maybe<bool> secure,
    Maybe<bool> http_only,
    Maybe<std::string> same_site,
    Maybe<double> expires,
    std::unique_ptr<SetCookieCallback> callback) {
  if (!host_) {
    callback->sendFailure(Response::InternalError());
    return;
  }

  net::CookieSameSite same_site_enum = net::CookieSameSite::DEFAULT_MODE;
  if (same_site.isJust()) {
    if (same_site.fromJust() == Network::CookieSameSiteEnum::Lax)
      same_site_enum = net::CookieSameSite::LAX_MODE;
    else if (same_site.fromJust() == Network::CookieSameSiteEnum::Strict)
      same_site_enum = net::CookieSameSite::STRICT_MODE;
  }

  base::Time expiration_date;
  if (expires.isJust()) {
    expiration_date = expires.fromJust() == 0
            ? base::Time::UnixEpoch()
            : base::Time::FromDoubleT(expires.fromJust());
  }

  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &SetCookieOnIO,
      base::Unretained(host_->GetSiteInstance()->GetBrowserContext()->
                       GetResourceContext()),
      base::Unretained(host_->GetProcess()->GetStoragePartition()->
                       GetURLRequestContext()),
      GURL(url), name, value, domain.fromMaybe(""), path.fromMaybe(""),
      secure.fromMaybe(false), http_only.fromMaybe(false), same_site_enum,
      expiration_date, base::Passed(std::move(callback))));
}

void NetworkHandler::DeleteCookie(
    const std::string& cookie_name,
    const std::string& url,
    std::unique_ptr<DeleteCookieCallback> callback) {
  if (!host_) {
    callback->sendFailure(Response::InternalError());
    return;
  }
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &DeleteCookieOnIO,
      base::Unretained(host_->GetSiteInstance()->GetBrowserContext()->
                       GetResourceContext()),
      base::Unretained(host_->GetProcess()->GetStoragePartition()->
                       GetURLRequestContext()),
      GURL(url),
      cookie_name,
      base::Passed(std::move(callback))));
}

Response NetworkHandler::SetUserAgentOverride(const std::string& user_agent) {
  if (user_agent.find('\n') != std::string::npos ||
      user_agent.find('\r') != std::string::npos ||
      user_agent.find('\0') != std::string::npos) {
    return Response::InvalidParams("Invalid characters found in userAgent");
  }
  user_agent_ = user_agent;
  return Response::FallThrough();
}

Response NetworkHandler::CanEmulateNetworkConditions(bool* result) {
  *result = false;
  return Response::OK();
}

void NetworkHandler::NavigationPreloadRequestSent(
    int worker_version_id,
    const std::string& request_id,
    const ResourceRequest& request) {
  if (!enabled_)
    return;
  const std::string version_id(base::IntToString(worker_version_id));
  std::unique_ptr<DictionaryValue> headers_dict(DictionaryValue::create());
  net::HttpRequestHeaders headers;
  headers.AddHeadersFromString(request.headers);
  for (net::HttpRequestHeaders::Iterator it(headers); it.GetNext();)
    headers_dict->setString(it.name(), it.value());
  frontend_->RequestWillBeSent(
      request_id, "" /* loader_id */, request.url.spec(),
      Network::Request::Create()
          .SetUrl(request.url.spec())
          .SetMethod(request.method)
          .SetHeaders(Object::fromValue(headers_dict.get(), nullptr))
          .SetInitialPriority(resourcePriority(request.priority))
          .SetReferrerPolicy(referrerPolicy(request.referrer_policy))
          .Build(),
      base::TimeTicks::Now().ToInternalValue() /
          static_cast<double>(base::Time::kMicrosecondsPerSecond),
      base::Time::Now().ToDoubleT(),
      Network::Initiator::Create()
          .SetType(Network::Initiator::TypeEnum::Preload)
          .Build(),
      std::unique_ptr<Network::Response>(),
      std::string(Page::ResourceTypeEnum::Other));
}

void NetworkHandler::NavigationPreloadResponseReceived(
    int worker_version_id,
    const std::string& request_id,
    const GURL& url,
    const ResourceResponseHead& head) {
  if (!enabled_)
    return;
  const std::string version_id(base::IntToString(worker_version_id));
  std::unique_ptr<DictionaryValue> headers_dict(DictionaryValue::create());
  size_t iterator = 0;
  std::string name;
  std::string value;
  while (head.headers->EnumerateHeaderLines(&iterator, &name, &value))
    headers_dict->setString(name, value);
  std::unique_ptr<Network::Response> response(
      Network::Response::Create()
          .SetUrl(url.spec())
          .SetStatus(head.headers->response_code())
          .SetStatusText(head.headers->GetStatusText())
          .SetHeaders(Object::fromValue(headers_dict.get(), nullptr))
          .SetMimeType(head.mime_type)
          .SetConnectionReused(head.load_timing.socket_reused)
          .SetConnectionId(head.load_timing.socket_log_id)
          .SetSecurityState(securityState(url, head.cert_status))
          .SetEncodedDataLength(head.encoded_data_length)
          .SetTiming(getTiming(head.load_timing))
          .SetFromDiskCache(!head.load_timing.request_start_time.is_null() &&
                            head.response_time <
                                head.load_timing.request_start_time)
          .Build());
  if (head.devtools_info) {
    if (head.devtools_info->http_status_code) {
      response->SetStatus(head.devtools_info->http_status_code);
      response->SetStatusText(head.devtools_info->http_status_text);
    }
    if (head.devtools_info->request_headers.size()) {
      response->SetRequestHeaders(
          getHeaders(head.devtools_info->request_headers));
    }
    if (!head.devtools_info->request_headers_text.empty()) {
      response->SetRequestHeadersText(
          head.devtools_info->request_headers_text);
    }
    if (head.devtools_info->response_headers.size())
      response->SetHeaders(getHeaders(head.devtools_info->response_headers));
    if (!head.devtools_info->response_headers_text.empty())
      response->SetHeadersText(head.devtools_info->response_headers_text);
  }
  response->SetProtocol(getProtocol(url, head));
  response->SetRemoteIPAddress(head.socket_address.HostForURL());
  response->SetRemotePort(head.socket_address.port());
  frontend_->ResponseReceived(
      request_id, "" /* loader_id */,
      base::TimeTicks::Now().ToInternalValue() /
          static_cast<double>(base::Time::kMicrosecondsPerSecond),
      Page::ResourceTypeEnum::Other, std::move(response));
}

void NetworkHandler::NavigationPreloadCompleted(
    const std::string& request_id,
    const ResourceRequestCompletionStatus& completion_status) {
  if (!enabled_)
    return;
  if (completion_status.error_code != net::OK) {
    frontend_->LoadingFailed(
        request_id,
        base::TimeTicks::Now().ToInternalValue() /
            static_cast<double>(base::Time::kMicrosecondsPerSecond),
        Page::ResourceTypeEnum::Other,
        net::ErrorToString(completion_status.error_code),
        completion_status.error_code == net::Error::ERR_ABORTED);
  }
  frontend_->LoadingFinished(
      request_id,
      completion_status.completion_time.ToInternalValue() /
          static_cast<double>(base::Time::kMicrosecondsPerSecond),
      completion_status.encoded_data_length);
}

void NetworkHandler::NavigationFailed(
    const CommonNavigationParams& common_params,
    const BeginNavigationParams& begin_params,
    net::Error error_code) {
  if (!enabled_)
    return;

  static int next_id = 0;
  std::string request_id = base::IntToString(base::GetCurrentProcId()) + "." +
                           base::IntToString(++next_id);
  std::string error_string = net::ErrorToString(error_code);
  bool cancelled = error_code == net::Error::ERR_ABORTED;

  std::unique_ptr<DictionaryValue> headers_dict(DictionaryValue::create());
  net::HttpRequestHeaders headers;
  headers.AddHeadersFromString(begin_params.headers);
  for (net::HttpRequestHeaders::Iterator it(headers); it.GetNext();)
    headers_dict->setString(it.name(), it.value());
  frontend_->RequestWillBeSent(
      request_id, "" /* loader_id */, common_params.url.spec(),
      Network::Request::Create()
          .SetUrl(common_params.url.spec())
          .SetMethod(common_params.method)
          .SetHeaders(Object::fromValue(headers_dict.get(), nullptr))
          // Note: the priority value is copied from
          // ResourceDispatcherHostImpl::BeginNavigationRequest but there isn't
          // a good way of sharing this.
          .SetInitialPriority(resourcePriority(net::HIGHEST))
          .SetReferrerPolicy(referrerPolicy(common_params.referrer.policy))
          .Build(),
      base::TimeTicks::Now().ToInternalValue() /
          static_cast<double>(base::Time::kMicrosecondsPerSecond),
      base::Time::Now().ToDoubleT(),
      Network::Initiator::Create()
          .SetType(Network::Initiator::TypeEnum::Parser)
          .Build(),
      std::unique_ptr<Network::Response>(),
      std::string(Page::ResourceTypeEnum::Document));

  frontend_->LoadingFailed(
      request_id,
      base::TimeTicks::Now().ToInternalValue() /
          static_cast<double>(base::Time::kMicrosecondsPerSecond),
      Page::ResourceTypeEnum::Document, error_string, cancelled);
}

std::string NetworkHandler::UserAgentOverride() const {
  return enabled_ ? user_agent_ : std::string();
}

DispatchResponse NetworkHandler::SetRequestInterceptionEnabled(bool enabled) {
  if (interception_enabled_ == enabled)
    return Response::OK();  // Nothing to do.

  WebContents* web_contents = WebContents::FromRenderFrameHost(host_);
  if (!web_contents)
    return Response::OK();

  DevToolsURLRequestInterceptor* devtools_url_request_interceptor =
      DevToolsURLRequestInterceptor::FromBrowserContext(
          web_contents->GetBrowserContext());
  if (!devtools_url_request_interceptor)
    return Response::OK();

  if (enabled) {
    devtools_url_request_interceptor->state()->StartInterceptingRequests(
        web_contents, weak_factory_.GetWeakPtr());
  } else {
    devtools_url_request_interceptor->state()->StopInterceptingRequests(
        web_contents);
    navigation_requests_.clear();
    canceled_navigation_requests_.clear();
  }
  interception_enabled_ = enabled;
  return Response::OK();
}

namespace {
bool GetPostData(const net::URLRequest* request, std::string* post_data) {
  if (!request->has_upload())
    return false;

  const net::UploadDataStream* stream = request->get_upload();
  if (!stream->GetElementReaders())
    return false;

  const auto* element_readers = stream->GetElementReaders();
  if (element_readers->empty())
    return false;

  *post_data = "";
  for (const auto& element_reader : *element_readers) {
    const net::UploadBytesElementReader* reader =
        element_reader->AsBytesReader();
    // TODO(alexclarke): This should really be base64 encoded.
    *post_data += std::string(reader->bytes(), reader->length());
  }
  return true;
}
}  // namespace

// TODO(alexclarke): Support structured data as well as |base64_raw_response|.
void NetworkHandler::ContinueInterceptedRequest(
    const std::string& interception_id,
    Maybe<std::string> error_reason,
    Maybe<std::string> base64_raw_response,
    Maybe<std::string> url,
    Maybe<std::string> method,
    Maybe<std::string> post_data,
    Maybe<protocol::Network::Headers> headers,
    Maybe<protocol::Network::AuthChallengeResponse> auth_challenge_response,
    std::unique_ptr<ContinueInterceptedRequestCallback> callback) {
  DevToolsURLRequestInterceptor* devtools_url_request_interceptor =
      DevToolsURLRequestInterceptor::FromBrowserContext(
          WebContents::FromRenderFrameHost(host_)->GetBrowserContext());
  if (!devtools_url_request_interceptor) {
    callback->sendFailure(Response::InternalError());
    return;
  }

  base::Optional<std::string> raw_response;
  if (base64_raw_response.isJust()) {
    std::string decoded;
    if (!base::Base64Decode(base64_raw_response.fromJust(), &decoded)) {
      callback->sendFailure(Response::InvalidParams("Invalid rawResponse."));
      return;
    }
    raw_response = decoded;
  }

  base::Optional<net::Error> error;
  if (error_reason.isJust()) {
    bool ok;
    error = NetErrorFromString(error_reason.fromJust(), &ok);
    if (!ok) {
      callback->sendFailure(Response::InvalidParams("Invalid errorReason."));
      return;
    }

    if (error_reason.fromJust() == Network::ErrorReasonEnum::Aborted) {
      auto it = navigation_requests_.find(interception_id);
      if (it != navigation_requests_.end()) {
        canceled_navigation_requests_.insert(it->second);
        // To successfully cancel navigation the request must succeed. We
        // provide simple mock response to avoid pointless network fetch.
        error.reset();
        raw_response = std::string("HTTP/1.1 200 OK\r\n\r\n");
      }
    }
  }

  devtools_url_request_interceptor->state()->ContinueInterceptedRequest(
      interception_id,
      base::MakeUnique<DevToolsURLRequestInterceptor::Modifications>(
          std::move(error), std::move(raw_response), std::move(url),
          std::move(method), std::move(post_data), std::move(headers),
          std::move(auth_challenge_response)),
      std::move(callback));
}

// static
std::unique_ptr<Network::Request> NetworkHandler::CreateRequestFromURLRequest(
    const net::URLRequest* request) {
  std::unique_ptr<DictionaryValue> headers_dict(DictionaryValue::create());
  for (net::HttpRequestHeaders::Iterator it(request->extra_request_headers());
       it.GetNext();) {
    headers_dict->setString(it.name(), it.value());
  }
  std::unique_ptr<protocol::Network::Request> request_object =
      Network::Request::Create()
          .SetUrl(request->url().spec())
          .SetMethod(request->method())
          .SetHeaders(Object::fromValue(headers_dict.get(), nullptr))
          .SetInitialPriority(resourcePriority(request->priority()))
          .SetReferrerPolicy(referrerPolicy(request->referrer_policy()))
          .Build();
  std::string post_data;
  if (GetPostData(request, &post_data))
    request_object->SetPostData(std::move(post_data));
  return request_object;
}

std::unique_ptr<NavigationThrottle> NetworkHandler::CreateThrottleForNavigation(
    NavigationHandle* navigation_handle) {
  if (!interception_enabled_)
    return nullptr;
  std::unique_ptr<NavigationThrottle> throttle(new NetworkNavigationThrottle(
      weak_factory_.GetWeakPtr(), navigation_handle));
  return throttle;
}

void NetworkHandler::InterceptedNavigationRequest(
    const GlobalRequestID& global_request_id,
    const std::string& interception_id) {
  navigation_requests_[interception_id] = global_request_id;
}

void NetworkHandler::InterceptedNavigationRequestFinished(
    const std::string& interception_id) {
  navigation_requests_.erase(interception_id);
}

bool NetworkHandler::ShouldCancelNavigation(
    const GlobalRequestID& global_request_id) {
  auto it = canceled_navigation_requests_.find(global_request_id);
  if (it == canceled_navigation_requests_.end())
    return false;
  canceled_navigation_requests_.erase(it);
  return true;
}

}  // namespace protocol
}  // namespace content
