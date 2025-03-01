// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/internal/android/tracker_impl_android.h"

#include <vector>

#include "base/android/callback_android.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "components/feature_engagement/public/feature_list.h"
#include "components/feature_engagement/public/tracker.h"
#include "jni/TrackerImpl_jni.h"

namespace feature_engagement {

namespace {

const char kTrackerImplAndroidKey[] = "tracker_impl_android";

// Create mapping from feature name to base::Feature.
TrackerImplAndroid::FeatureMap CreateMapFromNameToFeature(
    FeatureVector features) {
  TrackerImplAndroid::FeatureMap feature_map;
  for (auto it = features.begin(); it != features.end(); ++it) {
    feature_map[(*it)->name] = *it;
  }
  return feature_map;
}

TrackerImplAndroid* FromTrackerImpl(Tracker* feature_engagement) {
  TrackerImpl* impl = static_cast<TrackerImpl*>(feature_engagement);
  TrackerImplAndroid* impl_android = static_cast<TrackerImplAndroid*>(
      impl->GetUserData(kTrackerImplAndroidKey));
  if (!impl_android) {
    impl_android = new TrackerImplAndroid(impl, GetAllFeatures());
    impl->SetUserData(kTrackerImplAndroidKey, base::WrapUnique(impl_android));
  }
  return impl_android;
}

}  // namespace

// static
bool TrackerImplAndroid::RegisterJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

// static
TrackerImplAndroid* TrackerImplAndroid::FromJavaObject(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj) {
  return reinterpret_cast<TrackerImplAndroid*>(
      Java_TrackerImpl_getNativePtr(env, jobj));
}

// This function is declared in //components/feature_engagement/public/tracker.h
// and should be linked in to any binary using Tracker::GetJavaObject.
// static
base::android::ScopedJavaLocalRef<jobject> Tracker::GetJavaObject(
    Tracker* feature_engagement) {
  return FromTrackerImpl(feature_engagement)->GetJavaObject();
}

TrackerImplAndroid::TrackerImplAndroid(TrackerImpl* tracker_impl,
                                       FeatureVector features)
    : features_(CreateMapFromNameToFeature(features)),
      tracker_impl_(tracker_impl) {
  JNIEnv* env = base::android::AttachCurrentThread();

  java_obj_.Reset(
      env,
      Java_TrackerImpl_create(env, reinterpret_cast<intptr_t>(this)).obj());
}

TrackerImplAndroid::~TrackerImplAndroid() {
  Java_TrackerImpl_clearNativePtr(base::android::AttachCurrentThread(),
                                  java_obj_);
}

base::android::ScopedJavaLocalRef<jobject> TrackerImplAndroid::GetJavaObject() {
  return base::android::ScopedJavaLocalRef<jobject>(java_obj_);
}

void TrackerImplAndroid::NotifyEvent(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jevent) {
  std::string event = ConvertJavaStringToUTF8(env, jevent);
  tracker_impl_->NotifyEvent(event);
}

bool TrackerImplAndroid::ShouldTriggerHelpUI(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jfeature) {
  std::string feature = ConvertJavaStringToUTF8(env, jfeature);
  DCHECK(features_.find(feature) != features_.end());

  return tracker_impl_->ShouldTriggerHelpUI(*features_[feature]);
}

jint TrackerImplAndroid::GetTriggerState(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jfeature) {
  std::string feature = ConvertJavaStringToUTF8(env, jfeature);
  DCHECK(features_.find(feature) != features_.end());

  return static_cast<int>(tracker_impl_->GetTriggerState(*features_[feature]));
}

void TrackerImplAndroid::Dismissed(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jfeature) {
  std::string feature = ConvertJavaStringToUTF8(env, jfeature);
  DCHECK(features_.find(feature) != features_.end());

  tracker_impl_->Dismissed(*features_[feature]);
}

bool TrackerImplAndroid::IsInitialized(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj) {
  return tracker_impl_->IsInitialized();
}

void TrackerImplAndroid::AddOnInitializedCallback(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& jobj,
    const base::android::JavaParamRef<jobject>& j_callback_obj) {
  // Disambiguate RunCallbackAndroid to get the reference to the bool version.
  void (*runBoolCallback)(const base::android::JavaRef<jobject>&, bool) =
      &base::android::RunCallbackAndroid;
  tracker_impl_->AddOnInitializedCallback(
      base::Bind(runBoolCallback,
                 base::android::ScopedJavaGlobalRef<jobject>(j_callback_obj)));
}

}  // namespace feature_engagement
