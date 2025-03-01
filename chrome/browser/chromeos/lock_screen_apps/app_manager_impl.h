// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOCK_SCREEN_APPS_APP_MANAGER_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_LOCK_SCREEN_APPS_APP_MANAGER_IMPL_H_

#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "chrome/browser/chromeos/lock_screen_apps/app_manager.h"
#include "chrome/browser/chromeos/note_taking_helper.h"
#include "extensions/browser/extension_registry_observer.h"

class Profile;

namespace extensions {
class Extension;
class ExtensionRegistry;
}  // namespace extensions

namespace lock_screen_apps {

// The default implementation of lock_screen_apps::AppManager.
class AppManagerImpl : public AppManager,
                       public chromeos::NoteTakingHelper::Observer,
                       public extensions::ExtensionRegistryObserver {
 public:
  AppManagerImpl();
  ~AppManagerImpl() override;

  // AppManager implementation:
  void Initialize(Profile* primary_profile,
                  Profile* lock_screen_profile) override;
  void Start(const base::Closure& note_taking_changed_callback) override;
  void Stop() override;
  bool LaunchNoteTaking() override;
  bool IsNoteTakingAppAvailable() const override;
  std::string GetNoteTakingAppId() const override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;

  // chromeos::NoteTakingHelper::Observer:
  void OnAvailableNoteTakingAppsUpdated() override;
  void OnPreferredNoteTakingAppUpdated(Profile* profile) override;

 private:
  enum class State {
    // The manager has not yet been initialized.
    kNotInitialized,
    // The manager is initialized, but not started. The note taking app is
    // considered unset at this point, and cannot be launched.
    kInactive,
    // The manager is started. Lock screen note taking app, if set, is loaded
    // and ready to be launched.
    kActive,
    // The manager is started, but app is still being installed into the lock
    // screen apps profile.
    kActivating,
    // The manager is started, and there is no available lock screen enabled
    // app.
    kAppUnavailable,
  };

  // Called on UI thread when the lock screen app profile is initialized with
  // lock screen app assets. It completes the app installation to the lock
  // screen app profile.
  // |app| - the installing app. Cann be nullptr in case the app assets
  //     installation failed.
  void CompleteLockScreenAppInstall(
      int install_id,
      const scoped_refptr<const extensions::Extension>& app);

  // Installs app to the lock screen profile's extension service and enables
  // the app.
  void InstallAndEnableLockScreenAppInLockScreenProfile(
      const extensions::Extension* app);

  // Called when note taking related prefs change.
  void OnNoteTakingExtensionChanged();

  // Gets the currently enabled lock screen note taking app, is one is selected.
  // If no such app exists, returns an empty string.
  std::string FindLockScreenNoteTakingApp() const;

  // Starts installing the lock screen note taking app to the lock screen
  // profile.
  // Returns the state to which the app manager should move as a result of this
  // method.
  State AddAppToLockScreenProfile(const std::string& app_id);

  // Uninstalls lock screen note taking app from the lock screen profile.
  void RemoveAppFromLockScreenProfile(const std::string& app_id);

  Profile* primary_profile_ = nullptr;
  Profile* lock_screen_profile_ = nullptr;

  State state_ = State::kNotInitialized;
  std::string lock_screen_app_id_;

  ScopedObserver<extensions::ExtensionRegistry,
                 extensions::ExtensionRegistryObserver>
      extensions_observer_;

  ScopedObserver<chromeos::NoteTakingHelper,
                 chromeos::NoteTakingHelper::Observer>
      note_taking_helper_observer_;

  base::Closure note_taking_changed_callback_;

  // Counts app installs. Passed to app install callback as install request
  // identifier to determine whether the completed install is stale.
  int install_count_ = 0;

  base::WeakPtrFactory<AppManagerImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AppManagerImpl);
};

}  // namespace lock_screen_apps

#endif  // CHROME_BROWSER_CHROMEOS_LOCK_SCREEN_APPS_APP_MANAGER_IMPL_H_
