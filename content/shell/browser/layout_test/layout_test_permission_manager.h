// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_PERMISSION_MANAGER_H_
#define CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_PERMISSION_MANAGER_H_

#include <stddef.h>

#include "base/callback_forward.h"
#include "base/containers/hash_tables.h"
#include "base/id_map.h"
#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "content/public/browser/permission_manager.h"
#include "url/gurl.h"

namespace content {

class LayoutTestPermissionManager : public PermissionManager {
 public:
  LayoutTestPermissionManager();
  ~LayoutTestPermissionManager() override;

  // PermissionManager overrides.
  int RequestPermission(
      PermissionType permission,
      RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      bool user_gesture,
      const base::Callback<void(blink::mojom::PermissionStatus)>& callback)
      override;
  int RequestPermissions(
      const std::vector<PermissionType>& permission,
      RenderFrameHost* render_frame_host,
      const GURL& requesting_origin,
      bool user_gesture,
      const base::Callback<
          void(const std::vector<blink::mojom::PermissionStatus>&)>& callback)
      override;
  void CancelPermissionRequest(int request_id) override;
  void ResetPermission(PermissionType permission,
                       const GURL& requesting_origin,
                       const GURL& embedding_origin) override;
  blink::mojom::PermissionStatus GetPermissionStatus(
      PermissionType permission,
      const GURL& requesting_origin,
      const GURL& embedding_origin) override;
  int SubscribePermissionStatusChange(
      PermissionType permission,
      const GURL& requesting_origin,
      const GURL& embedding_origin,
      const base::Callback<void(blink::mojom::PermissionStatus)>& callback)
      override;
  void UnsubscribePermissionStatusChange(int subscription_id) override;

  void SetPermission(PermissionType permission,
                     blink::mojom::PermissionStatus status,
                     const GURL& url,
                     const GURL& embedding_url);
  void ResetPermissions();

 private:
  // Representation of a permission for the LayoutTestPermissionManager.
  struct PermissionDescription {
    PermissionDescription() = default;
    PermissionDescription(PermissionType type,
                          const GURL& origin,
                          const GURL& embedding_origin);
    bool operator==(const PermissionDescription& other) const;
    bool operator!=(const PermissionDescription& other) const;

    // Hash operator for hash maps.
    struct Hash {
      size_t operator()(const PermissionDescription& description) const;
    };

    PermissionType type;
    GURL origin;
    GURL embedding_origin;
  };

  struct Subscription;
  using SubscriptionsMap = IDMap<std::unique_ptr<Subscription>>;
  using PermissionsMap = base::hash_map<PermissionDescription,
                                        blink::mojom::PermissionStatus,
                                        PermissionDescription::Hash>;

  void OnPermissionChanged(const PermissionDescription& permission,
                           blink::mojom::PermissionStatus status);

  // Mutex for permissions access. Unfortunately, the permissions can be
  // accessed from the IO thread because of Notifications' synchronous IPC.
  base::Lock permissions_lock_;

  // List of permissions currently known by the LayoutTestPermissionManager and
  // their associated |PermissionStatus|.
  PermissionsMap permissions_;

  // List of subscribers currently listening to permission changes.
  SubscriptionsMap subscriptions_;

  DISALLOW_COPY_AND_ASSIGN(LayoutTestPermissionManager);
};

}  // namespace content

#endif // CONTENT_SHELL_BROWSER_LAYOUT_TEST_LAYOUT_TEST_PERMISSION_MANAGER_H_
