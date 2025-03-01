// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/metrics/user_metrics_recorder.h"

#include <memory>

#include "ash/login_status.h"
#include "ash/metrics/user_metrics_recorder_test_api.h"
#include "ash/public/cpp/config.h"
#include "ash/public/cpp/shelf_model.h"
#include "ash/session/session_controller.h"
#include "ash/session/test_session_controller_client.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/test/histogram_tester.h"

using session_manager::SessionState;

namespace ash {
namespace {

const char kAsh_NumberOfVisibleWindowsInPrimaryDisplay[] =
    "Ash.NumberOfVisibleWindowsInPrimaryDisplay";

const char kAsh_ActiveWindowShowTypeOverTime[] =
    "Ash.ActiveWindowShowTypeOverTime";

const char kAsh_Shelf_NumberOfItems[] = "Ash.Shelf.NumberOfItems";

const char kAsh_Shelf_NumberOfPinnedItems[] = "Ash.Shelf.NumberOfPinnedItems";

const char kAsh_Shelf_NumberOfUnpinnedItems[] =
    "Ash.Shelf.NumberOfUnpinnedItems";

}  // namespace

// Test fixture for the UserMetricsRecorder class. The tests manage their own
// session state.
class UserMetricsRecorderTest : public NoSessionAshTestBase {
 public:
  UserMetricsRecorderTest() = default;
  ~UserMetricsRecorderTest() override = default;

  UserMetricsRecorderTestAPI& test_api() { return test_api_; }

  base::HistogramTester& histograms() { return histograms_; }

 private:
  // Test API to access private members of the test target.
  UserMetricsRecorderTestAPI test_api_;

  // Histogram value verifier.
  base::HistogramTester histograms_;

  DISALLOW_COPY_AND_ASSIGN(UserMetricsRecorderTest);
};

// Verifies the return value of IsUserInActiveDesktopEnvironment() for the
// different login status values.
TEST_F(UserMetricsRecorderTest, VerifyIsUserInActiveDesktopEnvironmentValues) {
  SessionController* session = Shell::Get()->session_controller();

  // Environment is not active before login.
  ASSERT_FALSE(session->IsActiveUserSessionStarted());
  EXPECT_FALSE(test_api().IsUserInActiveDesktopEnvironment());

  // Environment is active after login.
  SetSessionStarted(true);
  ASSERT_TRUE(session->IsActiveUserSessionStarted());
  EXPECT_TRUE(test_api().IsUserInActiveDesktopEnvironment());

  // Environment is not active when screen is locked.
  TestSessionControllerClient* client = GetSessionControllerClient();
  client->SetSessionState(SessionState::LOCKED);
  ASSERT_TRUE(session->IsScreenLocked());
  EXPECT_FALSE(test_api().IsUserInActiveDesktopEnvironment());

  // Kiosk logins are not considered active.
  client->Reset();
  client->AddUserSession("app@kiosk-apps.device-local.localhost",
                         user_manager::USER_TYPE_KIOSK_APP);
  client->SetSessionState(session_manager::SessionState::ACTIVE);
  EXPECT_FALSE(test_api().IsUserInActiveDesktopEnvironment());

  // Arc kiosk logins are not considered active.
  client->Reset();
  client->AddUserSession("app@arc-kiosk-apps.device-local.localhost",
                         user_manager::USER_TYPE_ARC_KIOSK_APP);
  client->SetSessionState(session_manager::SessionState::ACTIVE);
  EXPECT_FALSE(test_api().IsUserInActiveDesktopEnvironment());
}

// Verifies that the IsUserInActiveDesktopEnvironment() dependent stats are not
// recorded when a user is not active in a desktop environment.
TEST_F(UserMetricsRecorderTest,
       VerifyStatsRecordedWhenUserNotInActiveDesktopEnvironment) {
  ASSERT_FALSE(test_api().IsUserInActiveDesktopEnvironment());
  test_api().RecordPeriodicMetrics();

  histograms().ExpectTotalCount(kAsh_NumberOfVisibleWindowsInPrimaryDisplay, 0);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfItems, 0);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfPinnedItems, 0);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfUnpinnedItems, 0);
}

// Verifies that the IsUserInActiveDesktopEnvironment() dependent stats are
// recorded when a user is active in a desktop environment.
TEST_F(UserMetricsRecorderTest,
       VerifyStatsRecordedWhenUserInActiveDesktopEnvironment) {
  SetSessionStarted(true);
  ASSERT_TRUE(test_api().IsUserInActiveDesktopEnvironment());
  test_api().RecordPeriodicMetrics();

  histograms().ExpectTotalCount(kAsh_NumberOfVisibleWindowsInPrimaryDisplay, 1);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfItems, 1);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfPinnedItems, 1);
  histograms().ExpectTotalCount(kAsh_Shelf_NumberOfUnpinnedItems, 1);
}

// Verifies recording of stats which are always recorded by
// RecordPeriodicMetrics.
TEST_F(UserMetricsRecorderTest, VerifyStatsRecordedByRecordPeriodicMetrics) {
  SetSessionStarted(true);
  test_api().RecordPeriodicMetrics();

  histograms().ExpectTotalCount(kAsh_ActiveWindowShowTypeOverTime, 1);
}

// Verify the shelf item counts recorded by the
// UserMetricsRecorder::RecordPeriodicMetrics() method.
TEST_F(UserMetricsRecorderTest, ValuesRecordedByRecordShelfItemCounts) {
  // TODO: investigate failure in mash, http://crbug.com/695629.
  if (Shell::GetAshConfig() == Config::MASH)
    return;

  SetSessionStarted(true);

  // Make sure the shelf contains the app list launcher button.
  ShelfModel* shelf_model = Shell::Get()->shelf_model();
  ASSERT_EQ(2u, shelf_model->items().size());
  ASSERT_EQ(TYPE_APP_LIST, shelf_model->items()[0].type);
  ASSERT_EQ(TYPE_BROWSER_SHORTCUT, shelf_model->items()[1].type);

  ShelfItem shelf_item;
  shelf_item.type = ash::TYPE_PINNED_APP;
  shelf_item.id = ShelfID("app_id_1");
  shelf_model->Add(shelf_item);
  shelf_item.id = ShelfID("app_id_2");
  shelf_model->Add(shelf_item);

  shelf_item.type = ash::TYPE_APP;
  shelf_item.id = ShelfID("app_id_3");
  shelf_model->Add(shelf_item);
  shelf_item.id = ShelfID("app_id_4");
  shelf_model->Add(shelf_item);

  test_api().RecordPeriodicMetrics();
  histograms().ExpectBucketCount(kAsh_Shelf_NumberOfItems, 5, 1);
  histograms().ExpectBucketCount(kAsh_Shelf_NumberOfPinnedItems, 3, 1);
  histograms().ExpectBucketCount(kAsh_Shelf_NumberOfUnpinnedItems, 2, 1);
}

}  // namespace ash
