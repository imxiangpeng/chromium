// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/shelf_item_delegate.h"
#include "ash/public/cpp/shelf_model.h"
#include "ash/shell.h"
#include "ash/wm/window_util.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/arc/arc_auth_notification.h"
#include "chrome/browser/chromeos/arc/arc_service_launcher.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/ui/app_list/app_list_controller_delegate.h"
#include "chrome/browser/ui/app_list/app_list_service.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service_factory.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/launcher/arc_app_deferred_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/arc_app_window_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "components/arc/arc_util.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/events/event_constants.h"

namespace mojo {

template <>
struct TypeConverter<arc::mojom::AppInfoPtr, arc::mojom::AppInfo> {
  static arc::mojom::AppInfoPtr Convert(const arc::mojom::AppInfo& app_info) {
    return app_info.Clone();
  }
};

template <>
struct TypeConverter<arc::mojom::ArcPackageInfoPtr,
                     arc::mojom::ArcPackageInfo> {
  static arc::mojom::ArcPackageInfoPtr Convert(
      const arc::mojom::ArcPackageInfo& package_info) {
    return package_info.Clone();
  }
};

template <>
struct TypeConverter<arc::mojom::ShortcutInfoPtr, arc::mojom::ShortcutInfo> {
  static arc::mojom::ShortcutInfoPtr Convert(
      const arc::mojom::ShortcutInfo& shortcut_info) {
    return shortcut_info.Clone();
  }
};

}  // namespace mojo

namespace {

constexpr char kTestAppName[] = "Test ARC App";
constexpr char kTestAppName2[] = "Test ARC App 2";
constexpr char kTestShortcutName[] = "Test Shortcut";
constexpr char kTestShortcutName2[] = "Test Shortcut 2";
constexpr char kTestAppPackage[] = "test.arc.app.package";
constexpr char kTestAppPackage2[] = "test.arc.app.package2";
constexpr char kTestAppPackage3[] = "test.arc.app.package3";
constexpr char kTestAppActivity[] = "test.arc.app.package.activity";
constexpr char kTestAppActivity2[] = "test.arc.gitapp.package.activity2";
constexpr char kTestShelfGroup[] = "shelf_group";
constexpr char kTestShelfGroup2[] = "shelf_group_2";
constexpr char kTestShelfGroup3[] = "shelf_group_3";
constexpr int kAppAnimatedThresholdMs = 100;

std::string GetTestApp1Id(const std::string& package_name) {
  return ArcAppListPrefs::GetAppId(package_name, kTestAppActivity);
}

std::string GetTestApp2Id(const std::string& package_name) {
  return ArcAppListPrefs::GetAppId(package_name, kTestAppActivity2);
}

std::vector<arc::mojom::AppInfoPtr> GetTestAppsList(
    const std::string& package_name,
    bool multi_app) {
  std::vector<arc::mojom::AppInfoPtr> apps;

  arc::mojom::AppInfoPtr app(arc::mojom::AppInfo::New());
  app->name = kTestAppName;
  app->package_name = package_name;
  app->activity = kTestAppActivity;
  app->sticky = false;
  apps.push_back(std::move(app));

  if (multi_app) {
    app = arc::mojom::AppInfo::New();
    app->name = kTestAppName2;
    app->package_name = package_name;
    app->activity = kTestAppActivity2;
    app->sticky = false;
    apps.push_back(std::move(app));
  }

  return apps;
}

class AppAnimatedWaiter {
 public:
  explicit AppAnimatedWaiter(const std::string& app_id) : app_id_(app_id) {}

  void Wait() {
    const base::TimeDelta threshold =
        base::TimeDelta::FromMilliseconds(kAppAnimatedThresholdMs);
    ArcAppDeferredLauncherController* controller =
        ChromeLauncherController::instance()->GetArcDeferredLauncher();
    while (controller->GetActiveTime(app_id_) < threshold) {
      base::RunLoop().RunUntilIdle();
    }
  }

 private:
  const std::string app_id_;
};

enum TestAction {
  TEST_ACTION_START,  // Start app on app appears.
  TEST_ACTION_EXIT,   // Exit Chrome during animation.
  TEST_ACTION_CLOSE,  // Close item during animation.
};

// Test parameters include TestAction and pin/unpin state.
typedef std::tr1::tuple<TestAction, bool> TestParameter;

TestParameter build_test_parameter[] = {
    TestParameter(TEST_ACTION_START, false),
    TestParameter(TEST_ACTION_EXIT, false),
    TestParameter(TEST_ACTION_CLOSE, false),
    TestParameter(TEST_ACTION_START, true),
};

std::string CreateIntentUriWithShelfGroup(const std::string& shelf_group_id) {
  return base::StringPrintf("#Intent;S.org.chromium.arc.shelf_group_id=%s;end",
                            shelf_group_id.c_str());
}

}  // namespace

class ArcAppLauncherBrowserTest : public ExtensionBrowserTest {
 public:
  ArcAppLauncherBrowserTest() {}
  ~ArcAppLauncherBrowserTest() override {}

 protected:
  // content::BrowserTestBase:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ExtensionBrowserTest::SetUpCommandLine(command_line);
    arc::SetArcAvailableCommandLineForTesting(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ExtensionBrowserTest::SetUpInProcessBrowserTestFixture();
    arc::ArcSessionManager::DisableUIForTesting();
    arc::ArcAuthNotification::DisableForTesting();
  }

  void SetUpOnMainThread() override {
    arc::SetArcPlayStoreEnabledForProfile(profile(), true);
  }

  void InstallTestApps(const std::string& package_name, bool multi_app) {
    app_host()->OnAppListRefreshed(GetTestAppsList(package_name, multi_app));

    std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
        app_prefs()->GetApp(GetTestApp1Id(package_name));
    ASSERT_TRUE(app_info);
    EXPECT_TRUE(app_info->ready);
    if (multi_app) {
      std::unique_ptr<ArcAppListPrefs::AppInfo> app_info2 =
          app_prefs()->GetApp(GetTestApp2Id(package_name));
      ASSERT_TRUE(app_info2);
      EXPECT_TRUE(app_info2->ready);
    }
  }

  std::string InstallShortcut(const std::string& name,
                              const std::string& shelf_group) {
    arc::mojom::ShortcutInfo shortcut;
    shortcut.name = name;
    shortcut.package_name = kTestAppPackage;
    shortcut.intent_uri = CreateIntentUriWithShelfGroup(shelf_group);
    const std::string shortcut_id =
        ArcAppListPrefs::GetAppId(shortcut.package_name, shortcut.intent_uri);
    app_host()->OnInstallShortcut(arc::mojom::ShortcutInfo::From(shortcut));
    base::RunLoop().RunUntilIdle();

    std::unique_ptr<ArcAppListPrefs::AppInfo> shortcut_info =
        app_prefs()->GetApp(shortcut_id);

    CHECK(shortcut_info);
    EXPECT_TRUE(shortcut_info->shortcut);
    EXPECT_EQ(kTestAppPackage, shortcut_info->package_name);
    EXPECT_EQ(shortcut.intent_uri, shortcut_info->intent_uri);
    return shortcut_id;
  }

  void SendPackageAdded(const std::string& package_name, bool package_synced) {
    arc::mojom::ArcPackageInfo package_info;
    package_info.package_name = package_name;
    package_info.package_version = 1;
    package_info.last_backup_android_id = 1;
    package_info.last_backup_time = 1;
    package_info.sync = package_synced;
    package_info.system = false;
    app_host()->OnPackageAdded(arc::mojom::ArcPackageInfo::From(package_info));

    base::RunLoop().RunUntilIdle();
  }

  void SendPackageUpdated(const std::string& package_name, bool multi_app) {
    app_host()->OnPackageAppListRefreshed(
        package_name, GetTestAppsList(package_name, multi_app));
  }

  void SendPackageRemoved(const std::string& package_name) {
    app_host()->OnPackageRemoved(package_name);
  }

  void SendInstallationStarted(const std::string& package_name) {
    app_host()->OnInstallationStarted(package_name);
    base::RunLoop().RunUntilIdle();
  }

  void SendInstallationFinished(const std::string& package_name, bool success) {
    arc::mojom::InstallationResult result;
    result.package_name = package_name;
    result.success = success;
    app_host()->OnInstallationFinished(
        arc::mojom::InstallationResultPtr(result.Clone()));
    base::RunLoop().RunUntilIdle();
  }

  void StartInstance() {
    if (!arc_session_manager()->profile()) {
      // This situation happens when StartInstance() is called after
      // StopInstance().
      // TODO(hidehiko): The emulation is not implemented correctly. Fix it.
      arc_session_manager()->SetProfile(profile());
      arc::ArcServiceLauncher::Get()->OnPrimaryUserProfilePrepared(profile());
    }
    app_instance_observer()->OnInstanceReady();
  }

  void StopInstance() {
    arc_session_manager()->Shutdown();
    app_instance_observer()->OnInstanceClosed();
  }

  ash::ShelfItemDelegate* GetShelfItemDelegate(const std::string& id) {
    ash::ShelfModel* model = ash::Shell::Get()->shelf_model();
    return model->GetShelfItemDelegate(ash::ShelfID(id));
  }

  ArcAppListPrefs* app_prefs() { return ArcAppListPrefs::Get(profile()); }

  // Returns as AppHost interface in order to access to private implementation
  // of the interface.
  arc::mojom::AppHost* app_host() { return app_prefs(); }

  // Returns as AppInstance observer interface in order to access to private
  // implementation of the interface.
  arc::InstanceHolder<arc::mojom::AppInstance>::Observer*
  app_instance_observer() {
    return app_prefs();
  }

  arc::ArcSessionManager* arc_session_manager() {
    return arc::ArcSessionManager::Get();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ArcAppLauncherBrowserTest);
};

class ArcAppDeferredLauncherBrowserTest
    : public ArcAppLauncherBrowserTest,
      public testing::WithParamInterface<TestParameter> {
 public:
  ArcAppDeferredLauncherBrowserTest() {}
  ~ArcAppDeferredLauncherBrowserTest() override {}

 protected:
  bool is_pinned() const { return std::tr1::get<1>(GetParam()); }

  TestAction test_action() const { return std::tr1::get<0>(GetParam()); }

 private:
  DISALLOW_COPY_AND_ASSIGN(ArcAppDeferredLauncherBrowserTest);
};

// This tests simulates normal workflow for starting ARC app in deferred mode.
IN_PROC_BROWSER_TEST_P(ArcAppDeferredLauncherBrowserTest, StartAppDeferred) {
  // Install app to remember existing apps.
  StartInstance();
  InstallTestApps(kTestAppPackage, false);
  SendPackageAdded(kTestAppPackage, false);

  ChromeLauncherController* controller = ChromeLauncherController::instance();
  const std::string app_id = GetTestApp1Id(kTestAppPackage);
  const ash::ShelfID shelf_id(app_id);
  if (is_pinned()) {
    controller->PinAppWithID(app_id);
    const ash::ShelfItem* item = controller->GetItem(shelf_id);
    EXPECT_EQ(base::UTF8ToUTF16(kTestAppName), item->title);
  } else {
    EXPECT_FALSE(controller->GetItem(shelf_id));
  }

  StopInstance();
  std::unique_ptr<ArcAppListPrefs::AppInfo> app_info =
      app_prefs()->GetApp(app_id);
  EXPECT_FALSE(app_info);

  // Restart instance. App should be taken from prefs but its state is non-ready
  // currently.
  StartInstance();
  app_info = app_prefs()->GetApp(app_id);
  ASSERT_TRUE(app_info);
  EXPECT_FALSE(app_info->ready);
  EXPECT_EQ(is_pinned(), controller->GetItem(shelf_id) != nullptr);

  // Launching non-ready ARC app creates item on shelf and spinning animation.
  arc::LaunchApp(profile(), app_id, ui::EF_LEFT_MOUSE_BUTTON);
  const ash::ShelfItem* item = controller->GetItem(shelf_id);
  EXPECT_EQ(base::UTF8ToUTF16(kTestAppName), item->title);
  AppAnimatedWaiter(app_id).Wait();

  switch (test_action()) {
    case TEST_ACTION_START:
      // Now simulates that ARC is started and app list is refreshed. This
      // should stop animation and delete icon from the shelf.
      InstallTestApps(kTestAppPackage, false);
      SendPackageAdded(kTestAppPackage, false);
      EXPECT_TRUE(controller->GetArcDeferredLauncher()
                      ->GetActiveTime(app_id)
                      .is_zero());
      EXPECT_EQ(is_pinned(), controller->GetItem(shelf_id) != nullptr);
      break;
    case TEST_ACTION_EXIT:
      // Just exit Chrome.
      break;
    case TEST_ACTION_CLOSE:
      {
        // Close item during animation.
        ash::ShelfItemDelegate* delegate = GetShelfItemDelegate(app_id);
        ASSERT_TRUE(delegate);
        delegate->Close();
        EXPECT_TRUE(controller->GetArcDeferredLauncher()
                        ->GetActiveTime(app_id)
                        .is_zero());
        EXPECT_EQ(is_pinned(), controller->GetItem(shelf_id) != nullptr);
      }
      break;
  }
}

INSTANTIATE_TEST_CASE_P(ArcAppDeferredLauncherBrowserTestInstance,
                        ArcAppDeferredLauncherBrowserTest,
                        ::testing::ValuesIn(build_test_parameter));

// This tests validates pin state on package update and remove.
IN_PROC_BROWSER_TEST_F(ArcAppLauncherBrowserTest, PinOnPackageUpdateAndRemove) {
  StartInstance();

  // Make use app list sync service is started. Normally it is started when
  // sycing is initialized.
  app_list::AppListSyncableServiceFactory::GetForProfile(profile())->GetModel();

  InstallTestApps(kTestAppPackage, true);
  SendPackageAdded(kTestAppPackage, false);

  const ash::ShelfID shelf_id1(GetTestApp1Id(kTestAppPackage));
  const ash::ShelfID shelf_id2(GetTestApp2Id(kTestAppPackage));
  ChromeLauncherController* controller = ChromeLauncherController::instance();
  controller->PinAppWithID(shelf_id1.app_id);
  controller->PinAppWithID(shelf_id2.app_id);
  EXPECT_TRUE(controller->GetItem(shelf_id1));
  EXPECT_TRUE(controller->GetItem(shelf_id2));

  // Package contains only one app. App list is not shown for updated package.
  SendPackageUpdated(kTestAppPackage, false);
  // Second pin should gone.
  EXPECT_TRUE(controller->GetItem(shelf_id1));
  EXPECT_FALSE(controller->GetItem(shelf_id2));

  // Package contains two apps. App list is not shown for updated package.
  SendPackageUpdated(kTestAppPackage, true);
  // Second pin should not appear.
  EXPECT_TRUE(controller->GetItem(shelf_id1));
  EXPECT_FALSE(controller->GetItem(shelf_id2));

  // Package removed.
  SendPackageRemoved(kTestAppPackage);
  // No pin is expected.
  EXPECT_FALSE(controller->GetItem(shelf_id1));
  EXPECT_FALSE(controller->GetItem(shelf_id2));
}

// This test validates that app list is shown on new package and not shown
// on package update.
IN_PROC_BROWSER_TEST_F(ArcAppLauncherBrowserTest, AppListShown) {
  StartInstance();
  AppListService* app_list_service = AppListService::Get();
  ASSERT_TRUE(app_list_service);

  EXPECT_FALSE(app_list_service->IsAppListVisible());

  SendInstallationStarted(kTestAppPackage);
  SendInstallationStarted(kTestAppPackage2);

  // New package is available. Show app list.
  SendInstallationFinished(kTestAppPackage, true);
  InstallTestApps(kTestAppPackage, false);
  SendPackageAdded(kTestAppPackage, true);
  EXPECT_TRUE(app_list_service->IsAppListVisible());

  app_list_service->DismissAppList();
  EXPECT_FALSE(app_list_service->IsAppListVisible());

  // Send package update event. App list is not shown.
  SendPackageAdded(kTestAppPackage, true);
  EXPECT_FALSE(app_list_service->IsAppListVisible());

  // Install next package from batch. Next new package is available.
  // Don't show app list.
  SendInstallationFinished(kTestAppPackage2, true);
  InstallTestApps(kTestAppPackage2, false);
  SendPackageAdded(kTestAppPackage2, true);
  EXPECT_FALSE(app_list_service->IsAppListVisible());

  // Run next installation batch. App list should be shown again.
  SendInstallationStarted(kTestAppPackage3);
  SendInstallationFinished(kTestAppPackage3, true);
  InstallTestApps(kTestAppPackage3, false);
  SendPackageAdded(kTestAppPackage3, true);
  EXPECT_TRUE(app_list_service->IsAppListVisible());
  app_list_service->DismissAppList();
}

// Test AppListControllerDelegate::IsAppOpen for ARC apps.
IN_PROC_BROWSER_TEST_F(ArcAppLauncherBrowserTest, IsAppOpen) {
  StartInstance();
  InstallTestApps(kTestAppPackage, false);
  SendPackageAdded(kTestAppPackage, true);
  const std::string app_id = GetTestApp1Id(kTestAppPackage);

  AppListService* service = AppListService::Get();
  AppListControllerDelegate* delegate = service->GetControllerDelegate();
  EXPECT_FALSE(delegate->IsAppOpen(app_id));
  arc::LaunchApp(profile(), app_id, ui::EF_LEFT_MOUSE_BUTTON);
  EXPECT_FALSE(delegate->IsAppOpen(app_id));
  // Simulate task creation so the app is marked as running/open.
  std::unique_ptr<ArcAppListPrefs::AppInfo> info = app_prefs()->GetApp(app_id);
  app_host()->OnTaskCreated(0, info->package_name, info->activity, info->name,
                            info->intent_uri);
  EXPECT_TRUE(delegate->IsAppOpen(app_id));
}

// Test Shelf Groups
IN_PROC_BROWSER_TEST_F(ArcAppLauncherBrowserTest, ShelfGroup) {
  StartInstance();
  InstallTestApps(kTestAppPackage, false);
  SendPackageAdded(kTestAppPackage, true);
  const std::string shorcut_id1 =
      InstallShortcut(kTestShortcutName, kTestShelfGroup);
  const std::string shorcut_id2 =
      InstallShortcut(kTestShortcutName2, kTestShelfGroup2);

  const std::string app_id = GetTestApp1Id(kTestAppPackage);
  std::unique_ptr<ArcAppListPrefs::AppInfo> info = app_prefs()->GetApp(app_id);
  ASSERT_TRUE(info);

  const std::string shelf_id1 =
      arc::ArcAppShelfId(kTestShelfGroup, app_id).ToString();
  const std::string shelf_id2 =
      arc::ArcAppShelfId(kTestShelfGroup2, app_id).ToString();
  const std::string shelf_id3 =
      arc::ArcAppShelfId(kTestShelfGroup3, app_id).ToString();

  // 1 task for group 1
  app_host()->OnTaskCreated(1, info->package_name, info->activity, info->name,
                            CreateIntentUriWithShelfGroup(kTestShelfGroup));

  ash::ShelfItemDelegate* delegate1 = GetShelfItemDelegate(shelf_id1);
  ASSERT_TRUE(delegate1);

  // 2 tasks for group 2
  app_host()->OnTaskCreated(2, info->package_name, info->activity, info->name,
                            CreateIntentUriWithShelfGroup(kTestShelfGroup2));

  ash::ShelfItemDelegate* delegate2 = GetShelfItemDelegate(shelf_id2);
  ASSERT_TRUE(delegate2);
  ASSERT_NE(delegate1, delegate2);

  app_host()->OnTaskCreated(3, info->package_name, info->activity, info->name,
                            CreateIntentUriWithShelfGroup(kTestShelfGroup2));

  ASSERT_EQ(delegate2, GetShelfItemDelegate(shelf_id2));

  // 2 tasks for group 3 which does not have shortcut.
  app_host()->OnTaskCreated(4, info->package_name, info->activity, info->name,
                            CreateIntentUriWithShelfGroup(kTestShelfGroup3));

  ash::ShelfItemDelegate* delegate3 = GetShelfItemDelegate(shelf_id3);
  ASSERT_TRUE(delegate3);
  ASSERT_NE(delegate1, delegate3);
  ASSERT_NE(delegate2, delegate3);

  app_host()->OnTaskCreated(5, info->package_name, info->activity, info->name,
                            CreateIntentUriWithShelfGroup(kTestShelfGroup3));

  ASSERT_EQ(delegate3, GetShelfItemDelegate(shelf_id3));

  // Destroy task #0, this kills shelf group 1
  app_host()->OnTaskDestroyed(1);
  EXPECT_FALSE(GetShelfItemDelegate(shelf_id1));

  // Destroy task #1, shelf group 2 is still alive
  app_host()->OnTaskDestroyed(2);
  EXPECT_EQ(delegate2, GetShelfItemDelegate(shelf_id2));
  // Destroy task #2, this kills shelf group 2
  app_host()->OnTaskDestroyed(3);
  EXPECT_FALSE(GetShelfItemDelegate(shelf_id2));

  // Disable ARC, this removes app and as result kills shelf group 3.
  arc::SetArcPlayStoreEnabledForProfile(profile(), false);
  EXPECT_FALSE(GetShelfItemDelegate(shelf_id3));
}
