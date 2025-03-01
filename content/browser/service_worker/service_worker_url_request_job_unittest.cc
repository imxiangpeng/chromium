// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/service_worker/service_worker_url_request_job.h"

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/histogram_tester.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/loader/resource_request_info_impl.h"
#include "content/browser/resource_context_impl.h"
#include "content/browser/service_worker/embedded_worker_registry.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_provider_host.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/browser/service_worker/service_worker_response_info.h"
#include "content/browser/service_worker/service_worker_test_utils.h"
#include "content/browser/service_worker/service_worker_version.h"
#include "content/common/service_worker/service_worker_messages.h"
#include "content/common/service_worker/service_worker_status_code.h"
#include "content/common/service_worker/service_worker_types.h"
#include "content/public/browser/blob_handle.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/request_context_frame_type.h"
#include "content/public/common/request_context_type.h"
#include "content/public/common/resource_request_body.h"
#include "content/public/common/resource_type.h"
#include "content/public/common/service_worker_modes.h"
#include "content/public/test/mock_resource_context.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/base/io_buffer.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/ssl/ssl_info.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_job.h"
#include "net/url_request/url_request_test_util.h"
#include "storage/browser/blob/blob_data_builder.h"
#include "storage/browser/blob/blob_storage_context.h"
#include "storage/browser/blob/blob_url_request_job.h"
#include "storage/browser/blob/blob_url_request_job_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

const int kProviderID = 100;
const char kTestData[] = "Here is sample text for the blob.";

// A simple ProtocolHandler implementation to create ServiceWorkerURLRequestJob.
//
// MockProtocolHandler is basically a mock of
// ServiceWorkerControlleeRequestHandler. In production code,
// ServiceWorkerControlleeRequestHandler::MaybeCreateJob() is called by
// ServiceWorkerRequestInterceptor, a custom URLRequestInterceptor, but for
// testing it's easier to make the job via ProtocolHandler.
class MockProtocolHandler : public net::URLRequestJobFactory::ProtocolHandler {
 public:
  MockProtocolHandler(
      base::WeakPtr<ServiceWorkerProviderHost> provider_host,
      const ResourceContext* resource_context,
      base::WeakPtr<storage::BlobStorageContext> blob_storage_context,
      ServiceWorkerURLRequestJob::Delegate* delegate)
      : provider_host_(provider_host),
        resource_context_(resource_context),
        blob_storage_context_(blob_storage_context),
        job_(nullptr),
        delegate_(delegate),
        resource_type_(RESOURCE_TYPE_MAIN_FRAME),
        simulate_navigation_preload_(false) {}

  ~MockProtocolHandler() override = default;

  void set_resource_type(ResourceType type) { resource_type_ = type; }
  void set_custom_timeout(base::Optional<base::TimeDelta> timeout) {
    custom_timeout_ = timeout;
  }
  void set_simulate_navigation_preload() {
    simulate_navigation_preload_ = true;
  }

  // A simple version of
  // ServiceWorkerControlleeRequestHandler::MaybeCreateJob().
  net::URLRequestJob* MaybeCreateJob(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const override {
    if (job_ && job_->ShouldFallbackToNetwork()) {
      // Simulate fallback to network by constructing a valid response.
      return new net::URLRequestTestJob(request, network_delegate,
                                        net::URLRequestTestJob::test_headers(),
                                        "PASS", true);
    }

    job_ = new ServiceWorkerURLRequestJob(
        request, network_delegate, provider_host_->client_uuid(),
        blob_storage_context_, resource_context_, FETCH_REQUEST_MODE_NO_CORS,
        FETCH_CREDENTIALS_MODE_OMIT, FetchRedirectMode::FOLLOW_MODE,
        std::string() /* integrity */, resource_type_,
        REQUEST_CONTEXT_TYPE_HYPERLINK, REQUEST_CONTEXT_FRAME_TYPE_TOP_LEVEL,
        scoped_refptr<ResourceRequestBody>(), ServiceWorkerFetchType::FETCH,
        custom_timeout_, delegate_);
    if (simulate_navigation_preload_) {
      job_->set_simulate_navigation_preload_for_test();
    }
    job_->ForwardToServiceWorker();
    return job_;
  }
  ServiceWorkerURLRequestJob* job() { return job_; }

 private:
  base::WeakPtr<ServiceWorkerProviderHost> provider_host_;
  const ResourceContext* resource_context_;
  base::WeakPtr<storage::BlobStorageContext> blob_storage_context_;
  mutable ServiceWorkerURLRequestJob* job_;
  ServiceWorkerURLRequestJob::Delegate* delegate_;
  ResourceType resource_type_;
  bool simulate_navigation_preload_;
  base::Optional<base::TimeDelta> custom_timeout_;
};

// Returns a BlobProtocolHandler that uses |blob_storage_context|. Caller owns
// the memory.
std::unique_ptr<storage::BlobProtocolHandler> CreateMockBlobProtocolHandler(
    storage::BlobStorageContext* blob_storage_context) {
  return base::MakeUnique<storage::BlobProtocolHandler>(blob_storage_context,
                                                        nullptr);
}

std::unique_ptr<ServiceWorkerHeaderMap> MakeHeaders() {
  auto headers = base::MakeUnique<ServiceWorkerHeaderMap>();
  (*headers)["Pineapple"] = "Pen";
  (*headers)["Foo"] = "Bar";
  (*headers)["Set-Cookie"] = "CookieCookieCookie";
  return headers;
}

void SaveStatusCallback(ServiceWorkerStatusCode* out_status,
                        ServiceWorkerStatusCode status) {
  *out_status = status;
}

}  // namespace

// ServiceWorkerURLRequestJobTest is for testing the handling of URL requests by
// a service worker.
//
// To use it, call SetUpWithHelper() in your test. This sets up the service
// worker and the scaffolding to make the worker handle https URLRequests.  (Of
// course, no actual service worker runs in the unit test, it is simulated via
// EmbeddedWorkerTestHelper receiving IPC messages from the browser and
// responding as if a service worker is running in the renderer.) Example:
//
//    auto request = url_request_context_.CreateRequest(
//        GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
//        &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
//    request->set_method("GET");
//    request->Start();
//    base::RunLoop().RunUntilIdle();
//    // Now the request was handled by a ServiceWorkerURLRequestJob.
//
// ServiceWorkerURLRequestJobTest is also a
// ServiceWorkerURLRequestJob::Delegate. In production code,
// ServiceWorkerControlleeRequestHandler is the Delegate (for non-"foreign
// fetch" request interceptions). So this class also basically mocks that part
// of ServiceWorkerControlleeRequestHandler.
class ServiceWorkerURLRequestJobTest
    : public testing::Test,
      public ServiceWorkerURLRequestJob::Delegate {
 public:
  MockProtocolHandler* handler() { return protocol_handler_; }

 protected:
  ServiceWorkerURLRequestJobTest()
      : thread_bundle_(TestBrowserThreadBundle::IO_MAINLOOP) {}
  ~ServiceWorkerURLRequestJobTest() override {}

  void SetUp() override {
    browser_context_.reset(new TestBrowserContext);
    InitializeResourceContext(browser_context_.get());
  }

  void SetUpWithHelper(std::unique_ptr<EmbeddedWorkerTestHelper> helper) {
    helper_ = std::move(helper);

    // Create a registration and service worker version.
    registration_ = new ServiceWorkerRegistration(
        ServiceWorkerRegistrationOptions(GURL("https://example.com/")), 1L,
        helper_->context()->AsWeakPtr());
    version_ = new ServiceWorkerVersion(
        registration_.get(), GURL("https://example.com/service_worker.js"), 1L,
        helper_->context()->AsWeakPtr());
    std::vector<ServiceWorkerDatabase::ResourceRecord> records;
    records.push_back(
        ServiceWorkerDatabase::ResourceRecord(10, version_->script_url(), 100));
    version_->script_cache_map()->SetResources(records);
    version_->set_fetch_handler_existence(
        ServiceWorkerVersion::FetchHandlerExistence::EXISTS);

    // Make the registration findable via storage functions.
    helper_->context()->storage()->LazyInitialize(base::Bind(&base::DoNothing));
    base::RunLoop().RunUntilIdle();
    ServiceWorkerStatusCode status = SERVICE_WORKER_ERROR_FAILED;
    helper_->context()->storage()->StoreRegistration(
        registration_.get(),
        version_.get(),
        CreateReceiverOnCurrentThread(&status));
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(SERVICE_WORKER_OK, status);

    // Set HTTP response info on the version.
    net::HttpResponseInfo http_info;
    http_info.ssl_info.cert =
        net::ImportCertFromFile(net::GetTestCertsDirectory(), "ok_cert.pem");
    EXPECT_TRUE(http_info.ssl_info.is_valid());
    http_info.ssl_info.security_bits = 0x100;
    // SSL3 TLS_DHE_RSA_WITH_AES_256_CBC_SHA
    http_info.ssl_info.connection_status = 0x300039;
    http_info.headers = make_scoped_refptr(new net::HttpResponseHeaders(""));
    version_->SetMainScriptHttpResponseInfo(http_info);

    // Create a controlled client.
    std::unique_ptr<ServiceWorkerProviderHost> provider_host =
        CreateProviderHostForWindow(
            helper_->mock_render_process_id(), kProviderID,
            true /* is_parent_frame_secure */, helper_->context()->AsWeakPtr(),
            &remote_endpoint_);
    provider_host_ = provider_host->AsWeakPtr();
    provider_host->SetDocumentUrl(GURL("https://example.com/"));
    registration_->SetActiveVersion(version_);
    provider_host->AssociateRegistration(registration_.get(),
                                         false /* notify_controllerchange */);

    // Set up scaffolding for handling URL requests.
    ChromeBlobStorageContext* chrome_blob_storage_context =
        ChromeBlobStorageContext::GetFor(browser_context_.get());
    // Wait for chrome_blob_storage_context to finish initializing.
    base::RunLoop().RunUntilIdle();
    storage::BlobStorageContext* blob_storage_context =
        chrome_blob_storage_context->context();
    url_request_job_factory_.reset(new net::URLRequestJobFactoryImpl);
    std::unique_ptr<MockProtocolHandler> handler(new MockProtocolHandler(
        provider_host->AsWeakPtr(), browser_context_->GetResourceContext(),
        blob_storage_context->AsWeakPtr(), this));
    protocol_handler_ = handler.get();
    url_request_job_factory_->SetProtocolHandler("https", std::move(handler));
    url_request_job_factory_->SetProtocolHandler(
        "blob", CreateMockBlobProtocolHandler(blob_storage_context));
    url_request_context_.set_job_factory(url_request_job_factory_.get());

    helper_->context()->AddProviderHost(std::move(provider_host));
  }

  void TearDown() override {
    version_ = nullptr;
    registration_ = nullptr;
    helper_.reset();
    request_.reset();
  }

  void TestRequestResult(int expected_status_code,
                         const std::string& expected_status_text,
                         const std::string& expected_response,
                         bool expect_valid_ssl) {
    EXPECT_EQ(net::OK, url_request_delegate_.request_status());
    EXPECT_EQ(expected_status_code,
              request_->response_headers()->response_code());
    EXPECT_EQ(expected_status_text,
              request_->response_headers()->GetStatusText());
    EXPECT_EQ(expected_response, url_request_delegate_.data_received());
    const net::SSLInfo& ssl_info = request_->response_info().ssl_info;
    if (expect_valid_ssl) {
      EXPECT_TRUE(ssl_info.is_valid());
      EXPECT_EQ(ssl_info.security_bits, 0x100);
      EXPECT_EQ(ssl_info.connection_status, 0x300039);
    } else {
      EXPECT_FALSE(ssl_info.is_valid());
    }
  }

  void CheckHeaders(const net::HttpResponseHeaders* headers) {
    size_t iter = 0;
    std::string name;
    std::string value;
    EXPECT_TRUE(headers->EnumerateHeaderLines(&iter, &name, &value));
    EXPECT_EQ("Foo", name);
    EXPECT_EQ("Bar", value);
    EXPECT_TRUE(headers->EnumerateHeaderLines(&iter, &name, &value));
    EXPECT_EQ("Pineapple", name);
    EXPECT_EQ("Pen", value);
    EXPECT_TRUE(headers->EnumerateHeaderLines(&iter, &name, &value));
    EXPECT_EQ("Set-Cookie", name);
    EXPECT_EQ("CookieCookieCookie", value);
    EXPECT_FALSE(headers->EnumerateHeaderLines(&iter, &name, &value));
  }

  void TestRequest(int expected_status_code,
                   const std::string& expected_status_text,
                   const std::string& expected_response,
                   bool expect_valid_ssl) {
    request_ = url_request_context_.CreateRequest(
        GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
        &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);

    request_->set_method("GET");
    request_->Start();
    base::RunLoop().RunUntilIdle();
    TestRequestResult(expected_status_code, expected_status_text,
                      expected_response, expect_valid_ssl);
  }

  bool HasWork() { return version_->HasWork(); }

  // Runs a request where the active worker starts a request in ACTIVATING state
  // and fails to reach ACTIVATED.
  void RunFailToActivateTest(ResourceType resource_type) {
    protocol_handler_->set_resource_type(resource_type);

    // Start a request with an activating worker.
    version_->SetStatus(ServiceWorkerVersion::ACTIVATING);
    request_ = url_request_context_.CreateRequest(
        GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
        &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
    request_->set_method("GET");
    request_->Start();

    // Proceed until the job starts waiting for the worker to activate.
    base::RunLoop().RunUntilIdle();

    // Simulate another worker kicking out the incumbent worker.  PostTask since
    // it might respond synchronously, and the TestDelegate would complain that
    // the message loop isn't being run.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&ServiceWorkerVersion::SetStatus, version_,
                              ServiceWorkerVersion::REDUNDANT));
    base::RunLoop().RunUntilIdle();
  }

  // Starts a navigation request with navigation preload enabled.
  void SetUpNavigationPreloadTest(ResourceType resource_type) {
    version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
    protocol_handler_->set_resource_type(resource_type);
    protocol_handler_->set_simulate_navigation_preload();
    request_ = url_request_context_.CreateRequest(
        GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
        &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
    ResourceRequestInfo::AllocateForTesting(
        request_.get(), resource_type, browser_context_->GetResourceContext(),
        -1, -1, -1, resource_type == RESOURCE_TYPE_MAIN_FRAME, false, true,
        true, PREVIEWS_OFF);

    request_->set_method("GET");
    request_->Start();
    base::RunLoop().RunUntilIdle();
  }

  // ServiceWorkerURLRequestJob::Delegate -------------------------------------
  void OnPrepareToRestart() override { times_prepare_to_restart_invoked_++; }

  ServiceWorkerVersion* GetServiceWorkerVersion(
      ServiceWorkerMetrics::URLRequestJobResult* result) override {
    if (!provider_host_) {
      *result = ServiceWorkerMetrics::REQUEST_JOB_ERROR_NO_PROVIDER_HOST;
      return nullptr;
    }
    if (!provider_host_->active_version()) {
      *result = ServiceWorkerMetrics::REQUEST_JOB_ERROR_NO_ACTIVE_VERSION;
      return nullptr;
    }
    return provider_host_->active_version();
  }

  bool RequestStillValid(
      ServiceWorkerMetrics::URLRequestJobResult* result) override {
    if (!provider_host_) {
      *result = ServiceWorkerMetrics::REQUEST_JOB_ERROR_NO_PROVIDER_HOST;
      return false;
    }
    return true;
  }

  void MainResourceLoadFailed() override {
    ASSERT_TRUE(provider_host_);
    // Detach the controller so subresource requests also skip the worker.
    provider_host_->NotifyControllerLost();
  }
  // ---------------------------------------------------------------------------

  TestBrowserThreadBundle thread_bundle_;

  std::unique_ptr<TestBrowserContext> browser_context_;
  std::unique_ptr<EmbeddedWorkerTestHelper> helper_;
  scoped_refptr<ServiceWorkerRegistration> registration_;
  scoped_refptr<ServiceWorkerVersion> version_;

  std::unique_ptr<net::URLRequestJobFactoryImpl> url_request_job_factory_;
  net::URLRequestContext url_request_context_;
  net::TestDelegate url_request_delegate_;
  std::unique_ptr<net::URLRequest> request_;

  int times_prepare_to_restart_invoked_ = 0;
  base::WeakPtr<ServiceWorkerProviderHost> provider_host_;
  ServiceWorkerRemoteProviderEndpoint remote_endpoint_;

  // Not owned.
  // The ProtocolHandler for https requests, which creates a
  // ServiceWorkerURLRequestJob.
  MockProtocolHandler* protocol_handler_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerURLRequestJobTest);
};

TEST_F(ServiceWorkerURLRequestJobTest, Simple) {
  SetUpWithHelper(base::MakeUnique<EmbeddedWorkerTestHelper>(base::FilePath()));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  TestRequest(200, "OK", std::string(), true /* expect_valid_ssl */);

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_TRUE(info->url_list_via_service_worker().empty());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
  EXPECT_FALSE(info->response_is_in_cache_storage());
  EXPECT_EQ(std::string(), info->response_cache_storage_cache_name());
}

// Helper for controlling when to start a worker and respond to a fetch event.
class DelayHelper : public EmbeddedWorkerTestHelper {
 public:
  DelayHelper(ServiceWorkerURLRequestJobTest* test)
      : EmbeddedWorkerTestHelper(base::FilePath()), test_(test) {}
  ~DelayHelper() override {}

  void CompleteNavigationPreload() {
    test_->handler()->job()->OnNavigationPreloadResponse();
  }

  void CompleteStartWorker() {
    EmbeddedWorkerTestHelper::OnStartWorker(
        embedded_worker_id_, service_worker_version_id_, scope_, script_url_,
        pause_after_download_, std::move(start_worker_request_),
        std::move(start_worker_instance_host_), std::move(provider_info_));
  }

  void Respond() {
    response_callback_->OnResponse(
        ServiceWorkerResponse(
            base::MakeUnique<std::vector<GURL>>(), 200, "OK",
            blink::mojom::FetchResponseType::kDefault,
            base::MakeUnique<ServiceWorkerHeaderMap>(), std::string(), 0,
            blink::kWebServiceWorkerResponseErrorUnknown, base::Time(),
            false /* response_is_in_cache_storage */,
            std::string() /* response_cache_storage_cache_name */,
            base::MakeUnique<
                ServiceWorkerHeaderList>() /* cors_exposed_header_names */),
        base::Time::Now());
    std::move(finish_callback_).Run(SERVICE_WORKER_OK, base::Time::Now());
  }

 protected:
  void OnStartWorker(
      int embedded_worker_id,
      int64_t service_worker_version_id,
      const GURL& scope,
      const GURL& script_url,
      bool pause_after_download,
      mojom::ServiceWorkerEventDispatcherRequest request,
      mojom::EmbeddedWorkerInstanceHostAssociatedPtrInfo instance_host,
      mojom::ServiceWorkerProviderInfoForStartWorkerPtr provider_info)
      override {
    embedded_worker_id_ = embedded_worker_id;
    service_worker_version_id_ = service_worker_version_id;
    scope_ = scope;
    script_url_ = script_url;
    pause_after_download_ = pause_after_download;
    start_worker_request_ = std::move(request);
    start_worker_instance_host_ = std::move(instance_host);
    provider_info_ = std::move(provider_info);
  }

  void OnFetchEvent(
      int embedded_worker_id,
      int fetch_event_id,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr preload_handle,
      mojom::ServiceWorkerFetchResponseCallbackPtr response_callback,
      FetchCallback finish_callback) override {
    embedded_worker_id_ = embedded_worker_id;
    fetch_event_id_ = fetch_event_id;
    response_callback_ = std::move(response_callback);
    finish_callback_ = std::move(finish_callback);
    preload_handle_ = std::move(preload_handle);
  }

 private:
  int64_t service_worker_version_id_;
  GURL scope_;
  GURL script_url_;
  bool pause_after_download_;
  mojom::ServiceWorkerEventDispatcherRequest start_worker_request_;
  mojom::EmbeddedWorkerInstanceHostAssociatedPtrInfo
      start_worker_instance_host_;
  mojom::ServiceWorkerProviderInfoForStartWorkerPtr provider_info_;
  int embedded_worker_id_ = 0;
  int fetch_event_id_ = 0;
  mojom::ServiceWorkerFetchResponseCallbackPtr response_callback_;
  mojom::FetchEventPreloadHandlePtr preload_handle_;
  FetchCallback finish_callback_;
  ServiceWorkerURLRequestJobTest* test_;
  DISALLOW_COPY_AND_ASSIGN(DelayHelper);
};

TEST_F(ServiceWorkerURLRequestJobTest,
       NavPreloadMetrics_WorkerAlreadyStarted_MainFrame) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  // Start the worker before the navigation.
  ServiceWorkerStatusCode status = SERVICE_WORKER_ERROR_MAX_VALUE;
  base::HistogramTester histogram_tester;
  version_->StartWorker(ServiceWorkerMetrics::EventType::UNKNOWN,
                        base::Bind(&SaveStatusCallback, &status));
  base::RunLoop().RunUntilIdle();
  helper->CompleteStartWorker();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(SERVICE_WORKER_OK, status);

  // Do the navigation.
  SetUpNavigationPreloadTest(RESOURCE_TYPE_MAIN_FRAME);
  helper->CompleteNavigationPreload();
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.ActivatedWorkerPreparationForMainFrame.Type_"
      "NavigationPreloadEnabled",
      static_cast<int>(ServiceWorkerMetrics::WorkerPreparationType::RUNNING),
      1);
  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame", false, 1);
  histogram_tester.ExpectTotalCount(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame_"
      "StartWorkerExistingProcess",
      0);
}

TEST_F(ServiceWorkerURLRequestJobTest,
       NavPreloadMetrics_WorkerFirst_MainFrame) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  base::HistogramTester histogram_tester;
  SetUpNavigationPreloadTest(RESOURCE_TYPE_MAIN_FRAME);

  // Worker finishes first.
  helper->CompleteStartWorker();
  helper->CompleteNavigationPreload();
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.ActivatedWorkerPreparationForMainFrame.Type_"
      "NavigationPreloadEnabled",
      static_cast<int>(ServiceWorkerMetrics::WorkerPreparationType::
                           START_IN_EXISTING_READY_PROCESS),
      1);
  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame", false, 1);
  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame_WorkerStartOccurred",
      false, 1);
}

TEST_F(ServiceWorkerURLRequestJobTest,
       NavPreloadMetrics_NavPreloadFirst_MainFrame) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  base::HistogramTester histogram_tester;
  SetUpNavigationPreloadTest(RESOURCE_TYPE_MAIN_FRAME);

  // Nav preload finishes first.
  helper->CompleteNavigationPreload();
  helper->CompleteStartWorker();
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.ActivatedWorkerPreparationForMainFrame.Type_"
      "NavigationPreloadEnabled",
      static_cast<int>(ServiceWorkerMetrics::WorkerPreparationType::
                           START_IN_EXISTING_READY_PROCESS),
      1);
  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame", true, 1);
  histogram_tester.ExpectUniqueSample(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame_WorkerStartOccurred",
      true, 1);
}

TEST_F(ServiceWorkerURLRequestJobTest, NavPreloadMetrics_WorkerFirst_SubFrame) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  base::HistogramTester histogram_tester;
  SetUpNavigationPreloadTest(RESOURCE_TYPE_SUB_FRAME);

  // Worker finishes first.
  helper->CompleteStartWorker();
  helper->CompleteNavigationPreload();
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  histogram_tester.ExpectTotalCount(
      "ServiceWorker.ActivatedWorkerPreparationForMainFrame.Type_"
      "NavigationPreloadEnabled",
      0);
  histogram_tester.ExpectTotalCount(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame", 0);
  histogram_tester.ExpectTotalCount(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame_"
      "StartWorkerExistingProcess",
      0);
}

TEST_F(ServiceWorkerURLRequestJobTest,
       NavPreloadMetrics_NavPreloadFirst_SubFrame) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  base::HistogramTester histogram_tester;
  SetUpNavigationPreloadTest(RESOURCE_TYPE_SUB_FRAME);

  // Nav preload finishes first.
  helper->CompleteNavigationPreload();
  helper->CompleteStartWorker();
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  histogram_tester.ExpectTotalCount(
      "ServiceWorker.ActivatedWorkerPreparationForMainFrame.Type_"
      "NavigationPreloadEnabled",
      0);
  histogram_tester.ExpectTotalCount(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame", 0);
  histogram_tester.ExpectTotalCount(
      "ServiceWorker.NavPreload.FinishedFirst_MainFrame_"
      "StartWorkerExistingProcess",
      0);
}

TEST_F(ServiceWorkerURLRequestJobTest, CustomTimeout) {
  SetUpWithHelper(base::MakeUnique<EmbeddedWorkerTestHelper>(base::FilePath()));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);

  // Set mock clock on version_ to check timeout behavior.
  {
    auto tick_clock = base::MakeUnique<base::SimpleTestTickClock>();
    tick_clock->SetNowTicks(base::TimeTicks::Now());
    version_->SetTickClockForTesting(std::move(tick_clock));
  }

  protocol_handler_->set_custom_timeout(base::TimeDelta::FromSeconds(5));
  TestRequest(200, "OK", std::string(), true /* expect_valid_ssl */);
  EXPECT_EQ(base::TimeDelta::FromSeconds(5), version_->remaining_timeout());
}

class ProviderDeleteHelper : public EmbeddedWorkerTestHelper {
 public:
  ProviderDeleteHelper() : EmbeddedWorkerTestHelper(base::FilePath()) {}
  ~ProviderDeleteHelper() override {}

 protected:
  void OnFetchEvent(
      int /* embedded_worker_id */,
      int /* fetch_event_id */,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr /* preload_handle */,
      mojom::ServiceWorkerFetchResponseCallbackPtr response_callback,
      FetchCallback finish_callback) override {
    context()->RemoveProviderHost(mock_render_process_id(), kProviderID);
    response_callback->OnResponse(
        ServiceWorkerResponse(
            base::MakeUnique<std::vector<GURL>>(), 200, "OK",
            blink::mojom::FetchResponseType::kDefault,
            base::MakeUnique<ServiceWorkerHeaderMap>(), std::string(), 0,
            blink::kWebServiceWorkerResponseErrorUnknown, base::Time(),
            false /* response_is_in_cache_storage */,
            std::string() /* response_cache_storage_cache_name */,
            base::MakeUnique<
                ServiceWorkerHeaderList>() /* cors_exposed_header_names */),
        base::Time::Now());
    std::move(finish_callback).Run(SERVICE_WORKER_OK, base::Time::Now());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ProviderDeleteHelper);
};

// Shouldn't crash if the ProviderHost is deleted prior to completion of the
// fetch event.
TEST_F(ServiceWorkerURLRequestJobTest, DeletedProviderHostOnFetchEvent) {
  SetUpWithHelper(base::MakeUnique<ProviderDeleteHelper>());

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  TestRequest(500, "Service Worker Response Error", std::string(),
              false /* expect_valid_ssl */);

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, DeletedProviderHostBeforeFetchEvent) {
  SetUpWithHelper(base::MakeUnique<EmbeddedWorkerTestHelper>(base::FilePath()));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);

  request_->set_method("GET");
  request_->Start();
  helper_->context()->RemoveProviderHost(helper_->mock_render_process_id(),
                                         kProviderID);
  base::RunLoop().RunUntilIdle();
  TestRequestResult(500, "Service Worker Response Error", std::string(),
                    false /* expect_valid_ssl */);

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_TRUE(info->service_worker_start_time().is_null());
  EXPECT_TRUE(info->service_worker_ready_time().is_null());
}

// Responds to fetch events with a blob.
class BlobResponder : public EmbeddedWorkerTestHelper {
 public:
  BlobResponder(const std::string& blob_uuid, uint64_t blob_size)
      : EmbeddedWorkerTestHelper(base::FilePath()),
        blob_uuid_(blob_uuid),
        blob_size_(blob_size) {}
  ~BlobResponder() override {}

 protected:
  void OnFetchEvent(
      int /* embedded_worker_id */,
      int /* fetch_event_id */,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr /* preload_handle */,
      mojom::ServiceWorkerFetchResponseCallbackPtr response_callback,
      FetchCallback finish_callback) override {
    response_callback->OnResponse(
        ServiceWorkerResponse(
            base::MakeUnique<std::vector<GURL>>(), 200, "OK",
            blink::mojom::FetchResponseType::kDefault, MakeHeaders(),
            blob_uuid_, blob_size_,
            blink::kWebServiceWorkerResponseErrorUnknown, base::Time(),
            false /* response_is_in_cache_storage */,
            std::string() /* response_cache_storage_cache_name */,
            base::MakeUnique<
                ServiceWorkerHeaderList>() /* cors_exposed_header_names */),
        base::Time::Now());
    std::move(finish_callback).Run(SERVICE_WORKER_OK, base::Time::Now());
  }

  std::string blob_uuid_;
  uint64_t blob_size_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BlobResponder);
};

TEST_F(ServiceWorkerURLRequestJobTest, BlobResponse) {
  ChromeBlobStorageContext* blob_storage_context =
      ChromeBlobStorageContext::GetFor(browser_context_.get());
  // Wait for blob_storage_context to finish initializing.
  base::RunLoop().RunUntilIdle();

  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);

  auto blob_data = base::MakeUnique<storage::BlobDataBuilder>("blob-id:myblob");
  for (int i = 0; i < 1024; ++i) {
    blob_data->AppendData(kTestData);
    expected_response += kTestData;
  }
  std::unique_ptr<storage::BlobDataHandle> blob_handle =
      blob_storage_context->context()->AddFinishedBlob(blob_data.get());
  SetUpWithHelper(base::MakeUnique<BlobResponder>(blob_handle->uuid(),
                                                  expected_response.size()));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  TestRequest(200, "OK", expected_response, true /* expect_valid_ssl */);
  CheckHeaders(request_->response_headers());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, NonExistentBlobUUIDResponse) {
  SetUpWithHelper(
      base::MakeUnique<BlobResponder>("blob-id:nothing-is-here", 0));
  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  TestRequest(500, "Service Worker Response Error", std::string(),
              true /* expect_valid_ssl */);

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

// Responds to fetch events with a stream.
class StreamResponder : public EmbeddedWorkerTestHelper {
 public:
  explicit StreamResponder(
      blink::mojom::ServiceWorkerStreamCallbackRequest callback_request,
      mojo::ScopedDataPipeConsumerHandle consumer_handle)
      : EmbeddedWorkerTestHelper(base::FilePath()) {
    EXPECT_TRUE(stream_handle_.is_null());
    stream_handle_ = blink::mojom::ServiceWorkerStreamHandle::New();
    stream_handle_->callback_request = std::move(callback_request);
    stream_handle_->stream = std::move(consumer_handle);
  }
  ~StreamResponder() override {}

 protected:
  void OnFetchEvent(
      int /* embedded_worker_id */,
      int /* fetch_event_id */,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr /* preload_handle */,
      mojom::ServiceWorkerFetchResponseCallbackPtr response_callback,
      FetchCallback finish_callback) override {
    ASSERT_FALSE(stream_handle_.is_null());
    response_callback->OnResponseStream(
        ServiceWorkerResponse(
            base::MakeUnique<std::vector<GURL>>(), 200, "OK",
            blink::mojom::FetchResponseType::kDefault, MakeHeaders(), "", 0,
            blink::kWebServiceWorkerResponseErrorUnknown, base::Time(),
            false /* response_is_in_cache_storage */,
            std::string() /* response_cache_storage_cache_name */,
            base::MakeUnique<
                ServiceWorkerHeaderList>() /* cors_exposed_header_names */),
        std::move(stream_handle_), base::Time::Now());
    std::move(finish_callback).Run(SERVICE_WORKER_OK, base::Time::Now());
  }

  blink::mojom::ServiceWorkerStreamHandlePtr stream_handle_;

 private:
  DISALLOW_COPY_AND_ASSIGN(StreamResponder);
};

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();

  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);
  for (int i = 0; i < 1024; ++i) {
    expected_response += kTestData;
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);
  }
  stream_callback->OnCompleted();
  data_pipe.producer_handle.reset();

  EXPECT_FALSE(HasWork());
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(HasWork());
  EXPECT_EQ(net::OK, url_request_delegate_.request_status());
  net::HttpResponseHeaders* headers = request_->response_headers();
  EXPECT_EQ(200, headers->response_code());
  EXPECT_EQ("OK", headers->GetStatusText());
  CheckHeaders(headers);
  EXPECT_EQ(expected_response, url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());

  request_.reset();
  EXPECT_FALSE(HasWork());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse_ConsecutiveRead) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);
  for (int i = 0; i < 1024; ++i) {
    expected_response += kTestData;
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(expected_response, url_request_delegate_.data_received());
  }
  stream_callback->OnCompleted();
  base::RunLoop().RunUntilIdle();

  // IO is still pending since |producer_handle| is not yet reset, but the data
  // should have been received by now.
  EXPECT_EQ(net::ERR_IO_PENDING, url_request_delegate_.request_status());
  EXPECT_EQ(200,
            request_->response_headers()->response_code());
  EXPECT_EQ("OK",
            request_->response_headers()->GetStatusText());
  EXPECT_EQ(expected_response, url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponseAndCancel) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  EXPECT_FALSE(HasWork());
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(HasWork());

  for (int i = 0; i < 512; ++i) {
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);
  }
  EXPECT_TRUE(data_pipe.producer_handle.is_valid());
  request_->Cancel();
  EXPECT_FALSE(HasWork());

  // Fail to write the data pipe because it's already canceled.
  uint32_t written_bytes = sizeof(kTestData) - 1;
  MojoResult result =
      mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                         &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION, result);

  stream_callback->OnAborted();

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(data_pipe.consumer_handle.is_valid());
  EXPECT_EQ(net::ERR_ABORTED, url_request_delegate_.request_status());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse_Abort) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();

  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);
  for (int i = 0; i < 1024; ++i) {
    expected_response += kTestData;
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);
  }
  stream_callback->OnAborted();
  data_pipe.producer_handle.reset();

  EXPECT_FALSE(HasWork());
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(HasWork());
  EXPECT_EQ(net::ERR_CONNECTION_RESET, url_request_delegate_.request_status());

  net::HttpResponseHeaders* headers = request_->response_headers();
  EXPECT_EQ(200, headers->response_code());
  EXPECT_EQ("OK", headers->GetStatusText());
  CheckHeaders(headers);
  EXPECT_EQ(expected_response, url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());

  request_.reset();
  EXPECT_FALSE(HasWork());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse_AbortBeforeData) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);

  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  base::RunLoop().RunUntilIdle();
  stream_callback->OnAborted();
  base::RunLoop().RunUntilIdle();

  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);
  for (int i = 0; i < 1024; ++i) {
    expected_response += kTestData;
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(expected_response, url_request_delegate_.data_received());
  }

  data_pipe.producer_handle.reset();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(net::ERR_CONNECTION_RESET, url_request_delegate_.request_status());
  EXPECT_EQ(200,
            request_->response_headers()->response_code());
  EXPECT_EQ("OK",
            request_->response_headers()->GetStatusText());
  EXPECT_EQ(expected_response, url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse_AbortAfterData) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);

  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  base::RunLoop().RunUntilIdle();
  data_pipe.producer_handle.reset();
  base::RunLoop().RunUntilIdle();
  stream_callback->OnAborted();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(net::ERR_CONNECTION_RESET, url_request_delegate_.request_status());
  EXPECT_EQ(200, request_->response_headers()->response_code());
  EXPECT_EQ("OK", request_->response_headers()->GetStatusText());
  EXPECT_EQ("", url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, StreamResponse_ConsecutiveReadAndAbort) {
  blink::mojom::ServiceWorkerStreamCallbackPtr stream_callback;
  mojo::DataPipe data_pipe;
  SetUpWithHelper(
      base::MakeUnique<StreamResponder>(mojo::MakeRequest(&stream_callback),
                                        std::move(data_pipe.consumer_handle)));

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  std::string expected_response;
  expected_response.reserve((sizeof(kTestData) - 1) * 1024);
  for (int i = 0; i < 512; ++i) {
    expected_response += kTestData;
    uint32_t written_bytes = sizeof(kTestData) - 1;
    MojoResult result =
        mojo::WriteDataRaw(data_pipe.producer_handle.get(), kTestData,
                           &written_bytes, MOJO_WRITE_DATA_FLAG_NONE);
    ASSERT_EQ(MOJO_RESULT_OK, result);
    EXPECT_EQ(sizeof(kTestData) - 1, written_bytes);

    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(expected_response, url_request_delegate_.data_received());
  }
  stream_callback->OnAborted();

  base::RunLoop().RunUntilIdle();

  // IO is still pending since |producer_handle| is not yet reset, but the data
  // should have been received by now.
  EXPECT_EQ(net::ERR_IO_PENDING, url_request_delegate_.request_status());
  EXPECT_EQ(200, request_->response_headers()->response_code());
  EXPECT_EQ("OK", request_->response_headers()->GetStatusText());
  EXPECT_EQ(expected_response, url_request_delegate_.data_received());

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

// Helper to simulate failing to dispatch a fetch event to a worker.
class FailFetchHelper : public EmbeddedWorkerTestHelper {
 public:
  FailFetchHelper() : EmbeddedWorkerTestHelper(base::FilePath()) {}
  ~FailFetchHelper() override {}

 protected:
  void OnFetchEvent(
      int embedded_worker_id,
      int /* fetch_event_id */,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr /* preload_handle */,
      mojom::ServiceWorkerFetchResponseCallbackPtr /* response_callback */,
      FetchCallback finish_callback) override {
    SimulateWorkerStopped(embedded_worker_id);
    std::move(finish_callback)
        .Run(SERVICE_WORKER_ERROR_ABORT, base::Time::Now());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(FailFetchHelper);
};

TEST_F(ServiceWorkerURLRequestJobTest, FailFetchDispatch) {
  SetUpWithHelper(base::MakeUnique<FailFetchHelper>());

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(net::OK, url_request_delegate_.request_status());
  // We should have fallen back to network.
  EXPECT_EQ(200, request_->GetResponseCode());
  EXPECT_EQ("PASS", url_request_delegate_.data_received());
  EXPECT_FALSE(HasWork());
  ServiceWorkerProviderHost* host = helper_->context()->GetProviderHost(
      helper_->mock_render_process_id(), kProviderID);
  ASSERT_TRUE(host);
  EXPECT_EQ(host->controlling_version(), nullptr);

  EXPECT_EQ(1, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
}

TEST_F(ServiceWorkerURLRequestJobTest, FailToActivate_MainResource) {
  SetUpWithHelper(base::MakeUnique<EmbeddedWorkerTestHelper>(base::FilePath()));
  RunFailToActivateTest(RESOURCE_TYPE_MAIN_FRAME);

  // The load should fail and we should have fallen back to network because
  // this is a main resource request.
  EXPECT_EQ(net::OK, url_request_delegate_.request_status());
  EXPECT_EQ(200, request_->GetResponseCode());
  EXPECT_EQ("PASS", url_request_delegate_.data_received());

  // The controller should be reset since the main resource request failed.
  ServiceWorkerProviderHost* host = helper_->context()->GetProviderHost(
      helper_->mock_render_process_id(), kProviderID);
  ASSERT_TRUE(host);
  EXPECT_EQ(host->controlling_version(), nullptr);
}

TEST_F(ServiceWorkerURLRequestJobTest, FailToActivate_Subresource) {
  SetUpWithHelper(base::MakeUnique<EmbeddedWorkerTestHelper>(base::FilePath()));
  RunFailToActivateTest(RESOURCE_TYPE_IMAGE);

  // The load should fail and we should not fall back to network because
  // this is a subresource request.
  EXPECT_EQ(net::OK, url_request_delegate_.request_status());
  EXPECT_EQ(500, request_->GetResponseCode());
  EXPECT_EQ("Service Worker Response Error",
            request_->response_headers()->GetStatusText());

  // The controller should still be set after a subresource request fails.
  ServiceWorkerProviderHost* host = helper_->context()->GetProviderHost(
      helper_->mock_render_process_id(), kProviderID);
  ASSERT_TRUE(host);
  EXPECT_EQ(host->controlling_version(), version_);
}

class EarlyResponseHelper : public EmbeddedWorkerTestHelper {
 public:
  EarlyResponseHelper() : EmbeddedWorkerTestHelper(base::FilePath()) {}
  ~EarlyResponseHelper() override {}

  void FinishWaitUntil() {
    std::move(finish_callback_).Run(SERVICE_WORKER_OK, base::Time::Now());
  }

 protected:
  void OnFetchEvent(
      int /* embedded_worker_id */,
      int /* fetch_event_id */,
      const ServiceWorkerFetchRequest& /* request */,
      mojom::FetchEventPreloadHandlePtr /* preload_handle */,
      mojom::ServiceWorkerFetchResponseCallbackPtr response_callback,
      FetchCallback finish_callback) override {
    finish_callback_ = std::move(finish_callback);
    response_callback->OnResponse(
        ServiceWorkerResponse(
            base::MakeUnique<std::vector<GURL>>(), 200, "OK",
            blink::mojom::FetchResponseType::kDefault,
            base::MakeUnique<ServiceWorkerHeaderMap>(), std::string(), 0,
            blink::kWebServiceWorkerResponseErrorUnknown, base::Time(),
            false /* response_is_in_cache_storage */,
            std::string() /* response_cache_storage_cache_name */,
            base::MakeUnique<
                ServiceWorkerHeaderList>() /* cors_exposed_header_names */),
        base::Time::Now());
  }

 private:
  FetchCallback finish_callback_;
  DISALLOW_COPY_AND_ASSIGN(EarlyResponseHelper);
};

// This simulates the case when a response is returned and the fetch event is
// still in flight.
TEST_F(ServiceWorkerURLRequestJobTest, EarlyResponse) {
  SetUpWithHelper(base::MakeUnique<EarlyResponseHelper>());
  EarlyResponseHelper* helper =
      static_cast<EarlyResponseHelper*>(helper_.get());

  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  TestRequest(200, "OK", std::string(), true /* expect_valid_ssl */);

  EXPECT_EQ(0, times_prepare_to_restart_invoked_);
  ServiceWorkerResponseInfo* info =
      ServiceWorkerResponseInfo::ForRequest(request_.get());
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->was_fetched_via_service_worker());
  EXPECT_FALSE(info->was_fallback_required());
  EXPECT_EQ(0u, info->url_list_via_service_worker().size());
  EXPECT_EQ(blink::mojom::FetchResponseType::kDefault,
            info->response_type_via_service_worker());
  EXPECT_FALSE(info->service_worker_start_time().is_null());
  EXPECT_FALSE(info->service_worker_ready_time().is_null());
  EXPECT_FALSE(info->response_is_in_cache_storage());
  EXPECT_EQ(std::string(), info->response_cache_storage_cache_name());

  EXPECT_TRUE(version_->HasWork());
  helper->FinishWaitUntil();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(version_->HasWork());
}

// Test cancelling the URLRequest while the fetch event is in flight.
TEST_F(ServiceWorkerURLRequestJobTest, CancelRequest) {
  SetUpWithHelper(base::MakeUnique<DelayHelper>(this));
  DelayHelper* helper = static_cast<DelayHelper*>(helper_.get());

  // Start the URL request. The job will be waiting for the
  // worker to respond to the fetch event.
  version_->SetStatus(ServiceWorkerVersion::ACTIVATED);
  request_ = url_request_context_.CreateRequest(
      GURL("https://example.com/foo.html"), net::DEFAULT_PRIORITY,
      &url_request_delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->set_method("GET");
  request_->Start();
  base::RunLoop().RunUntilIdle();
  helper->CompleteStartWorker();
  base::RunLoop().RunUntilIdle();

  // Cancel the URL request.
  request_->Cancel();
  base::RunLoop().RunUntilIdle();

  // Respond to the fetch event.
  EXPECT_TRUE(version_->HasWork());
  helper->Respond();
  base::RunLoop().RunUntilIdle();

  // The fetch event request should no longer be in-flight.
  EXPECT_FALSE(version_->HasWork());
}

// TODO(kinuko): Add more tests with different response data and also for
// FallbackToNetwork case.

}  // namespace content
