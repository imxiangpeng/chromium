// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_CUSTOMTABS_ORIGIN_VERIFIER_H_
#define CHROME_BROWSER_ANDROID_CUSTOMTABS_ORIGIN_VERIFIER_H_

#include "base/android/scoped_java_ref.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context_getter.h"

namespace base {
class DictionaryValue;
}

namespace digital_asset_links {
class DigitalAssetLinksHandler;
}

namespace customtabs {

// JNI bridge for OriginVerifier.java
class OriginVerifier {
 public:
  OriginVerifier(JNIEnv* env,
                 const base::android::JavaRef<jobject>& obj,
                 const base::android::JavaRef<jobject>& jprofile);
  ~OriginVerifier();

  // Verify origin with the given parameters. No network requests can be made
  // if the params are null.
  bool VerifyOrigin(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    const base::android::JavaParamRef<jstring>& j_package_name,
                    const base::android::JavaParamRef<jstring>& j_fingerprint,
                    const base::android::JavaParamRef<jstring>& j_origin);

  void Destroy(JNIEnv* env, const base::android::JavaRef<jobject>& obj);

 private:
  void OnRelationshipCheckComplete(
      std::unique_ptr<base::DictionaryValue> response);

  std::unique_ptr<digital_asset_links::DigitalAssetLinksHandler>
      asset_link_handler_;

  base::android::ScopedJavaGlobalRef<jobject> jobject_;

  DISALLOW_COPY_AND_ASSIGN(OriginVerifier);
};

}  // namespace customtabs

#endif  // CHROME_BROWSER_ANDROID_CUSTOMTABS_ORIGIN_VERIFIER_H_
