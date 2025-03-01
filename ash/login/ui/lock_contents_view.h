// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_LOGIN_UI_CONTENTS_VIEW_H_
#define ASH_LOGIN_UI_CONTENTS_VIEW_H_

#include "ash/ash_export.h"
#include "ash/login/ui/login_data_dispatcher.h"
#include "base/macros.h"
#include "base/scoped_observer.h"
#include "ui/display/display_observer.h"
#include "ui/display/screen.h"
#include "ui/views/view.h"

namespace views {
class BoundsAnimator;
class BoxLayout;
class ScrollView;
}  // namespace views

namespace ash {

class LoginAuthUserView;
class LoginUserView;

// LockContentsView hosts the root view for the lock screen. All other lock
// screen views are embedded within this one. LockContentsView is per-display,
// but it is always shown on the primary display. There is only one instance
// at a time.
class ASH_EXPORT LockContentsView : public views::View,
                                    public LoginDataDispatcher::Observer,
                                    public display::DisplayObserver {
 public:
  // TestApi is used for tests to get internal implementation details.
  class ASH_EXPORT TestApi {
   public:
    explicit TestApi(LockContentsView* view);
    ~TestApi();

    LoginAuthUserView* auth_user_view() const;
    const std::vector<LoginUserView*>& user_views() const;

   private:
    LockContentsView* const view_;
  };

  explicit LockContentsView(LoginDataDispatcher* data_dispatcher);
  ~LockContentsView() override;

  // views::View:
  void Layout() override;
  void AddedToWidget() override;

  // LoginDataDispatcher::Observer:
  void OnUsersChanged(
      const std::vector<ash::mojom::UserInfoPtr>& users) override;
  void OnPinEnabledForUserChanged(const AccountId& user, bool enabled) override;

  // display::DisplayObserver:
  void OnDisplayMetricsChanged(const display::Display& display,
                               uint32_t changed_metrics) override;

 private:
  struct UserState {
    explicit UserState(AccountId account_id);

    AccountId account_id;
    bool show_pin = false;
  };

  using OnRotate = base::RepeatingCallback<void(bool landscape)>;

  // 1-2 users.
  void CreateLowDensityLayout(
      const std::vector<ash::mojom::UserInfoPtr>& users);
  // 3-6 users.
  void CreateMediumDensityLayout(
      const std::vector<ash::mojom::UserInfoPtr>& users);
  // 7+ users.
  void CreateHighDensityLayout(
      const std::vector<ash::mojom::UserInfoPtr>& users);

  // Lay out the entire view. This is called when the view is attached to a
  // widget and when the screen is rotated.
  void DoLayout();

  // Creates a new view with |landscape| and |portrait| preferred sizes.
  // |landscape| and |portrait| specify the width of the preferred size; the
  // height is an arbitrary non-zero value. The correct size is chosen
  // dynamically based on screen orientation. The view will respond to
  // orientation changes.
  views::View* MakeOrientationViewWithWidths(int landscape, int portrait);

  // Adds |on_rotate| to |rotation_actions_| and immediately executes it with
  // the current rotation.
  void AddRotationAction(const OnRotate& on_rotate);

  // Tries to lookup the stored state for |user|. Returns an unowned pointer
  // that is invalidated whenver |users_| changes.
  UserState* FindStateForUser(const AccountId& user);

  void UpdateAuthMethodsForAuthUser(bool animate);

  std::vector<UserState> users_;

  LoginDataDispatcher* const data_dispatcher_;  // Unowned.

  std::unique_ptr<views::BoundsAnimator> auth_user_view_animator_;

  LoginAuthUserView* auth_user_view_;
  // All non-auth users; |auth_user_view_| is not contained in this list.
  std::vector<LoginUserView*> user_views_;
  views::ScrollView* scroller_;
  views::View* background_;
  views::BoxLayout* root_layout_;

  // Actions that should be executed when rotation changes. A full layout pass
  // is performed after all actions are executed.
  std::vector<OnRotate> rotation_actions_;

  ScopedObserver<display::Screen, display::DisplayObserver> display_observer_;

  DISALLOW_COPY_AND_ASSIGN(LockContentsView);
};

}  // namespace ash

#endif  // ASH_LOGIN_UI_CONTENTS_VIEW_H_
