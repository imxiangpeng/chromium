// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/native_web_keyboard_event.h"

#include "base/android/jni_android.h"
#include "content/browser/renderer_host/input/web_input_event_builders_android.h"
#include "ui/events/base_event_utils.h"
#include "ui/gfx/native_widget_types.h"

namespace {

jobject NewGlobalRefForKeyEvent(jobject key_event) {
  if (key_event == nullptr) return nullptr;
  return base::android::AttachCurrentThread()->NewGlobalRef(key_event);
}

void DeleteGlobalRefForKeyEvent(jobject key_event) {
  if (key_event != nullptr)
    base::android::AttachCurrentThread()->DeleteGlobalRef(key_event);
}

}

namespace content {

NativeWebKeyboardEvent::NativeWebKeyboardEvent(blink::WebInputEvent::Type type,
                                               int modifiers,
                                               base::TimeTicks timestamp)
    : NativeWebKeyboardEvent(type,
                             modifiers,
                             ui::EventTimeStampToSeconds(timestamp)) {}

NativeWebKeyboardEvent::NativeWebKeyboardEvent(blink::WebInputEvent::Type type,
                                               int modifiers,
                                               double timestampSeconds)
    : WebKeyboardEvent(type, modifiers, timestampSeconds),
      os_event(nullptr),
      skip_in_browser(false) {}

NativeWebKeyboardEvent::NativeWebKeyboardEvent(
    const blink::WebKeyboardEvent& web_event)
    : WebKeyboardEvent(web_event), os_event(nullptr), skip_in_browser(false) {}

NativeWebKeyboardEvent::NativeWebKeyboardEvent(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& android_key_event,
    blink::WebInputEvent::Type type,
    int modifiers,
    double time_secs,
    int keycode,
    int scancode,
    int unicode_character,
    bool is_system_key)
    : WebKeyboardEvent(WebKeyboardEventBuilder::Build(env,
                                                      android_key_event,
                                                      type,
                                                      modifiers,
                                                      time_secs,
                                                      keycode,
                                                      scancode,
                                                      unicode_character,
                                                      is_system_key)),
      os_event(nullptr),
      skip_in_browser(false) {
  if (!android_key_event.is_null())
    os_event = NewGlobalRefForKeyEvent(android_key_event.obj());
}

NativeWebKeyboardEvent::NativeWebKeyboardEvent(
    const NativeWebKeyboardEvent& other)
    : WebKeyboardEvent(other),
      os_event(NewGlobalRefForKeyEvent(other.os_event)),
      skip_in_browser(other.skip_in_browser) {
}

NativeWebKeyboardEvent& NativeWebKeyboardEvent::operator=(
    const NativeWebKeyboardEvent& other) {
  WebKeyboardEvent::operator=(other);

  os_event = NewGlobalRefForKeyEvent(other.os_event);
  skip_in_browser = other.skip_in_browser;

  return *this;
}

NativeWebKeyboardEvent::~NativeWebKeyboardEvent() {
  DeleteGlobalRefForKeyEvent(os_event);
}

}  // namespace content
