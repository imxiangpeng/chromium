// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ANDROID_DIALOG_OVERLAY_IMPL_H_
#define CONTENT_BROWSER_ANDROID_DIALOG_OVERLAY_IMPL_H_

#include "base/android/jni_android.h"
#include "base/android/jni_weak_ref.h"
#include "base/android/scoped_java_ref.h"
#include "base/unguessable_token.h"
#include "content/browser/android/content_view_core.h"
#include "content/browser/android/content_view_core_observer.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {

// Native counterpart to DialogOverlayImpl java class.  This is created by the
// java side.  When the ContentViewCore for the provided token is attached or
// detached from a WindowAndroid, we get the Android window token and notify the
// java side.
class DialogOverlayImpl : public ContentViewCoreObserver,
                          public WebContentsObserver {
 public:
  // This may not call back into |obj| directly, but must post.  This is because
  // |obj| is still being initialized.
  DialogOverlayImpl(const base::android::JavaParamRef<jobject>& obj,
                    RenderFrameHostImpl* rfhi,
                    WebContents* web_contents,
                    ContentViewCore* cvc);
  ~DialogOverlayImpl() override;

  // Called when the java side is ready for token / dismissed callbacks.  May
  // call back before returning.  Must guarantee that a token is eventually sent
  // if we have one.
  void CompleteInit(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj);

  // Clean up and post to delete |this| later.
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);

  // Calls ReceiveCompositorOffset() (java) with the compositor screen offset
  // before returning, in physical pixels.  We send |rect| as a convenience.
  void GetCompositorOffset(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           const base::android::JavaParamRef<jobject>& rect);

  // ContentViewCoreObserver
  void OnContentViewCoreDestroyed() override;
  void OnAttachedToWindow() override;
  void OnDetachedFromWindow() override;

  // WebContentsObserver
  void WasHidden() override;
  void WebContentsDestroyed() override;
  void FrameDeleted(RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(RenderFrameHost* render_frame_host) override;
  void RenderFrameHostChanged(RenderFrameHost* old_host,
                              RenderFrameHost* new_host) override;

  // Unregister for tokens if we're registered, and clear |cvc_|.
  void UnregisterForTokensIfNeeded();

 private:
  // Signals the overlay should be cleaned up and no longer used.
  void Stop();

  // Java object that owns us.
  JavaObjectWeakGlobalRef obj_;

  // RenderFrameHostImpl* associated with the given overlay routing token.
  RenderFrameHostImpl* rfhi_;

  // ContentViewCore instance that we're registered with as an observer.
  ContentViewCore* cvc_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_ANDROID_DIALOG_OVERLAY_IMPL_H_
