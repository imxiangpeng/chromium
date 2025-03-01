/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/exported/WebEmbeddedWorkerImpl.h"

#include <memory>
#include "bindings/core/v8/SourceLocation.h"
#include "core/dom/Document.h"
#include "core/dom/SecurityContext.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/exported/WebDocumentLoaderImpl.h"
#include "core/exported/WebViewImpl.h"
#include "core/frame/WebLocalFrameImpl.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/ThreadableLoadingContext.h"
#include "core/loader/WorkerFetchContext.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/GlobalScopeCreationParams.h"
#include "core/workers/ParentFrameTaskRunners.h"
#include "core/workers/WorkerBackingThreadStartupData.h"
#include "core/workers/WorkerContentSettingsClient.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerInspectorProxy.h"
#include "core/workers/WorkerScriptLoader.h"
#include "modules/indexeddb/IndexedDBClientImpl.h"
#include "modules/serviceworkers/ServiceWorkerContainerClient.h"
#include "modules/serviceworkers/ServiceWorkerGlobalScopeClient.h"
#include "modules/serviceworkers/ServiceWorkerGlobalScopeProxy.h"
#include "modules/serviceworkers/ServiceWorkerInstalledScriptsManager.h"
#include "modules/serviceworkers/ServiceWorkerThread.h"
#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/SharedBuffer.h"
#include "platform/WaitableEvent.h"
#include "platform/heap/Handle.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/SubstituteData.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/network/ContentSecurityPolicyResponseHeaders.h"
#include "platform/network/NetworkUtils.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebContentSettingsClient.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/WebWorkerFetchContext.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerNetworkProvider.h"
#include "public/platform/modules/serviceworker/WebServiceWorkerProvider.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebDevToolsAgent.h"
#include "public/web/WebSettings.h"
#include "public/web/WebView.h"
#include "public/web/modules/serviceworker/WebServiceWorkerContextClient.h"

namespace blink {

std::unique_ptr<WebEmbeddedWorker> WebEmbeddedWorker::Create(
    std::unique_ptr<WebServiceWorkerContextClient> client,
    std::unique_ptr<WebServiceWorkerInstalledScriptsManager>
        installed_scripts_manager,
    std::unique_ptr<WebContentSettingsClient> content_settings_client) {
  return WTF::MakeUnique<WebEmbeddedWorkerImpl>(
      std::move(client), std::move(installed_scripts_manager),
      std::move(content_settings_client));
}

WebEmbeddedWorkerImpl::WebEmbeddedWorkerImpl(
    std::unique_ptr<WebServiceWorkerContextClient> client,
    std::unique_ptr<WebServiceWorkerInstalledScriptsManager>
        installed_scripts_manager,
    std::unique_ptr<WebContentSettingsClient> content_settings_client)
    : worker_context_client_(std::move(client)),
      content_settings_client_(std::move(content_settings_client)),
      worker_inspector_proxy_(WorkerInspectorProxy::Create()),
      web_view_(nullptr),
      main_frame_(nullptr),
      loading_shadow_page_(false),
      asked_to_terminate_(false),
      pause_after_download_state_(kDontPauseAfterDownload),
      waiting_for_debugger_state_(kNotWaitingForDebugger) {
  if (RuntimeEnabledFeatures::ServiceWorkerScriptStreamingEnabled() &&
      installed_scripts_manager) {
    installed_scripts_manager_ =
        WTF::MakeUnique<ServiceWorkerInstalledScriptsManager>(
            std::move(installed_scripts_manager));
  }
  service_manager::mojom::InterfaceProviderPtr provider;
  mojo::MakeRequest(&provider);
  interface_provider_.Bind(std::move(provider));
}

WebEmbeddedWorkerImpl::~WebEmbeddedWorkerImpl() {
  // TerminateWorkerContext() must be called before the destructor.
  DCHECK(asked_to_terminate_);

  DCHECK(web_view_);

  // Detach the client before closing the view to avoid getting called back.
  main_frame_->SetClient(0);

  if (worker_global_scope_proxy_) {
    worker_global_scope_proxy_->Detach();
    worker_global_scope_proxy_.Clear();
  }

  web_view_->Close();
  main_frame_->Close();
}

void WebEmbeddedWorkerImpl::StartWorkerContext(
    const WebEmbeddedWorkerStartData& data) {
  DCHECK(!asked_to_terminate_);
  DCHECK(!main_script_loader_);
  DCHECK_EQ(pause_after_download_state_, kDontPauseAfterDownload);
  worker_start_data_ = data;

  // TODO(mkwst): This really needs to be piped through from the requesting
  // document, like we're doing for SharedWorkers. That turns out to be
  // incredibly convoluted, and since ServiceWorkers are locked to the same
  // origin as the page which requested them, the only time it would come
  // into play is a DNS poisoning attack after the page load. It's something
  // we should fix, but we're taking this shortcut for the prototype.
  //
  // https://crbug.com/590714
  KURL script_url = worker_start_data_.script_url;
  worker_start_data_.address_space = kWebAddressSpacePublic;
  if (NetworkUtils::IsReservedIPAddress(script_url.Host()))
    worker_start_data_.address_space = kWebAddressSpacePrivate;
  if (SecurityOrigin::Create(script_url)->IsLocalhost())
    worker_start_data_.address_space = kWebAddressSpaceLocal;

  if (data.pause_after_download_mode ==
      WebEmbeddedWorkerStartData::kPauseAfterDownload)
    pause_after_download_state_ = kDoPauseAfterDownload;
  PrepareShadowPageForLoader();
}

void WebEmbeddedWorkerImpl::TerminateWorkerContext() {
  if (asked_to_terminate_)
    return;
  asked_to_terminate_ = true;
  if (loading_shadow_page_) {
    // This deletes 'this'.
    worker_context_client_->WorkerContextFailedToStart();
    return;
  }
  if (main_script_loader_) {
    main_script_loader_->Cancel();
    main_script_loader_.Clear();
    // This deletes 'this'.
    worker_context_client_->WorkerContextFailedToStart();
    return;
  }
  if (!worker_thread_) {
    // The worker thread has not been created yet if the worker is asked to
    // terminate during waiting for debugger or paused after download.
    DCHECK(worker_start_data_.wait_for_debugger_mode ==
               WebEmbeddedWorkerStartData::kWaitForDebugger ||
           pause_after_download_state_ == kIsPausedAfterDownload);
    // This deletes 'this'.
    worker_context_client_->WorkerContextFailedToStart();
    return;
  }
  worker_thread_->Terminate();
  worker_inspector_proxy_->WorkerThreadTerminated();
}

void WebEmbeddedWorkerImpl::ResumeAfterDownload() {
  DCHECK(!asked_to_terminate_);
  DCHECK_EQ(pause_after_download_state_, kIsPausedAfterDownload);

  pause_after_download_state_ = kDontPauseAfterDownload;
  StartWorkerThread();
}

void WebEmbeddedWorkerImpl::AttachDevTools(const WebString& host_id,
                                           int session_id) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Attach(host_id, session_id);
}

void WebEmbeddedWorkerImpl::ReattachDevTools(const WebString& host_id,
                                             int session_id,
                                             const WebString& saved_state) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Reattach(host_id, session_id, saved_state);
  ResumeStartup();
}

void WebEmbeddedWorkerImpl::DetachDevTools(int session_id) {
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent)
    devtools_agent->Detach(session_id);
}

void WebEmbeddedWorkerImpl::DispatchDevToolsMessage(int session_id,
                                                    int call_id,
                                                    const WebString& method,
                                                    const WebString& message) {
  if (asked_to_terminate_)
    return;
  WebDevToolsAgent* devtools_agent = main_frame_->DevToolsAgent();
  if (devtools_agent) {
    devtools_agent->DispatchOnInspectorBackend(session_id, call_id, method,
                                               message);
  }
}

void WebEmbeddedWorkerImpl::AddMessageToConsole(
    const WebConsoleMessage& message) {
  MessageLevel web_core_message_level;
  switch (message.level) {
    case WebConsoleMessage::kLevelVerbose:
      web_core_message_level = kVerboseMessageLevel;
      break;
    case WebConsoleMessage::kLevelInfo:
      web_core_message_level = kInfoMessageLevel;
      break;
    case WebConsoleMessage::kLevelWarning:
      web_core_message_level = kWarningMessageLevel;
      break;
    case WebConsoleMessage::kLevelError:
      web_core_message_level = kErrorMessageLevel;
      break;
    default:
      NOTREACHED();
      return;
  }

  main_frame_->GetFrame()->GetDocument()->AddConsoleMessage(
      ConsoleMessage::Create(
          kOtherMessageSource, web_core_message_level, message.text,
          SourceLocation::Create(message.url, message.line_number,
                                 message.column_number, nullptr)));
}

void WebEmbeddedWorkerImpl::PostMessageToPageInspector(int session_id,
                                                       const String& message) {
  worker_inspector_proxy_->DispatchMessageFromWorker(session_id, message);
}

void WebEmbeddedWorkerImpl::SetContentSecurityPolicyAndReferrerPolicy(
    ContentSecurityPolicyResponseHeaders csp_headers,
    String referrer_policy,
    WaitableEvent* event) {
  Document* document = main_frame_->GetFrame()->GetDocument();
  ContentSecurityPolicy* content_security_policy =
      ContentSecurityPolicy::Create();
  content_security_policy->SetOverrideURLForSelf(document->Url());
  content_security_policy->DidReceiveHeaders(csp_headers);
  document->InitContentSecurityPolicy(content_security_policy);
  if (!referrer_policy.IsNull())
    document->ParseAndSetReferrerPolicy(referrer_policy);
  event->Signal();
}

void WebEmbeddedWorkerImpl::PrepareShadowPageForLoader() {
  // Create 'shadow page', which is never displayed and is used mainly to
  // provide a context for loading on the main thread.
  //
  // FIXME: This does mostly same as WebSharedWorkerImpl::initializeLoader.
  // This code, and probably most of the code in this class should be shared
  // with SharedWorker.
  DCHECK(!web_view_);
  web_view_ = WebViewImpl::Create(nullptr, kWebPageVisibilityStateVisible);
  WebSettings* settings = web_view_->GetSettings();
  // FIXME: http://crbug.com/363843. This needs to find a better way to
  // not create graphics layers.
  settings->SetAcceleratedCompositingEnabled(false);
  // Currently we block all mixed-content requests from a ServiceWorker.
  // FIXME: When we support FetchEvent.default(), we should relax this
  // restriction.
  settings->SetStrictMixedContentChecking(true);
  settings->SetAllowRunningOfInsecureContent(false);
  settings->SetDataSaverEnabled(worker_start_data_.data_saver_enabled);
  main_frame_ = WebLocalFrameImpl::CreateMainFrame(
      web_view_, this, nullptr, nullptr, g_empty_atom, WebSandboxFlags::kNone);
  main_frame_->SetDevToolsAgentClient(this);

  // If we were asked to wait for debugger then it is the good time to do that.
  worker_context_client_->WorkerReadyForInspection();
  if (worker_start_data_.wait_for_debugger_mode ==
      WebEmbeddedWorkerStartData::kWaitForDebugger) {
    waiting_for_debugger_state_ = kWaitingForDebugger;
    return;
  }

  LoadShadowPage();
}

void WebEmbeddedWorkerImpl::LoadShadowPage() {
  // Construct substitute data source for the 'shadow page'. We only need it
  // to have same origin as the worker so the loading checks work correctly.
  CString content("");
  RefPtr<SharedBuffer> buffer(
      SharedBuffer::Create(content.data(), content.length()));
  loading_shadow_page_ = true;
  main_frame_->GetFrame()->Loader().Load(
      FrameLoadRequest(0, ResourceRequest(worker_start_data_.script_url),
                       SubstituteData(buffer)));
}

void WebEmbeddedWorkerImpl::FrameDetached(WebLocalFrame* frame,
                                          DetachType type) {
  DCHECK(type == DetachType::kRemove && frame->Parent());
  DCHECK(frame->FrameWidget());
  frame->Close();
}

void WebEmbeddedWorkerImpl::DidFinishDocumentLoad() {
  DCHECK(!main_script_loader_);
  DCHECK(main_frame_);
  DCHECK(worker_context_client_);
  DCHECK(loading_shadow_page_);
  DCHECK(!asked_to_terminate_);
  loading_shadow_page_ = false;
  main_frame_->GetDocumentLoader()->SetServiceWorkerNetworkProvider(
      worker_context_client_->CreateServiceWorkerNetworkProvider());

  // Kickstart the worker before loading the script when the script has been
  // installed.
  if (RuntimeEnabledFeatures::ServiceWorkerScriptStreamingEnabled() &&
      installed_scripts_manager_ &&
      installed_scripts_manager_->IsScriptInstalled(
          worker_start_data_.script_url)) {
    DCHECK_EQ(pause_after_download_state_, kDontPauseAfterDownload);
    StartWorkerThread();
    return;
  }

  main_script_loader_ = WorkerScriptLoader::Create();
  main_script_loader_->LoadAsynchronously(
      *main_frame_->GetFrame()->GetDocument(), worker_start_data_.script_url,
      WebURLRequest::kRequestContextServiceWorker,
      WebURLRequest::kFetchRequestModeSameOrigin,
      WebURLRequest::kFetchCredentialsModeSameOrigin,
      worker_start_data_.address_space, WTF::Closure(),
      Bind(&WebEmbeddedWorkerImpl::OnScriptLoaderFinished,
           WTF::Unretained(this)));
  // Do nothing here since onScriptLoaderFinished() might have been already
  // invoked and |this| might have been deleted at this point.
}

service_manager::InterfaceProvider*
WebEmbeddedWorkerImpl::GetInterfaceProvider() {
  return &interface_provider_;
}

void WebEmbeddedWorkerImpl::SendProtocolMessage(int session_id,
                                                int call_id,
                                                const WebString& message,
                                                const WebString& state) {
  worker_context_client_->SendDevToolsMessage(session_id, call_id, message,
                                              state);
}

void WebEmbeddedWorkerImpl::ResumeStartup() {
  bool was_waiting = (waiting_for_debugger_state_ == kWaitingForDebugger);
  waiting_for_debugger_state_ = kNotWaitingForDebugger;
  if (was_waiting)
    LoadShadowPage();
}

WebDevToolsAgentClient::WebKitClientMessageLoop*
WebEmbeddedWorkerImpl::CreateClientMessageLoop() {
  return worker_context_client_->CreateDevToolsMessageLoop();
}

void WebEmbeddedWorkerImpl::OnScriptLoaderFinished() {
  DCHECK(main_script_loader_);
  if (asked_to_terminate_)
    return;

  // The browser is expected to associate a registration and then load the
  // script. If there's no associated registration, the browser could not
  // successfully handle the SetHostedVersionID IPC, and the script load came
  // through the normal network stack rather than through service worker
  // loading code.
  if (!worker_context_client_->HasAssociatedRegistration() ||
      main_script_loader_->Failed()) {
    TerminateWorkerContext();
    return;
  }
  worker_context_client_->WorkerScriptLoaded();

  DEFINE_STATIC_LOCAL(CustomCountHistogram, script_size_histogram,
                      ("ServiceWorker.ScriptSize", 1000, 5000000, 50));
  script_size_histogram.Count(main_script_loader_->SourceText().length());
  if (main_script_loader_->CachedMetadata()) {
    DEFINE_STATIC_LOCAL(
        CustomCountHistogram, script_cached_metadata_size_histogram,
        ("ServiceWorker.ScriptCachedMetadataSize", 1000, 50000000, 50));
    script_cached_metadata_size_histogram.Count(
        main_script_loader_->CachedMetadata()->size());
  }

  if (pause_after_download_state_ == kDoPauseAfterDownload) {
    pause_after_download_state_ = kIsPausedAfterDownload;
    return;
  }
  StartWorkerThread();
}

void WebEmbeddedWorkerImpl::StartWorkerThread() {
  DCHECK_EQ(pause_after_download_state_, kDontPauseAfterDownload);
  DCHECK(!asked_to_terminate_);

  Document* document = main_frame_->GetFrame()->GetDocument();

  // FIXME: this document's origin is pristine and without any extra privileges.
  // (crbug.com/254993)
  SecurityOrigin* starter_origin = document->GetSecurityOrigin();

  WorkerClients* worker_clients = WorkerClients::Create();
  ProvideIndexedDBClientToWorker(worker_clients,
                                 IndexedDBClientImpl::Create(*worker_clients));

  ProvideContentSettingsClientToWorker(worker_clients,
                                       std::move(content_settings_client_));
  ProvideServiceWorkerGlobalScopeClientToWorker(
      worker_clients,
      new ServiceWorkerGlobalScopeClient(*worker_context_client_));
  ProvideServiceWorkerContainerClientToWorker(
      worker_clients, worker_context_client_->CreateServiceWorkerProvider());

  if (RuntimeEnabledFeatures::OffMainThreadFetchEnabled()) {
    std::unique_ptr<WebWorkerFetchContext> web_worker_fetch_context =
        worker_context_client_->CreateServiceWorkerFetchContext();
    DCHECK(web_worker_fetch_context);
    web_worker_fetch_context->SetDataSaverEnabled(
        document->GetFrame()->GetSettings()->GetDataSaverEnabled());
    ProvideWorkerFetchContextToWorker(worker_clients,
                                      std::move(web_worker_fetch_context));
  }

  WorkerThreadStartMode start_mode =
      worker_inspector_proxy_->WorkerStartMode(document);
  std::unique_ptr<WorkerSettings> worker_settings =
      WTF::WrapUnique(new WorkerSettings(document->GetSettings()));

  std::unique_ptr<GlobalScopeCreationParams> global_scope_creation_params;
  // |main_script_loader_| isn't created if the InstalledScriptsManager had the
  // script.
  if (main_script_loader_) {
    // We need to set the CSP to both the shadow page's document and the
    // ServiceWorkerGlobalScope.
    document->InitContentSecurityPolicy(
        main_script_loader_->ReleaseContentSecurityPolicy());
    if (!main_script_loader_->GetReferrerPolicy().IsNull()) {
      document->ParseAndSetReferrerPolicy(
          main_script_loader_->GetReferrerPolicy());
    }
    global_scope_creation_params = WTF::MakeUnique<GlobalScopeCreationParams>(
        worker_start_data_.script_url, worker_start_data_.user_agent,
        main_script_loader_->SourceText(),
        main_script_loader_->ReleaseCachedMetadata(), start_mode,
        document->GetContentSecurityPolicy()->Headers().get(),
        main_script_loader_->GetReferrerPolicy(), starter_origin,
        worker_clients, main_script_loader_->ResponseAddressSpace(),
        main_script_loader_->OriginTrialTokens(), std::move(worker_settings),
        static_cast<V8CacheOptions>(worker_start_data_.v8_cache_options));
    main_script_loader_.Clear();
  } else {
    // ContentSecurityPolicy and ReferrerPolicy are applied to |document| at
    // SetContentSecurityPolicyAndReferrerPolicy() before evaluating the main
    // script.
    global_scope_creation_params = WTF::MakeUnique<GlobalScopeCreationParams>(
        worker_start_data_.script_url, worker_start_data_.user_agent,
        "" /* SourceText */, nullptr /* CachedMetadata */, start_mode,
        nullptr /* ContentSecurityPolicy */, "" /* ReferrerPolicy */,
        starter_origin, worker_clients, worker_start_data_.address_space,
        nullptr /* OriginTrialTokens */, std::move(worker_settings),
        static_cast<V8CacheOptions>(worker_start_data_.v8_cache_options));
  }

  worker_global_scope_proxy_ =
      ServiceWorkerGlobalScopeProxy::Create(*this, *worker_context_client_);
  worker_thread_ = WTF::MakeUnique<ServiceWorkerThread>(
      ThreadableLoadingContext::Create(*document), *worker_global_scope_proxy_,
      std::move(installed_scripts_manager_));

  // We have a dummy document here for loading but it doesn't really represent
  // the document/frame of associated document(s) for this worker. Here we
  // populate the task runners with default task runners of the main thread.
  worker_thread_->Start(std::move(global_scope_creation_params),
                        WorkerBackingThreadStartupData::CreateDefault(),
                        ParentFrameTaskRunners::Create());

  worker_inspector_proxy_->WorkerThreadCreated(document, worker_thread_.get(),
                                               worker_start_data_.script_url);
}

}  // namespace blink
