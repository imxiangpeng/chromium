// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/android/media_drm_storage_bridge.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/android/callback_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "jni/MediaDrmStorageBridge_jni.h"
#include "media/base/android/android_util.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaByteArrayToByteVector;
using base::android::JavaParamRef;
using base::android::RunCallbackAndroid;
using base::android::ScopedJavaLocalRef;
using base::android::ToJavaByteArray;

namespace media {

// static
bool MediaDrmStorageBridge::RegisterMediaDrmStorageBridge(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

MediaDrmStorageBridge::MediaDrmStorageBridge()
    : task_runner_(base::ThreadTaskRunnerHandle::Get()), weak_factory_(this) {}

MediaDrmStorageBridge::~MediaDrmStorageBridge() = default;

void MediaDrmStorageBridge::Initialize(const url::Origin& origin,
                                       const CreateStorageCB& create_storage_cb,
                                       base::OnceClosure on_init) {
  DCHECK(create_storage_cb);
  impl_ = create_storage_cb.Run();

  // TODO(yucliu): MediaDrmStorage should generate and return origin id
  // asynchronously in a callback.
  impl_->Initialize(origin);
  origin_id_ = origin.Serialize();

  std::move(on_init).Run();
}

void MediaDrmStorageBridge::OnProvisioned(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_storage,
    // Callback<Boolean>
    const JavaParamRef<jobject>& j_callback) {
  DCHECK(impl_);
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MediaDrmStorage::OnProvisioned, impl_->AsWeakPtr(),
          base::BindOnce(&MediaDrmStorageBridge::RunAndroidBoolCallback,
                         // Bind callback to WeakPtr in case callback is called
                         // after object is deleted.
                         weak_factory_.GetWeakPtr(),
                         base::Passed(CreateJavaObjectPtr(j_callback.obj())))));
}

void MediaDrmStorageBridge::OnLoadInfo(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_storage,
    const JavaParamRef<jbyteArray>& j_session_id,
    // Callback<PersistentInfo>
    const JavaParamRef<jobject>& j_callback) {
  DCHECK(impl_);
  std::string session_id = JavaBytesToString(env, j_session_id);
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MediaDrmStorage::LoadPersistentSession, impl_->AsWeakPtr(),
          session_id,
          base::BindOnce(&MediaDrmStorageBridge::OnSessionDataLoaded,
                         weak_factory_.GetWeakPtr(),
                         base::Passed(CreateJavaObjectPtr(j_callback.obj())),
                         session_id)));
}

void MediaDrmStorageBridge::OnSaveInfo(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_storage,
    const JavaParamRef<jobject>& j_persist_info,
    // Callback<Boolean>
    const JavaParamRef<jobject>& j_callback) {
  DCHECK(impl_);
  std::vector<uint8_t> key_set_id;
  JavaByteArrayToByteVector(
      env, Java_PersistentInfo_keySetId(env, j_persist_info).obj(),
      &key_set_id);

  std::string mime = ConvertJavaStringToUTF8(
      env, Java_PersistentInfo_mimeType(env, j_persist_info));

  std::string session_id = JavaBytesToString(
      env, Java_PersistentInfo_emeId(env, j_persist_info).obj());

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MediaDrmStorage::SavePersistentSession, impl_->AsWeakPtr(),
          session_id,
          MediaDrmStorage::SessionData(std::move(key_set_id), std::move(mime)),
          base::BindOnce(&MediaDrmStorageBridge::RunAndroidBoolCallback,
                         weak_factory_.GetWeakPtr(),
                         base::Passed(CreateJavaObjectPtr(j_callback.obj())))));
}

void MediaDrmStorageBridge::OnClearInfo(
    JNIEnv* env,
    const JavaParamRef<jobject>& j_storage,
    const JavaParamRef<jbyteArray>& j_session_id,
    // Callback<Boolean>
    const JavaParamRef<jobject>& j_callback) {
  DCHECK(impl_);
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &MediaDrmStorage::RemovePersistentSession, impl_->AsWeakPtr(),
          JavaBytesToString(env, j_session_id),
          base::BindOnce(&MediaDrmStorageBridge::RunAndroidBoolCallback,
                         weak_factory_.GetWeakPtr(),
                         base::Passed(CreateJavaObjectPtr(j_callback.obj())))));
}

void MediaDrmStorageBridge::RunAndroidBoolCallback(JavaObjectPtr j_callback,
                                                   bool success) {
  RunCallbackAndroid(*j_callback, success);
}

void MediaDrmStorageBridge::OnSessionDataLoaded(
    JavaObjectPtr j_callback,
    const std::string& session_id,
    std::unique_ptr<MediaDrmStorage::SessionData> session_data) {
  if (!session_data) {
    RunCallbackAndroid(*j_callback, ScopedJavaLocalRef<jobject>());
    return;
  }

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jbyteArray> j_eme_id = StringToJavaBytes(env, session_id);
  ScopedJavaLocalRef<jbyteArray> j_key_set_id = ToJavaByteArray(
      env, session_data->key_set_id.data(), session_data->key_set_id.size());
  ScopedJavaLocalRef<jstring> j_mime =
      ConvertUTF8ToJavaString(env, session_data->mime_type);

  RunCallbackAndroid(*j_callback, Java_PersistentInfo_create(
                                      env, j_eme_id, j_key_set_id, j_mime));
}

}  // namespace media
