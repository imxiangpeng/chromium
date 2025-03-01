// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SESSIONS_CORE_TAB_RESTORE_SERVICE_CLIENT_H_
#define COMPONENTS_SESSIONS_CORE_TAB_RESTORE_SERVICE_CLIENT_H_

#include <memory>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "components/sessions/core/session_id.h"
#include "components/sessions/core/sessions_export.h"

class GURL;

namespace base {
class CancelableTaskTracker;
}

namespace sessions {

class LiveTab;
class LiveTabContext;
struct SessionWindow;

// Callback from TabRestoreServiceClient::GetLastSession.
// The second parameter is the id of the window that was last active.
using GetLastSessionCallback =
    base::Callback<void(std::vector<std::unique_ptr<SessionWindow>>,
                        SessionID::id_type)>;

// A client interface that needs to be supplied to the tab restore service by
// the embedder.
class SESSIONS_EXPORT TabRestoreServiceClient {
 public:
  virtual ~TabRestoreServiceClient();

  // Creates a LiveTabContext instance that is associated with |app_name|. May
  // return nullptr (e.g., if the embedder does not support LiveTabContext
  // functionality).
  virtual LiveTabContext* CreateLiveTabContext(const std::string& app_name) = 0;

  // Returns the LiveTabContext instance that is associated with
  // |tab|, or null if there is no such instance.
  virtual LiveTabContext* FindLiveTabContextForTab(const LiveTab* tab) = 0;

  // Returns the LiveTabContext instance that is associated with |desired_id|,
  // or null if there is no such instance.
  virtual LiveTabContext* FindLiveTabContextWithID(
      SessionID::id_type desired_id) = 0;

  // Returns whether a given URL should be tracked for restoring.
  virtual bool ShouldTrackURLForRestore(const GURL& url) = 0;

  // Returns the extension app ID for the given LiveTab, or the empty string
  // if there is no such ID (e.g., if extensions are not supported by the
  // embedder).
  virtual std::string GetExtensionAppIDForTab(LiveTab* tab) = 0;

  // Returns the path of the directory to save state into.
  virtual base::FilePath GetPathToSaveTo() = 0;

  // Returns the URL that corresponds to the new tab page.
  virtual GURL GetNewTabURL() = 0;

  // Returns whether there is a previous session to load.
  virtual bool HasLastSession() = 0;

  // Fetches the contents of the last session, notifying the callback when
  // done. If the callback is supplied an empty vector of SessionWindows
  // it means the session could not be restored.
  virtual void GetLastSession(const GetLastSessionCallback& callback,
                              base::CancelableTaskTracker* tracker) = 0;

  // Called when a tab is restored. |url| is the URL that the tab is currently
  // visiting.
  virtual void OnTabRestored(const GURL& url);
};

}  // namespace sessions

#endif  // COMPONENTS_SESSIONS_CORE_TAB_RESTORE_SERVICE_CLIENT_H_
