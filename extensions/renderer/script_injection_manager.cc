// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/script_injection_manager.h"

#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_thread.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/feature_switch.h"
#include "extensions/renderer/async_scripts_run_info.h"
#include "extensions/renderer/extension_frame_helper.h"
#include "extensions/renderer/extension_injection_host.h"
#include "extensions/renderer/programmatic_script_injector.h"
#include "extensions/renderer/renderer_extension_registry.h"
#include "extensions/renderer/script_injection.h"
#include "extensions/renderer/scripts_run_info.h"
#include "extensions/renderer/web_ui_injection_host.h"
#include "ipc/ipc_message_macros.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "url/gurl.h"

namespace extensions {

namespace {

// The length of time to wait after the DOM is complete to try and run user
// scripts.
const int kScriptIdleTimeoutInMs = 200;

// Returns the RunLocation that follows |run_location|.
UserScript::RunLocation NextRunLocation(UserScript::RunLocation run_location) {
  switch (run_location) {
    case UserScript::DOCUMENT_START:
      return UserScript::DOCUMENT_END;
    case UserScript::DOCUMENT_END:
      return UserScript::DOCUMENT_IDLE;
    case UserScript::DOCUMENT_IDLE:
      return UserScript::RUN_LOCATION_LAST;
    case UserScript::UNDEFINED:
    case UserScript::RUN_DEFERRED:
    case UserScript::BROWSER_DRIVEN:
    case UserScript::RUN_LOCATION_LAST:
      break;
  }
  NOTREACHED();
  return UserScript::RUN_LOCATION_LAST;
}

}  // namespace

class ScriptInjectionManager::RFOHelper : public content::RenderFrameObserver {
 public:
  RFOHelper(content::RenderFrame* render_frame,
            ScriptInjectionManager* manager);
  ~RFOHelper() override;

 private:
  // RenderFrameObserver implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void DidCreateNewDocument() override;
  void DidCreateDocumentElement() override;
  void DidFailProvisionalLoad(const blink::WebURLError& error) override;
  void DidFinishDocumentLoad() override;
  void DidFinishLoad() override;
  void FrameDetached() override;
  void OnDestruct() override;
  void OnStop() override;

  virtual void OnExecuteCode(const ExtensionMsg_ExecuteCode_Params& params);
  virtual void OnExecuteDeclarativeScript(int tab_id,
                                          const ExtensionId& extension_id,
                                          int script_id,
                                          const GURL& url);
  virtual void OnPermitScriptInjection(int64_t request_id);

  // Tells the ScriptInjectionManager to run tasks associated with
  // document_idle.
  void RunIdle();

  void StartInjectScripts(UserScript::RunLocation run_location);

  // Indicate that the frame is no longer valid because it is starting
  // a new load or closing.
  void InvalidateAndResetFrame();

  // The owning ScriptInjectionManager.
  ScriptInjectionManager* manager_;

  bool should_run_idle_;

  base::WeakPtrFactory<RFOHelper> weak_factory_;
};

ScriptInjectionManager::RFOHelper::RFOHelper(content::RenderFrame* render_frame,
                                             ScriptInjectionManager* manager)
    : content::RenderFrameObserver(render_frame),
      manager_(manager),
      should_run_idle_(true),
      weak_factory_(this) {
}

ScriptInjectionManager::RFOHelper::~RFOHelper() {
}

bool ScriptInjectionManager::RFOHelper::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ScriptInjectionManager::RFOHelper, message)
    IPC_MESSAGE_HANDLER(ExtensionMsg_ExecuteCode, OnExecuteCode)
    IPC_MESSAGE_HANDLER(ExtensionMsg_PermitScriptInjection,
                        OnPermitScriptInjection)
    IPC_MESSAGE_HANDLER(ExtensionMsg_ExecuteDeclarativeScript,
                        OnExecuteDeclarativeScript)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ScriptInjectionManager::RFOHelper::DidCreateNewDocument() {
  // A new document is going to be shown, so invalidate the old document state.
  // Check that the frame's state is known before invalidating the frame,
  // because it is possible that a script injection was scheduled before the
  // page was loaded, e.g. by navigating to a javascript: URL before the page
  // has loaded.
  if (manager_->frame_statuses_.count(render_frame()) != 0)
    InvalidateAndResetFrame();
}

void ScriptInjectionManager::RFOHelper::DidCreateDocumentElement() {
  ExtensionFrameHelper::Get(render_frame())
      ->ScheduleAtDocumentStart(
          base::Bind(&ScriptInjectionManager::RFOHelper::StartInjectScripts,
                     weak_factory_.GetWeakPtr(), UserScript::DOCUMENT_START));
}

void ScriptInjectionManager::RFOHelper::DidFailProvisionalLoad(
    const blink::WebURLError& error) {
  FrameStatusMap::iterator it = manager_->frame_statuses_.find(render_frame());
  if (it != manager_->frame_statuses_.end() &&
      it->second == UserScript::DOCUMENT_START) {
    // Since the provisional load failed, the frame stays at its previous loaded
    // state and origin (or the parent's origin for new/about:blank frames).
    // Reset the frame to DOCUMENT_IDLE in order to reflect that the frame is
    // done loading, and avoid any deadlock in the system.
    //
    // We skip injection of DOCUMENT_END and DOCUMENT_IDLE scripts, because the
    // injections closely follow the DOMContentLoaded (and onload) events, which
    // are not triggered after a failed provisional load.
    // This assumption is verified in the checkDOMContentLoadedEvent subtest of
    // ExecuteScriptApiTest.FrameWithHttp204 (browser_tests).
    InvalidateAndResetFrame();
    should_run_idle_ = false;
    manager_->frame_statuses_[render_frame()] = UserScript::DOCUMENT_IDLE;
  }
}

void ScriptInjectionManager::RFOHelper::DidFinishDocumentLoad() {
  DCHECK(content::RenderThread::Get());
  ExtensionFrameHelper::Get(render_frame())
      ->ScheduleAtDocumentEnd(
          base::Bind(&ScriptInjectionManager::RFOHelper::StartInjectScripts,
                     weak_factory_.GetWeakPtr(), UserScript::DOCUMENT_END));

  // We try to run idle in two places: here and DidFinishLoad.
  // DidFinishDocumentLoad() corresponds to completing the document's load,
  // whereas DidFinishLoad corresponds to completing the document and all
  // subresources' load. We don't want to hold up script injection for a
  // particularly slow subresource, so we set a delayed task from here - but if
  // we finish everything before that point (i.e., DidFinishLoad() is
  // triggered), then there's no reason to keep waiting.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ScriptInjectionManager::RFOHelper::RunIdle,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kScriptIdleTimeoutInMs));

  if (FeatureSwitch::yield_between_content_script_runs()->IsEnabled()) {
    ExtensionFrameHelper::Get(render_frame())
        ->ScheduleAtDocumentIdle(
            base::Bind(&ScriptInjectionManager::RFOHelper::RunIdle,
                       weak_factory_.GetWeakPtr()));
  }
}

void ScriptInjectionManager::RFOHelper::DidFinishLoad() {
  DCHECK(content::RenderThread::Get());
  if (!FeatureSwitch::yield_between_content_script_runs()->IsEnabled()) {
    // Ensure that we don't block any UI progress by running scripts.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&ScriptInjectionManager::RFOHelper::RunIdle,
                              weak_factory_.GetWeakPtr()));
  }
}

void ScriptInjectionManager::RFOHelper::FrameDetached() {
  // The frame is closing - invalidate.
  InvalidateAndResetFrame();
}

void ScriptInjectionManager::RFOHelper::OnDestruct() {
  manager_->RemoveObserver(this);
}

void ScriptInjectionManager::RFOHelper::OnStop() {
  // With PlzNavigate, we won't get a provisional load failed notification
  // for 204/205/downloads since these don't notify the renderer. However the
  // browser does fire the OnStop IPC. So use that signal instead to avoid
  // keeping the frame in a START state indefinitely which leads to deadlocks.
  if (content::IsBrowserSideNavigationEnabled())
    DidFailProvisionalLoad(blink::WebURLError());
}

void ScriptInjectionManager::RFOHelper::OnExecuteCode(
    const ExtensionMsg_ExecuteCode_Params& params) {
  manager_->HandleExecuteCode(params, render_frame());
}

void ScriptInjectionManager::RFOHelper::OnExecuteDeclarativeScript(
    int tab_id,
    const ExtensionId& extension_id,
    int script_id,
    const GURL& url) {
  // TODO(markdittmer): URL-checking isn't the best security measure.
  // Begin script injection workflow only if the current URL is identical to
  // the one that matched declarative conditions in the browser.
  if (GURL(render_frame()->GetWebFrame()->GetDocument().Url()) == url) {
    manager_->HandleExecuteDeclarativeScript(render_frame(),
                                             tab_id,
                                             extension_id,
                                             script_id,
                                             url);
  }
}

void ScriptInjectionManager::RFOHelper::OnPermitScriptInjection(
    int64_t request_id) {
  manager_->HandlePermitScriptInjection(request_id);
}

void ScriptInjectionManager::RFOHelper::RunIdle() {
  // Only notify the manager if the frame hasn't either been removed or already
  // had idle run since the task to RunIdle() was posted.
  if (should_run_idle_) {
    should_run_idle_ = false;
    manager_->StartInjectScripts(render_frame(), UserScript::DOCUMENT_IDLE);
  }
}

void ScriptInjectionManager::RFOHelper::StartInjectScripts(
    UserScript::RunLocation run_location) {
  manager_->StartInjectScripts(render_frame(), run_location);
}

void ScriptInjectionManager::RFOHelper::InvalidateAndResetFrame() {
  // Invalidate any pending idle injections, and reset the frame inject on idle.
  weak_factory_.InvalidateWeakPtrs();
  // We reset to inject on idle, because the frame can be reused (in the case of
  // navigation).
  should_run_idle_ = true;
  manager_->InvalidateForFrame(render_frame());
}

ScriptInjectionManager::ScriptInjectionManager(
    UserScriptSetManager* user_script_set_manager)
    : user_script_set_manager_(user_script_set_manager),
      user_script_set_manager_observer_(this) {
  user_script_set_manager_observer_.Add(user_script_set_manager_);
}

ScriptInjectionManager::~ScriptInjectionManager() {
  for (const auto& injection : pending_injections_)
    injection->invalidate_render_frame();
  for (const auto& injection : running_injections_)
    injection->invalidate_render_frame();
}

void ScriptInjectionManager::OnRenderFrameCreated(
    content::RenderFrame* render_frame) {
  rfo_helpers_.push_back(base::MakeUnique<RFOHelper>(render_frame, this));
}

void ScriptInjectionManager::OnExtensionUnloaded(
    const std::string& extension_id) {
  for (auto iter = pending_injections_.begin();
      iter != pending_injections_.end();) {
    if ((*iter)->host_id().id() == extension_id) {
      (*iter)->OnHostRemoved();
      iter = pending_injections_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void ScriptInjectionManager::OnInjectionFinished(
    ScriptInjection* injection) {
  auto iter =
      std::find_if(running_injections_.begin(), running_injections_.end(),
                   [injection](const std::unique_ptr<ScriptInjection>& mode) {
                     return injection == mode.get();
                   });
  if (iter != running_injections_.end())
    running_injections_.erase(iter);
}

void ScriptInjectionManager::OnUserScriptsUpdated(
    const std::set<HostID>& changed_hosts) {
  for (auto iter = pending_injections_.begin();
       iter != pending_injections_.end();) {
    if (changed_hosts.count((*iter)->host_id()) > 0)
      iter = pending_injections_.erase(iter);
    else
      ++iter;
  }
}

void ScriptInjectionManager::RemoveObserver(RFOHelper* helper) {
  for (auto iter = rfo_helpers_.begin(); iter != rfo_helpers_.end(); ++iter) {
    if (iter->get() == helper) {
      rfo_helpers_.erase(iter);
      break;
    }
  }
}

void ScriptInjectionManager::InvalidateForFrame(content::RenderFrame* frame) {
  // If the frame invalidated is the frame being injected into, we need to
  // note it.
  active_injection_frames_.erase(frame);

  for (auto iter = pending_injections_.begin();
       iter != pending_injections_.end();) {
    if ((*iter)->render_frame() == frame)
      iter = pending_injections_.erase(iter);
    else
      ++iter;
  }

  frame_statuses_.erase(frame);
}

void ScriptInjectionManager::StartInjectScripts(
    content::RenderFrame* frame,
    UserScript::RunLocation run_location) {
  FrameStatusMap::iterator iter = frame_statuses_.find(frame);
  // We also don't execute if we detect that the run location is somehow out of
  // order. This can happen if:
  // - The first run location reported for the frame isn't DOCUMENT_START, or
  // - The run location reported doesn't immediately follow the previous
  //   reported run location.
  // We don't want to run because extensions may have requirements that scripts
  // running in an earlier run location have run by the time a later script
  // runs. Better to just not run.
  // Note that we check run_location > NextRunLocation() in the second clause
  // (as opposed to !=) because earlier signals (like DidCreateDocumentElement)
  // can happen multiple times, so we can receive earlier/equal run locations.
  if ((iter == frame_statuses_.end() &&
           run_location != UserScript::DOCUMENT_START) ||
      (iter != frame_statuses_.end() &&
           run_location > NextRunLocation(iter->second))) {
    // We also invalidate the frame, because the run order of pending injections
    // may also be bad.
    InvalidateForFrame(frame);
    return;
  } else if (iter != frame_statuses_.end() && iter->second >= run_location) {
    // Certain run location signals (like DidCreateDocumentElement) can happen
    // multiple times. Ignore the subsequent signals.
    return;
  }

  // Otherwise, all is right in the world, and we can get on with the
  // injections!
  frame_statuses_[frame] = run_location;
  InjectScripts(frame, run_location);
}

void ScriptInjectionManager::InjectScripts(
    content::RenderFrame* frame,
    UserScript::RunLocation run_location) {
  // Find any injections that want to run on the given frame.
  ScriptInjectionVector frame_injections;
  for (auto iter = pending_injections_.begin();
       iter != pending_injections_.end();) {
    if ((*iter)->render_frame() == frame) {
      frame_injections.push_back(std::move(*iter));
      iter = pending_injections_.erase(iter);
    } else {
      ++iter;
    }
  }

  // Add any injections for user scripts.
  int tab_id = ExtensionFrameHelper::Get(frame)->tab_id();
  user_script_set_manager_->GetAllInjections(&frame_injections, frame, tab_id,
                                             run_location);

  // Note that we are running in |frame|.
  active_injection_frames_.insert(frame);

  ScriptsRunInfo scripts_run_info(frame, run_location);
  scoped_refptr<AsyncScriptsRunInfo> async_run_info;
  if (FeatureSwitch::yield_between_content_script_runs()->IsEnabled())
    async_run_info = base::MakeRefCounted<AsyncScriptsRunInfo>(run_location);

  for (auto iter = frame_injections.begin(); iter != frame_injections.end();) {
    // It's possible for the frame to be invalidated in the course of injection
    // (if a script removes its own frame, for example). If this happens, abort.
    if (!active_injection_frames_.count(frame))
      break;
    std::unique_ptr<ScriptInjection> injection(std::move(*iter));
    iter = frame_injections.erase(iter);
    TryToInject(std::move(injection), run_location, &scripts_run_info,
                async_run_info);
  }

  // We are done running in the frame.
  active_injection_frames_.erase(frame);

  scripts_run_info.LogRun(activity_logging_enabled_);
}

void ScriptInjectionManager::TryToInject(
    std::unique_ptr<ScriptInjection> injection,
    UserScript::RunLocation run_location,
    ScriptsRunInfo* scripts_run_info,
    scoped_refptr<AsyncScriptsRunInfo> async_run_info) {
  // Try to inject the script. If the injection is waiting (i.e., for
  // permission), add it to the list of pending injections. If the injection
  // has blocked, add it to the list of running injections.
  // The Unretained below is safe because this object owns all the
  // ScriptInjections, so is guaranteed to outlive them.
  switch (injection->TryToInject(
      run_location, scripts_run_info, std::move(async_run_info),
      base::Bind(&ScriptInjectionManager::OnInjectionFinished,
                 base::Unretained(this)))) {
    case ScriptInjection::INJECTION_WAITING:
      pending_injections_.push_back(std::move(injection));
      break;
    case ScriptInjection::INJECTION_BLOCKED:
      running_injections_.push_back(std::move(injection));
      break;
    case ScriptInjection::INJECTION_FINISHED:
      break;
  }
}

void ScriptInjectionManager::HandleExecuteCode(
    const ExtensionMsg_ExecuteCode_Params& params,
    content::RenderFrame* render_frame) {
  std::unique_ptr<const InjectionHost> injection_host;
  if (params.host_id.type() == HostID::EXTENSIONS) {
    injection_host = ExtensionInjectionHost::Create(params.host_id.id());
    if (!injection_host)
      return;
  } else if (params.host_id.type() == HostID::WEBUI) {
    injection_host.reset(
        new WebUIInjectionHost(params.host_id));
  }

  std::unique_ptr<ScriptInjection> injection(new ScriptInjection(
      std::unique_ptr<ScriptInjector>(new ProgrammaticScriptInjector(params)),
      render_frame, std::move(injection_host),
      static_cast<UserScript::RunLocation>(params.run_at),
      activity_logging_enabled_));

  FrameStatusMap::const_iterator iter = frame_statuses_.find(render_frame);
  UserScript::RunLocation run_location =
      iter == frame_statuses_.end() ? UserScript::UNDEFINED : iter->second;

  ScriptsRunInfo scripts_run_info(render_frame, run_location);
  TryToInject(std::move(injection), run_location, &scripts_run_info, nullptr);
}

void ScriptInjectionManager::HandleExecuteDeclarativeScript(
    content::RenderFrame* render_frame,
    int tab_id,
    const ExtensionId& extension_id,
    int script_id,
    const GURL& url) {
  std::unique_ptr<ScriptInjection> injection =
      user_script_set_manager_->GetInjectionForDeclarativeScript(
          script_id, render_frame, tab_id, url, extension_id);
  if (injection.get()) {
    ScriptsRunInfo scripts_run_info(render_frame, UserScript::BROWSER_DRIVEN);
    // TODO(markdittmer): Use return value of TryToInject for error handling.
    TryToInject(std::move(injection), UserScript::BROWSER_DRIVEN,
                &scripts_run_info, nullptr);

    scripts_run_info.LogRun(activity_logging_enabled_);
  }
}

void ScriptInjectionManager::HandlePermitScriptInjection(int64_t request_id) {
  auto iter = pending_injections_.begin();
  for (; iter != pending_injections_.end(); ++iter) {
    if ((*iter)->request_id() == request_id) {
      DCHECK((*iter)->host_id().type() == HostID::EXTENSIONS);
      break;
    }
  }
  if (iter == pending_injections_.end())
    return;

  // At this point, because the request is present in pending_injections_, we
  // know that this is the same page that issued the request (otherwise,
  // RFOHelper::InvalidateAndResetFrame would have caused it to be cleared out).

  std::unique_ptr<ScriptInjection> injection(std::move(*iter));
  pending_injections_.erase(iter);

  ScriptsRunInfo scripts_run_info(injection->render_frame(),
                                  UserScript::RUN_DEFERRED);
  ScriptInjection::InjectionResult res = injection->OnPermissionGranted(
      &scripts_run_info);
  if (res == ScriptInjection::INJECTION_BLOCKED)
    running_injections_.push_back(std::move(injection));
  scripts_run_info.LogRun(activity_logging_enabled_);
}

}  // namespace extensions
