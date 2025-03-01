// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/compositor/layer_title_cache.h"

#include <android/bitmap.h>

#include <memory>

#include "base/memory/ptr_util.h"
#include "cc/layers/layer.h"
#include "cc/layers/ui_resource_layer.h"
#include "chrome/browser/android/compositor/decoration_title.h"
#include "jni/LayerTitleCache_jni.h"
#include "ui/android/resources/resource_manager.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"

using base::android::JavaParamRef;
using base::android::JavaRef;

namespace android {

// static
LayerTitleCache* LayerTitleCache::FromJavaObject(const JavaRef<jobject>& jobj) {
  if (jobj.is_null())
    return nullptr;
  return reinterpret_cast<LayerTitleCache*>(Java_LayerTitleCache_getNativePtr(
      base::android::AttachCurrentThread(), jobj));
}

LayerTitleCache::LayerTitleCache(JNIEnv* env,
                                 jobject obj,
                                 jint fade_width,
                                 jint favicon_start_padding,
                                 jint favicon_end_padding,
                                 jint spinner_resource_id,
                                 jint spinner_incognito_resource_id)
    : weak_java_title_cache_(env, obj),
      fade_width_(fade_width),
      favicon_start_padding_(favicon_start_padding),
      favicon_end_padding_(favicon_end_padding),
      spinner_resource_id_(spinner_resource_id),
      spinner_incognito_resource_id_(spinner_incognito_resource_id),
      resource_manager_(nullptr) {
}

void LayerTitleCache::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

void LayerTitleCache::UpdateLayer(JNIEnv* env,
                                  const JavaParamRef<jobject>& obj,
                                  jint tab_id,
                                  jint title_resource_id,
                                  jint favicon_resource_id,
                                  bool is_incognito,
                                  bool is_rtl) {
  DecorationTitle* title_layer = layer_cache_.Lookup(tab_id);
  if (title_layer) {
    if (title_resource_id != -1 && favicon_resource_id != -1) {
      title_layer->Update(title_resource_id, favicon_resource_id, fade_width_,
                          favicon_start_padding_, favicon_end_padding_,
                          is_incognito, is_rtl);
    } else {
      layer_cache_.Remove(tab_id);
    }
  } else {
    layer_cache_.AddWithID(
        base::MakeUnique<DecorationTitle>(
            resource_manager_, title_resource_id, favicon_resource_id,
            spinner_resource_id_, spinner_incognito_resource_id_, fade_width_,
            favicon_start_padding_, favicon_end_padding_, is_incognito, is_rtl),
        tab_id);
  }
}

void LayerTitleCache::UpdateFavicon(JNIEnv* env,
                                    const JavaParamRef<jobject>& obj,
                                    jint tab_id,
                                    jint favicon_resource_id) {
  DecorationTitle* title_layer = layer_cache_.Lookup(tab_id);
  if (title_layer && favicon_resource_id != -1) {
    title_layer->SetFaviconResourceId(favicon_resource_id);
  }
}

void LayerTitleCache::ClearExcept(JNIEnv* env,
                                  const JavaParamRef<jobject>& obj,
                                  jint except_id) {
  IDMap<std::unique_ptr<DecorationTitle>>::iterator iter(&layer_cache_);
  for (; !iter.IsAtEnd(); iter.Advance()) {
    const int id = iter.GetCurrentKey();
    if (id != except_id)
      layer_cache_.Remove(id);
  }
}

DecorationTitle* LayerTitleCache::GetTitleLayer(int tab_id) {
  if (!layer_cache_.Lookup(tab_id)) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_LayerTitleCache_buildUpdatedTitle(env, weak_java_title_cache_.get(env),
        tab_id);
  }

  return layer_cache_.Lookup(tab_id);
}

void LayerTitleCache::SetResourceManager(
    ui::ResourceManager* resource_manager) {
  resource_manager_ = resource_manager;

  IDMap<std::unique_ptr<DecorationTitle>>::iterator iter(&layer_cache_);
  for (; !iter.IsAtEnd(); iter.Advance()) {
    iter.GetCurrentValue()->SetResourceManager(resource_manager_);
  }
}

LayerTitleCache::~LayerTitleCache() {
}

// ----------------------------------------------------------------------------
// Native JNI methods
// ----------------------------------------------------------------------------

jlong Init(JNIEnv* env,
           const JavaParamRef<jobject>& obj,
           jint fade_width,
           jint favicon_start_padding,
           jint favicon_end_padding,
           jint spinner_resource_id,
           jint spinner_incognito_resource_id) {
  LayerTitleCache* cache = new LayerTitleCache(
      env, obj, fade_width, favicon_start_padding, favicon_end_padding,
      spinner_resource_id, spinner_incognito_resource_id);
  return reinterpret_cast<intptr_t>(cache);
}

}  // namespace android
