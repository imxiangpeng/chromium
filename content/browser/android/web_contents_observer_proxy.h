// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_WEB_CONTENTS_OBSERVER_PROXY_H_
#define CONTENT_BROWSER_ANDROID_WEB_CONTENTS_OBSERVER_PROXY_H_

#include <jni.h>

#include "base/android/jni_weak_ref.h"
#include "base/macros.h"
#include "base/process/kill.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/frame_navigate_params.h"
#include "url/gurl.h"

namespace content {

class WebContents;

// Extends WebContentsObserver for providing a public Java API for some of the
// the calls it receives.
class WebContentsObserverProxy : public WebContentsObserver {
 public:
  WebContentsObserverProxy(JNIEnv* env, jobject obj, WebContents* web_contents);
  ~WebContentsObserverProxy() override;

  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

 private:
  void RenderViewReady() override;
  void RenderProcessGone(base::TerminationStatus termination_status) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidFailLoad(RenderFrameHost* render_frame_host,
                   const GURL& validated_url,
                   int error_code,
                   const base::string16& error_description) override;
  void DocumentAvailableInMainFrame() override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void WasShown() override;
  void WasHidden() override;
  void TitleWasSet(NavigationEntry* entry, bool explicit_set) override;

  void DidStartNavigation(NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

  void DidFinishLoad(RenderFrameHost* render_frame_host,
                     const GURL& validated_url) override;
  void DocumentLoadedInFrame(RenderFrameHost* render_frame_host) override;
  void NavigationEntryCommitted(
      const LoadCommittedDetails& load_details) override;
  void WebContentsDestroyed() override;
  void DidAttachInterstitialPage() override;
  void DidDetachInterstitialPage() override;
  void DidChangeThemeColor(SkColor color) override;
  void SetToBaseURLForDataURLIfNeeded(std::string* url);

  base::android::ScopedJavaGlobalRef<jobject> java_observer_;
  GURL base_url_of_last_started_data_url_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsObserverProxy);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_WEB_CONTENTS_OBSERVER_PROXY_H_
