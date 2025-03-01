// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/permissions/extensions_api_permissions.h"

#include <stddef.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "extensions/common/api/declarative_net_request/constants.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/socket_permission.h"
#include "extensions/common/permissions/usb_device_permission.h"
#include "extensions/strings/grit/extensions_strings.h"

namespace extensions {

namespace {

template <typename T>
APIPermission* CreateAPIPermission(const APIPermissionInfo* permission) {
  return new T(permission);
}

}  // namespace

std::vector<std::unique_ptr<APIPermissionInfo>>
ExtensionsAPIPermissions::GetAllPermissions() const {
  // WARNING: If you are modifying a permission message in this list, be sure to
  // add the corresponding permission message rule to
  // ChromePermissionMessageRule::GetAllRules as well.
  APIPermissionInfo::InitInfo permissions_to_register[] = {
      {APIPermission::kAlarms, "alarms"},
      {APIPermission::kAlphaEnabled, "app.window.alpha"},
      {APIPermission::kAlwaysOnTopWindows, "app.window.alwaysOnTop"},
      {APIPermission::kAppView, "appview",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kAudio, "audio"},
      {APIPermission::kAudioCapture, "audioCapture"},
      {APIPermission::kBluetoothPrivate, "bluetoothPrivate",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kClipboard, "clipboard"},
      {APIPermission::kClipboardRead, "clipboardRead",
       APIPermissionInfo::kFlagSupportsContentCapabilities},
      {APIPermission::kClipboardWrite, "clipboardWrite",
       APIPermissionInfo::kFlagSupportsContentCapabilities},
      {APIPermission::kDeclarativeWebRequest, "declarativeWebRequest"},
      {APIPermission::kDiagnostics, "diagnostics",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kDisplaySource, "displaySource"},
      {APIPermission::kDns, "dns"},
      {APIPermission::kDocumentScan, "documentScan"},
      {APIPermission::kExtensionView, "extensionview",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kExternallyConnectableAllUrls,
       "externally_connectable.all_urls"},
      {APIPermission::kFullscreen, "app.window.fullscreen"},

      // The permission string for "fileSystem" is only shown when
      // "write" or "directory" is present. Read-only access is only
      // granted after the user has been shown a file or directory
      // chooser dialog and selected a file or directory. Selecting
      // the file or directory is considered consent to read it.
      {APIPermission::kFileSystem, "fileSystem"},
      {APIPermission::kFileSystemDirectory, "fileSystem.directory"},
      {APIPermission::kFileSystemRequestFileSystem,
       "fileSystem.requestFileSystem"},
      {APIPermission::kFileSystemRetainEntries, "fileSystem.retainEntries"},
      {APIPermission::kFileSystemWrite, "fileSystem.write"},

      {APIPermission::kHid, "hid"},
      {APIPermission::kImeWindowEnabled, "app.window.ime"},
      {APIPermission::kOverrideEscFullscreen,
       "app.window.fullscreen.overrideEsc"},
      {APIPermission::kIdle, "idle"},
      {APIPermission::kLockScreen, "lockScreen"},
      {APIPermission::kMediaPerceptionPrivate, "mediaPerceptionPrivate"},
      {APIPermission::kMetricsPrivate, "metricsPrivate",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kNetworkingConfig, "networking.config"},
      {APIPermission::kNetworkingOnc, "networking.onc"},
      {APIPermission::kNetworkingPrivate, "networkingPrivate",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kNewTabPageOverride, "newTabPageOverride",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kPower, "power"},
      {APIPermission::kPrinterProvider, "printerProvider"},
      {APIPermission::kSerial, "serial"},
      {APIPermission::kSocket, "socket",
       APIPermissionInfo::kFlagCannotBeOptional,
       &CreateAPIPermission<SocketPermission>},
      {APIPermission::kStorage, "storage"},
      {APIPermission::kSystemCpu, "system.cpu"},
      {APIPermission::kSystemMemory, "system.memory"},
      {APIPermission::kSystemNetwork, "system.network"},
      {APIPermission::kSystemDisplay, "system.display"},
      {APIPermission::kSystemStorage, "system.storage"},
      {APIPermission::kU2fDevices, "u2fDevices"},
      {APIPermission::kUnlimitedStorage, "unlimitedStorage",
       APIPermissionInfo::kFlagCannotBeOptional |
           APIPermissionInfo::kFlagSupportsContentCapabilities},
      {APIPermission::kUsb, "usb", APIPermissionInfo::kFlagNone},
      {APIPermission::kUsbDevice, "usbDevices", APIPermissionInfo::kFlagNone,
       &CreateAPIPermission<UsbDevicePermission>},
      {APIPermission::kVideoCapture, "videoCapture"},
      {APIPermission::kVirtualKeyboard, "virtualKeyboard"},
      {APIPermission::kVpnProvider, "vpnProvider",
       APIPermissionInfo::kFlagCannotBeOptional},
      // NOTE(kalman): This is provided by a manifest property but needs to
      // appear in the install permission dialogue, so we need a fake
      // permission for it. See http://crbug.com/247857.
      {APIPermission::kWebConnectable, "webConnectable",
       APIPermissionInfo::kFlagCannotBeOptional |
           APIPermissionInfo::kFlagInternal},
      {APIPermission::kWebRequest, "webRequest"},
      {APIPermission::kWebRequestBlocking, "webRequestBlocking"},
      {APIPermission::kDeclarativeNetRequest,
       declarative_net_request::kAPIPermission},
      {APIPermission::kWebView, "webview",
       APIPermissionInfo::kFlagCannotBeOptional},
      {APIPermission::kWindowShape, "app.window.shape"},
  };

  std::vector<std::unique_ptr<APIPermissionInfo>> permissions;

  for (size_t i = 0; i < arraysize(permissions_to_register); ++i)
    permissions.push_back(
        base::WrapUnique(new APIPermissionInfo(permissions_to_register[i])));
  return permissions;
}

}  // namespace extensions
