// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_APP_LIST_APP_LIST_PRESENTER_SERVICE_H_
#define CHROME_BROWSER_UI_ASH_APP_LIST_APP_LIST_PRESENTER_SERVICE_H_

#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "ui/app_list/presenter/app_list_presenter.mojom.h"

class Profile;

namespace app_list {
class AppListPresenterImpl;
}

// A service providing the Mojo interface to manipulate the App List.
class AppListPresenterService : public app_list::mojom::AppListPresenter {
 public:
  explicit AppListPresenterService(Profile* profile);
  ~AppListPresenterService() override;

  // app_list::mojom::AppListPresenter:
  void Show(int64_t display_id) override;
  void Dismiss() override;
  void ToggleAppList(int64_t display_id) override;
  void StartVoiceInteractionSession() override;
  void UpdateYPositionAndOpacity(int y_position_in_screen,
                                 float background_opacity,
                                 bool is_end_gesture) override;

 private:
  app_list::AppListPresenterImpl* GetPresenter();

  Profile* const profile_;  // Owned by ProfileManager.
  mojo::Binding<app_list::mojom::AppListPresenter> binding_;

  DISALLOW_COPY_AND_ASSIGN(AppListPresenterService);
};

#endif  // CHROME_BROWSER_UI_ASH_APP_LIST_APP_LIST_PRESENTER_SERVICE_H_
