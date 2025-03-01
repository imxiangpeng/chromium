// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/android/scoped_java_ref.h"
#include "base/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/threading/thread_task_runner_handle.h"
#include "jni/ScreenshotTask_jni.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/android/window_android.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/snapshot/snapshot.h"

using base::android::AttachCurrentThread;
using base::android::JavaParamRef;
using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using ui::WindowAndroid;

namespace chrome {
namespace android {

void SnapshotCallback(JNIEnv* env,
                      const JavaRef<jobject>& callback,
                      scoped_refptr<base::RefCountedMemory> png_data) {
  if (png_data.get()) {
    size_t size = png_data->size();
    ScopedJavaLocalRef<jbyteArray> jbytes(env, env->NewByteArray(size));
    env->SetByteArrayRegion(jbytes.obj(), 0, size, (jbyte*)png_data->front());
    Java_ScreenshotTask_notifySnapshotFinished(env, callback, jbytes);
  } else {
    Java_ScreenshotTask_notifySnapshotFinished(env, callback, nullptr);
  }
}

void GrabWindowSnapshotAsync(JNIEnv* env,
                             const JavaParamRef<jclass>& clazz,
                             const JavaParamRef<jobject>& jcallback,
                             jlong native_window_android,
                             jint window_width,
                             jint window_height) {
  WindowAndroid* window_android = reinterpret_cast<WindowAndroid*>(
      native_window_android);
  gfx::Rect window_bounds(window_width, window_height);
  ui::GrabWindowSnapshotAsyncPNG(
      window_android, window_bounds, base::ThreadTaskRunnerHandle::Get(),
      base::Bind(&SnapshotCallback, env,
                 ScopedJavaGlobalRef<jobject>(env, jcallback)));
}

}  // namespace android
}  // namespace chrome
