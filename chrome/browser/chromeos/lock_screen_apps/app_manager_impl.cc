// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/lock_screen_apps/app_manager_impl.h"

#include <memory>
#include <utility>

#include "apps/launcher.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string16.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/note_taking_helper.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/extensions/extension_assets_manager.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/common/pref_names.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/install_flag.h"
#include "extensions/common/api/app_runtime.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest.h"

namespace lock_screen_apps {

namespace {

using ExtensionCallback = base::Callback<void(
    const scoped_refptr<const extensions::Extension>& extension)>;

void InvokeCallbackOnTaskRunner(
    const ExtensionCallback& callback,
    const scoped_refptr<base::SequencedTaskRunner>& task_runner,
    const scoped_refptr<const extensions::Extension>& extension) {
  task_runner->PostTask(FROM_HERE, base::Bind(callback, extension));
}

// Loads extension with the provided |extension_id|, |location|, and
// |creation_flags| from the |version_dir| directory - directory to which the
// extension has been installed.
// |temp_copy| - scoped dir that contains the path from which extension
//     resources have been installed. Not used in this method, but passed around
//     to keep the directory in scope while the app is being installed.
// |callback| - callback to which the loaded app should be passed.
void LoadInstalledExtension(const std::string& extension_id,
                            extensions::Manifest::Location install_source,
                            int creation_flags,
                            std::unique_ptr<base::ScopedTempDir> temp_copy,
                            const ExtensionCallback& callback,
                            const base::FilePath& version_dir) {
  if (version_dir.empty()) {
    callback.Run(nullptr);
    return;
  }

  std::string error;
  scoped_refptr<const extensions::Extension> extension =
      extensions::file_util::LoadExtension(
          version_dir, extension_id, install_source, creation_flags, &error);
  callback.Run(extension);
}

// Installs |extension| as a copy of an extension unpacked at |original_path|
// into |target_install_dir|.
// |profile| is the profile to which the extension is being installed.
// |callback| - called with the app loaded from the final installation path.
void InstallExtensionCopy(
    const scoped_refptr<const extensions::Extension>& extension,
    const base::FilePath& original_path,
    const base::FilePath& target_install_dir,
    Profile* profile,
    const ExtensionCallback& callback) {
  base::FilePath target_dir = target_install_dir.Append(extension->id());
  base::FilePath install_temp_dir =
      extensions::file_util::GetInstallTempDir(target_dir);
  auto extension_temp_dir = base::MakeUnique<base::ScopedTempDir>();
  if (install_temp_dir.empty() ||
      !extension_temp_dir->CreateUniqueTempDirUnderPath(install_temp_dir)) {
    callback.Run(nullptr);
    return;
  }

  // Copy the original extension path to a temp path to prevent
  // ExtensionAssetsManager from deleting it (as InstallExtension renames the
  // source path to a new location under the target install directory).
  base::FilePath temp_copy =
      extension_temp_dir->GetPath().Append(original_path.BaseName());
  if (!base::CopyDirectory(original_path, temp_copy, true /* recursive */)) {
    callback.Run(nullptr);
    return;
  }

  // Note: |extension_temp_dir| is passed around to ensure it stays in scope
  // until the app installation is done.
  extensions::ExtensionAssetsManager::GetInstance()->InstallExtension(
      extension.get(), temp_copy, target_install_dir, profile,
      base::Bind(&LoadInstalledExtension, extension->id(),
                 extension->location(), extension->creation_flags(),
                 base::Passed(std::move(extension_temp_dir)), callback));
}

}  // namespace

AppManagerImpl::AppManagerImpl()
    : extensions_observer_(this),
      note_taking_helper_observer_(this),
      weak_ptr_factory_(this) {}

AppManagerImpl::~AppManagerImpl() = default;

void AppManagerImpl::Initialize(Profile* primary_profile,
                                Profile* lock_screen_profile) {
  DCHECK_EQ(State::kNotInitialized, state_);
  DCHECK(primary_profile);
  DCHECK(lock_screen_profile);
  DCHECK_NE(primary_profile, lock_screen_profile);
  // Do not use OTR profile for lock screen apps. This is important for
  // profile usage in |LaunchNoteTaking| - lock screen app background page runs
  // in original, non off the record profile, so the launch event has to be
  // dispatched to that profile. For other |lock_screen_profile_|, it makes no
  // difference - the profile is used to get browser context keyed services, all
  // of which redirect OTR profile to the original one.
  DCHECK(!lock_screen_profile->IsOffTheRecord());

  CHECK(!chromeos::ProfileHelper::Get()->GetUserByProfile(lock_screen_profile))
      << "Lock screen profile should not be associated with any users.";

  primary_profile_ = primary_profile;
  lock_screen_profile_ = lock_screen_profile;
  state_ = State::kInactive;

  note_taking_helper_observer_.Add(chromeos::NoteTakingHelper::Get());
}

void AppManagerImpl::Start(const base::Closure& note_taking_changed_callback) {
  DCHECK_NE(State::kNotInitialized, state_);

  note_taking_changed_callback_ = note_taking_changed_callback;
  extensions_observer_.Add(
      extensions::ExtensionRegistry::Get(primary_profile_));

  if (state_ == State::kActive)
    return;

  lock_screen_app_id_.clear();
  std::string app_id = FindLockScreenNoteTakingApp();
  if (app_id.empty()) {
    state_ = State::kAppUnavailable;
    return;
  }

  state_ = AddAppToLockScreenProfile(app_id);
  if (state_ == State::kActive || state_ == State::kActivating)
    lock_screen_app_id_ = app_id;
}

void AppManagerImpl::Stop() {
  DCHECK_NE(State::kNotInitialized, state_);

  note_taking_changed_callback_.Reset();
  extensions_observer_.RemoveAll();

  if (state_ == State::kInactive)
    return;

  RemoveAppFromLockScreenProfile(lock_screen_app_id_);
  lock_screen_app_id_.clear();
  state_ = State::kInactive;
}

bool AppManagerImpl::IsNoteTakingAppAvailable() const {
  return state_ == State::kActive && !lock_screen_app_id_.empty();
}

std::string AppManagerImpl::GetNoteTakingAppId() const {
  if (!IsNoteTakingAppAvailable())
    return std::string();
  return lock_screen_app_id_;
}

bool AppManagerImpl::LaunchNoteTaking() {
  if (!IsNoteTakingAppAvailable())
    return false;

  const extensions::ExtensionRegistry* extension_registry =
      extensions::ExtensionRegistry::Get(lock_screen_profile_);
  const extensions::Extension* app = extension_registry->GetExtensionById(
      lock_screen_app_id_, extensions::ExtensionRegistry::ENABLED);
  if (!app)
    return false;

  auto action_data =
      base::MakeUnique<extensions::api::app_runtime::ActionData>();
  action_data->action_type =
      extensions::api::app_runtime::ActionType::ACTION_TYPE_NEW_NOTE;
  action_data->is_lock_screen_action = base::MakeUnique<bool>(true);

  apps::LaunchPlatformAppWithAction(lock_screen_profile_, app,
                                    std::move(action_data), base::FilePath());
  return true;
}

void AppManagerImpl::OnExtensionLoaded(content::BrowserContext* browser_context,
                                       const extensions::Extension* extension) {
  if (extension->id() ==
      primary_profile_->GetPrefs()->GetString(prefs::kNoteTakingAppId)) {
    OnNoteTakingExtensionChanged();
  }
}

void AppManagerImpl::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  if (extension->id() == lock_screen_app_id_)
    OnNoteTakingExtensionChanged();
}

void AppManagerImpl::OnAvailableNoteTakingAppsUpdated() {}

void AppManagerImpl::OnPreferredNoteTakingAppUpdated(Profile* profile) {
  if (profile != primary_profile_)
    return;

  OnNoteTakingExtensionChanged();
}

void AppManagerImpl::OnNoteTakingExtensionChanged() {
  if (state_ == State::kInactive)
    return;

  std::string app_id = FindLockScreenNoteTakingApp();
  if (app_id == lock_screen_app_id_)
    return;

  RemoveAppFromLockScreenProfile(lock_screen_app_id_);
  lock_screen_app_id_.clear();

  state_ = AddAppToLockScreenProfile(app_id);
  if (state_ == State::kActive || state_ == State::kActivating)
    lock_screen_app_id_ = app_id;

  if (!note_taking_changed_callback_.is_null())
    note_taking_changed_callback_.Run();
}

std::string AppManagerImpl::FindLockScreenNoteTakingApp() const {
  // Note that lock screen does not currently support Android apps, so
  // it's enough to only check the state of the preferred Chrome app.
  std::unique_ptr<chromeos::NoteTakingAppInfo> note_taking_app =
      chromeos::NoteTakingHelper::Get()->GetPreferredChromeAppInfo(
          primary_profile_);

  if (!note_taking_app || !note_taking_app->preferred ||
      note_taking_app->lock_screen_support !=
          chromeos::NoteTakingLockScreenSupport::kEnabled) {
    return std::string();
  }

  return note_taking_app->app_id;
}

AppManagerImpl::State AppManagerImpl::AddAppToLockScreenProfile(
    const std::string& app_id) {
  extensions::ExtensionRegistry* primary_registry =
      extensions::ExtensionRegistry::Get(primary_profile_);
  const extensions::Extension* app =
      primary_registry->enabled_extensions().GetByID(app_id);
  if (!app)
    return State::kAppUnavailable;

  bool is_unpacked = extensions::Manifest::IsUnpackedLocation(app->location());

  // Unpacked apps in lock screen profile will be loaded from their original
  // file path, so their path will be the same as the primary profile app's.
  // For the rest, the app will be copied to a location in the lock screen
  // profile's extension install directory (using |InstallExtensionCopy|) - the
  // exact final path is not known at this point, and will be set as part of
  // |InstallExtensionCopy|.
  base::FilePath lock_profile_app_path =
      is_unpacked ? app->path() : base::FilePath();

  std::string error;
  scoped_refptr<extensions::Extension> lock_profile_app =
      extensions::Extension::Create(lock_profile_app_path, app->location(),
                                    *app->manifest()->value()->CreateDeepCopy(),
                                    app->creation_flags(), app->id(), &error);

  // While extension creation can fail in general, in this case the lock screen
  // profile extension creation arguments come from an app already installed in
  // a user profile. If the extension parameters were invalid, the app would not
  // exist in a user profile, and thus |app| would be nullptr, which is not the
  // case at this point.
  DCHECK(lock_profile_app);

  install_count_++;

  if (is_unpacked) {
    InstallAndEnableLockScreenAppInLockScreenProfile(lock_profile_app.get());
    return State::kActive;
  }

  ExtensionService* lock_screen_service =
      extensions::ExtensionSystem::Get(lock_screen_profile_)
          ->extension_service();

  lock_screen_service->GetFileTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(
          &InstallExtensionCopy, lock_profile_app, app->path(),
          lock_screen_service->install_directory(), lock_screen_profile_,
          base::Bind(&InvokeCallbackOnTaskRunner,
                     base::Bind(&AppManagerImpl::CompleteLockScreenAppInstall,
                                weak_ptr_factory_.GetWeakPtr(), install_count_),
                     base::ThreadTaskRunnerHandle::Get())));
  return State::kActivating;
}

void AppManagerImpl::CompleteLockScreenAppInstall(
    int install_id,
    const scoped_refptr<const extensions::Extension>& app) {
  // Bail out if the app manager is no longer waiting for this app's
  // installation - the copied resources will be cleaned up when the (ephemeral)
  // lock screen profile is destroyed.
  if (install_id != install_count_ || state_ != State::kActivating)
    return;

  if (app) {
    DCHECK_EQ(lock_screen_app_id_, app->id());
    InstallAndEnableLockScreenAppInLockScreenProfile(app.get());
    state_ = State::kActive;
  } else {
    state_ = State::kAppUnavailable;
  }

  if (!note_taking_changed_callback_.is_null())
    note_taking_changed_callback_.Run();
}

void AppManagerImpl::InstallAndEnableLockScreenAppInLockScreenProfile(
    const extensions::Extension* app) {
  ExtensionService* lock_screen_service =
      extensions::ExtensionSystem::Get(lock_screen_profile_)
          ->extension_service();

  lock_screen_service->OnExtensionInstalled(
      app, syncer::StringOrdinal(), extensions::kInstallFlagInstallImmediately);
  lock_screen_service->EnableExtension(app->id());
}

void AppManagerImpl::RemoveAppFromLockScreenProfile(const std::string& app_id) {
  if (app_id.empty())
    return;

  extensions::ExtensionRegistry* lock_screen_registry =
      extensions::ExtensionRegistry::Get(lock_screen_profile_);
  if (!lock_screen_registry->GetExtensionById(
          app_id, extensions::ExtensionRegistry::EVERYTHING)) {
    return;
  }

  base::string16 error;
  extensions::ExtensionSystem::Get(lock_screen_profile_)
      ->extension_service()
      ->UninstallExtension(app_id,
                           extensions::UNINSTALL_REASON_INTERNAL_MANAGEMENT,
                           base::Bind(&base::DoNothing), &error);
}

}  // namespace lock_screen_apps
