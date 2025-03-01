// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/offline_items_collection/core/android/offline_content_aggregator_bridge.h"

#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/offline_items_collection/core/android/offline_item_bridge.h"
#include "components/offline_items_collection/core/android/offline_item_visuals_bridge.h"
#include "components/offline_items_collection/core/offline_item.h"
#include "components/offline_items_collection/core/throttled_offline_content_provider.h"
#include "jni/OfflineContentAggregatorBridge_jni.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using base::android::ScopedJavaGlobalRef;

namespace offline_items_collection {
namespace android {

namespace {
const char kOfflineContentAggregatorBridgeUserDataKey[] = "aggregator_bridge";

ContentId CreateContentId(JNIEnv* env,
                          const JavaParamRef<jstring>& j_namespace,
                          const JavaParamRef<jstring>& j_id) {
  return ContentId(ConvertJavaStringToUTF8(env, j_namespace),
                   ConvertJavaStringToUTF8(env, j_id));
}

void GetVisualsForItemHelperCallback(ScopedJavaGlobalRef<jobject> j_callback,
                                     const ContentId& id,
                                     const OfflineItemVisuals* visuals) {
  JNIEnv* env = AttachCurrentThread();
  Java_OfflineContentAggregatorBridge_onVisualsAvailable(
      env, j_callback, ConvertUTF8ToJavaString(env, id.name_space),
      ConvertUTF8ToJavaString(env, id.id),
      OfflineItemVisualsBridge::CreateOfflineItemVisuals(env, visuals));
}

}  // namespace

// static
base::android::ScopedJavaLocalRef<jobject>
OfflineContentAggregatorBridge::GetBridgeForOfflineContentAggregator(
    OfflineContentAggregator* aggregator) {
  if (!aggregator->GetUserData(kOfflineContentAggregatorBridgeUserDataKey)) {
    aggregator->SetUserData(
        kOfflineContentAggregatorBridgeUserDataKey,
        base::WrapUnique(new OfflineContentAggregatorBridge(aggregator)));
  }
  OfflineContentAggregatorBridge* bridge =
      static_cast<OfflineContentAggregatorBridge*>(
          aggregator->GetUserData(kOfflineContentAggregatorBridgeUserDataKey));

  return ScopedJavaLocalRef<jobject>(bridge->java_ref_);
}

OfflineContentAggregatorBridge::OfflineContentAggregatorBridge(
    OfflineContentAggregator* aggregator)
    : provider_(base::MakeUnique<ThrottledOfflineContentProvider>(aggregator)) {
  JNIEnv* env = AttachCurrentThread();
  java_ref_.Reset(Java_OfflineContentAggregatorBridge_create(
      env, reinterpret_cast<intptr_t>(this)));

  provider_->AddObserver(this);
}

OfflineContentAggregatorBridge::~OfflineContentAggregatorBridge() {
  // TODO(dtrainor): Do not need to unregister because in the destructor of the
  // base class of OfflineContentAggregator.  Is |observers_| already dead?
  provider_->RemoveObserver(this);

  Java_OfflineContentAggregatorBridge_onNativeDestroyed(AttachCurrentThread(),
                                                        java_ref_);
}

jboolean OfflineContentAggregatorBridge::AreItemsAvailable(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj) {
  return provider_->AreItemsAvailable();
}

void OfflineContentAggregatorBridge::OpenItem(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id) {
  provider_->OpenItem(CreateContentId(env, j_namespace, j_id));
}

void OfflineContentAggregatorBridge::RemoveItem(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id) {
  provider_->RemoveItem(CreateContentId(env, j_namespace, j_id));
}

void OfflineContentAggregatorBridge::CancelDownload(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id) {
  provider_->CancelDownload(CreateContentId(env, j_namespace, j_id));
}

void OfflineContentAggregatorBridge::PauseDownload(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_guid) {
  provider_->PauseDownload(CreateContentId(env, j_namespace, j_guid));
}

void OfflineContentAggregatorBridge::ResumeDownload(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id) {
  provider_->ResumeDownload(CreateContentId(env, j_namespace, j_id));
}

ScopedJavaLocalRef<jobject> OfflineContentAggregatorBridge::GetItemById(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id) {
  const OfflineItem* item =
      provider_->GetItemById(CreateContentId(env, j_namespace, j_id));

  return OfflineItemBridge::CreateOfflineItem(env, item);
}

ScopedJavaLocalRef<jobject> OfflineContentAggregatorBridge::GetAllItems(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj) {
  return OfflineItemBridge::CreateOfflineItemList(env,
                                                  provider_->GetAllItems());
}

void OfflineContentAggregatorBridge::GetVisualsForItem(
    JNIEnv* env,
    const JavaParamRef<jobject>& jobj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_id,
    const JavaParamRef<jobject>& j_callback) {
  provider_->GetVisualsForItem(
      CreateContentId(env, j_namespace, j_id),
      base::Bind(&GetVisualsForItemHelperCallback,
                 ScopedJavaGlobalRef<jobject>(env, j_callback)));
}

void OfflineContentAggregatorBridge::OnItemsAvailable(
    OfflineContentProvider* provider) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = AttachCurrentThread();
  Java_OfflineContentAggregatorBridge_onItemsAvailable(env, java_ref_);
}

void OfflineContentAggregatorBridge::OnItemsAdded(
    const OfflineContentProvider::OfflineItemList& items) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = AttachCurrentThread();
  Java_OfflineContentAggregatorBridge_onItemsAdded(
      env, java_ref_, OfflineItemBridge::CreateOfflineItemList(env, items));
}

void OfflineContentAggregatorBridge::OnItemRemoved(const ContentId& id) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = AttachCurrentThread();
  Java_OfflineContentAggregatorBridge_onItemRemoved(
      env, java_ref_, ConvertUTF8ToJavaString(env, id.name_space),
      ConvertUTF8ToJavaString(env, id.id));
}

void OfflineContentAggregatorBridge::OnItemUpdated(const OfflineItem& item) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = AttachCurrentThread();
  Java_OfflineContentAggregatorBridge_onItemUpdated(
      env, java_ref_, OfflineItemBridge::CreateOfflineItem(env, &item));
}

}  // namespace android
}  // namespace offline_items_collection
