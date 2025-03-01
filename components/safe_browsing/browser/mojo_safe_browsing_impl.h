// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SAFE_BROWSING_BROWSER_MOJO_SAFE_BROWSING_IMPL_H_
#define COMPONENTS_SAFE_BROWSING_BROWSER_MOJO_SAFE_BROWSING_IMPL_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "components/safe_browsing/browser/url_checker_delegate.h"
#include "components/safe_browsing/common/safe_browsing.mojom.h"
#include "ipc/ipc_message.h"

namespace safe_browsing {

// This class implements the Mojo interface for renderers to perform
// SafeBrowsing URL checks.
class MojoSafeBrowsingImpl : public mojom::SafeBrowsing {
 public:
  ~MojoSafeBrowsingImpl() override;

  static void MaybeCreate(
      int render_process_id,
      const base::Callback<UrlCheckerDelegate*()>& delegate_getter,
      mojom::SafeBrowsingRequest request);

 private:
  MojoSafeBrowsingImpl(scoped_refptr<UrlCheckerDelegate> delegate,
                       int render_process_id);

  // mojom::SafeBrowsing implementation.
  void CreateCheckerAndCheck(int32_t render_frame_id,
                             mojom::SafeBrowsingUrlCheckerRequest request,
                             const GURL& url,
                             const std::string& method,
                             const std::string& headers,
                             int32_t load_flags,
                             content::ResourceType resource_type,
                             bool has_user_gesture,
                             CreateCheckerAndCheckCallback callback) override;

  scoped_refptr<UrlCheckerDelegate> delegate_;
  int render_process_id_ = MSG_ROUTING_NONE;

  DISALLOW_COPY_AND_ASSIGN(MojoSafeBrowsingImpl);
};

}  // namespace safe_browsing

#endif  // COMPONENTS_SAFE_BROWSING_BROWSER_MOJO_SAFE_BROWSING_IMPL_H_
