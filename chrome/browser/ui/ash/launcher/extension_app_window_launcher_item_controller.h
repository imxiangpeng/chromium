// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LAUNCHER_EXTENSION_APP_WINDOW_LAUNCHER_ITEM_CONTROLLER_H_
#define CHROME_BROWSER_UI_ASH_LAUNCHER_EXTENSION_APP_WINDOW_LAUNCHER_ITEM_CONTROLLER_H_

#include <string>

#include "base/macros.h"
#include "chrome/browser/ui/ash/launcher/app_window_launcher_item_controller.h"

namespace extensions {
class AppWindow;
}

// Shelf item delegate for extension app windows.
class ExtensionAppWindowLauncherItemController
    : public AppWindowLauncherItemController {
 public:
  explicit ExtensionAppWindowLauncherItemController(
      const ash::ShelfID& shelf_id);

  ~ExtensionAppWindowLauncherItemController() override;

  void AddAppWindow(extensions::AppWindow* app_window);

  // aura::WindowObserver overrides:
  void OnWindowTitleChanged(aura::Window* window) override;

  // AppWindowLauncherItemController:
  ash::MenuItemList GetAppMenuItems(int event_flags) override;
  void ExecuteCommand(uint32_t command_id, int32_t event_flags) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExtensionAppWindowLauncherItemController);
};

#endif  // CHROME_BROWSER_UI_ASH_LAUNCHER_EXTENSION_APP_WINDOW_LAUNCHER_ITEM_CONTROLLER_H_
