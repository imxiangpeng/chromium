// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/launch_util.h"

#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_sync_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/extension.h"

#if defined(USE_ASH)
#include "ash/shell.h"  // nogncheck
#endif

namespace extensions {
namespace {

// A preference set by the the NTP to persist the desired launch container type
// used for apps.
const char kPrefLaunchType[] = "launchType";

}  // namespace

namespace launch_util {

// static
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(pref_names::kBookmarkAppCreationLaunchType,
                                LAUNCH_TYPE_WINDOW);
}

}  // namespace launch_util

LaunchType GetLaunchType(const ExtensionPrefs* prefs,
                         const Extension* extension) {
  LaunchType result = LAUNCH_TYPE_DEFAULT;

  int value = GetLaunchTypePrefValue(prefs, extension->id());
  if (value >= LAUNCH_TYPE_FIRST && value < NUM_LAUNCH_TYPES)
    result = static_cast<LaunchType>(value);

#if defined(OS_MACOSX)
  // Disable opening as window on Mac if:
  //  1. the extension isn't a platform app, AND
  //  2. the intended result is open as window, AND
  //  3. CanHostedAppsOpenInWindows() is false
  if (!extension->is_platform_app() && result == LAUNCH_TYPE_WINDOW &&
      !extensions::util::CanHostedAppsOpenInWindows()) {
    result = LAUNCH_TYPE_REGULAR;
  }
#else
  if (extensions::util::IsNewBookmarkAppsEnabled()) {
    if (result == LAUNCH_TYPE_PINNED)
      result = LAUNCH_TYPE_REGULAR;
    if (result == LAUNCH_TYPE_FULLSCREEN)
      result = LAUNCH_TYPE_WINDOW;
  }
#endif

  return result;
}

LaunchType GetLaunchTypePrefValue(const ExtensionPrefs* prefs,
                                  const std::string& extension_id) {
  int value = LAUNCH_TYPE_INVALID;
  return prefs->ReadPrefAsInteger(extension_id, kPrefLaunchType, &value)
      ? static_cast<LaunchType>(value) : LAUNCH_TYPE_INVALID;
}

void SetLaunchType(content::BrowserContext* context,
                   const std::string& extension_id,
                   LaunchType launch_type) {
  DCHECK(launch_type >= LAUNCH_TYPE_FIRST && launch_type < NUM_LAUNCH_TYPES);

  ExtensionPrefs::Get(context)->UpdateExtensionPref(
      extension_id, kPrefLaunchType,
      base::MakeUnique<base::Value>(static_cast<int>(launch_type)));

  // Sync the launch type.
  const Extension* extension =
      ExtensionRegistry::Get(context)
          ->GetExtensionById(extension_id, ExtensionRegistry::EVERYTHING);
  if (extension)
    ExtensionSyncService::Get(context)->SyncExtensionChangeIfNeeded(*extension);
}

LaunchContainer GetLaunchContainer(const ExtensionPrefs* prefs,
                                   const Extension* extension) {
  LaunchContainer manifest_launch_container =
      AppLaunchInfo::GetLaunchContainer(extension);

  const LaunchContainer kInvalidLaunchContainer =
      static_cast<LaunchContainer>(-1);

  LaunchContainer result = kInvalidLaunchContainer;

  if (manifest_launch_container == LAUNCH_CONTAINER_PANEL) {
    // Apps with app.launch.container = 'panel' should always respect the
    // manifest setting.
    result = manifest_launch_container;
  } else if (manifest_launch_container == LAUNCH_CONTAINER_TAB) {
    // Look for prefs that indicate the user's choice of launch container. The
    // app's menu on the NTP provides a UI to set this preference.
    LaunchType prefs_launch_type = GetLaunchType(prefs, extension);

    if (prefs_launch_type == LAUNCH_TYPE_WINDOW) {
      // If the pref is set to launch a window (or no pref is set, and
      // window opening is the default), make the container a window.
      result = LAUNCH_CONTAINER_WINDOW;
#if defined(USE_ASH)
    } else if (prefs_launch_type == LAUNCH_TYPE_FULLSCREEN) {
      // LAUNCH_TYPE_FULLSCREEN launches in a maximized app window in ash.
      // For desktop chrome AURA on all platforms we should open the
      // application in full screen mode in the current tab, on the same
      // lines as non AURA chrome.
      result = LAUNCH_CONTAINER_WINDOW;
#endif
    } else {
      // All other launch types (tab, pinned, fullscreen) are
      // implemented as tabs in a window.
      result = LAUNCH_CONTAINER_TAB;
    }
  } else {
    // If a new value for app.launch.container is added, logic for it should be
    // added here. LAUNCH_CONTAINER_WINDOW is not present because there is no
    // way to set it in a manifest.
    NOTREACHED() << manifest_launch_container;
  }

  // All paths should set |result|.
  if (result == kInvalidLaunchContainer) {
    DLOG(FATAL) << "Failed to set a launch container.";
    result = LAUNCH_CONTAINER_TAB;
  }

  return result;
}

bool HasPreferredLaunchContainer(const ExtensionPrefs* prefs,
                                 const Extension* extension) {
  int value = -1;
  LaunchContainer manifest_launch_container =
      AppLaunchInfo::GetLaunchContainer(extension);
  return manifest_launch_container == LAUNCH_CONTAINER_TAB &&
      prefs->ReadPrefAsInteger(extension->id(), kPrefLaunchType, &value);
}

bool LaunchesInWindow(content::BrowserContext* context,
                      const Extension* extension) {
  return GetLaunchType(ExtensionPrefs::Get(context), extension) ==
         LAUNCH_TYPE_WINDOW;
}

}  // namespace extensions
