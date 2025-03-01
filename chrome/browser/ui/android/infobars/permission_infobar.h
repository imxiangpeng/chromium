// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ANDROID_INFOBARS_PERMISSION_INFOBAR_H_
#define CHROME_BROWSER_UI_ANDROID_INFOBARS_PERMISSION_INFOBAR_H_

#include "base/android/scoped_java_ref.h"
#include "base/macros.h"
#include "chrome/browser/ui/android/infobars/confirm_infobar.h"

class PermissionInfoBarDelegate;

class PermissionInfoBar : public ConfirmInfoBar {
 public:
  explicit PermissionInfoBar(
      std::unique_ptr<PermissionInfoBarDelegate> delegate);
  ~PermissionInfoBar() override;

  static base::android::ScopedJavaLocalRef<jobject> CreateRenderInfoBarHelper(
      JNIEnv* env,
      int enumerated_icon_id,
      const base::android::JavaRef<jobject>& tab,
      const base::android::ScopedJavaLocalRef<jobject>& icon_bitmap,
      const base::string16& message_text,
      const base::string16& link_text,
      const base::string16& ok_button_text,
      const base::string16& cancel_button_text,
      std::vector<int>& content_settings,
      bool show_persistence_toggle);

  static bool IsSwitchOn(JNIEnv* env,
                         const base::android::JavaRef<jobject>& info_bar_obj);

 protected:
  PermissionInfoBarDelegate* GetDelegate();

  // InfoBarAndroid overrides.
  base::android::ScopedJavaLocalRef<jobject> CreateRenderInfoBar(
      JNIEnv* env) override;

 private:
  void ProcessButton(int action) override;

  DISALLOW_COPY_AND_ASSIGN(PermissionInfoBar);
};

#endif  // CHROME_BROWSER_UI_ANDROID_INFOBARS_PERMISSION_INFOBAR_H_
