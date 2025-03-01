// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/lock_screen_apps/app_manager_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/memory/ptr_util.h"
#include "base/test/scoped_command_line.h"
#include "base/values.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_test_user_manager.h"
#include "chrome/browser/chromeos/note_taking_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/arc/arc_service_manager.h"
#include "components/arc/arc_session.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/event_router_factory.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/api/app_runtime.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::DictionaryBuilder;
using extensions::ListBuilder;

namespace lock_screen_apps {

namespace {

std::unique_ptr<arc::ArcSession> ArcSessionFactory() {
  ADD_FAILURE() << "Attempt to create arc session.";
  return nullptr;
}

class TestEventRouter : public extensions::EventRouter {
 public:
  explicit TestEventRouter(content::BrowserContext* context)
      : extensions::EventRouter(context,
                                extensions::ExtensionPrefs::Get(context)),
        context_(context) {}
  ~TestEventRouter() override = default;

  bool ExtensionHasEventListener(const std::string& extension_id,
                                 const std::string& event_name) const override {
    return event_name == extensions::api::app_runtime::OnLaunched::kEventName;
  }

  void BroadcastEvent(std::unique_ptr<extensions::Event> event) override {}

  void DispatchEventToExtension(
      const std::string& extension_id,
      std::unique_ptr<extensions::Event> event) override {
    if (event->event_name !=
        extensions::api::app_runtime::OnLaunched::kEventName) {
      return;
    }
    ASSERT_TRUE(event->event_args);
    const base::Value* arg_value = nullptr;
    ASSERT_TRUE(event->event_args->Get(0, &arg_value));
    ASSERT_TRUE(arg_value);
    if (event->restrict_to_browser_context)
      EXPECT_EQ(context_, event->restrict_to_browser_context);

    std::unique_ptr<extensions::api::app_runtime::LaunchData> launch_data =
        extensions::api::app_runtime::LaunchData::FromValue(*arg_value);
    ASSERT_TRUE(launch_data);
    ASSERT_TRUE(launch_data->action_data);
    EXPECT_EQ(extensions::api::app_runtime::ACTION_TYPE_NEW_NOTE,
              launch_data->action_data->action_type);
    ASSERT_TRUE(launch_data->action_data->is_lock_screen_action);
    EXPECT_TRUE(*launch_data->action_data->is_lock_screen_action);

    launched_apps_.push_back(extension_id);
  }

  const std::vector<std::string>& launched_apps() const {
    return launched_apps_;
  }

  void ClearLaunchedApps() { launched_apps_.clear(); }

 private:
  std::vector<std::string> launched_apps_;
  content::BrowserContext* context_;

  DISALLOW_COPY_AND_ASSIGN(TestEventRouter);
};

std::unique_ptr<KeyedService> TestEventRouterFactoryFunction(
    content::BrowserContext* profile) {
  return base::MakeUnique<TestEventRouter>(profile);
}

enum class TestAppLocation { kUnpacked, kInternal };

class LockScreenAppManagerImplTest
    : public testing::TestWithParam<TestAppLocation> {
 public:
  LockScreenAppManagerImplTest()
      : profile_manager_(TestingBrowserProcess::GetGlobal()) {}

  ~LockScreenAppManagerImplTest() override = default;

  void SetUp() override {
    // Initialize command line so chromeos::NoteTakingHelper thinks note taking
    // on lock screen is enabled.
    command_line_ = base::MakeUnique<base::test::ScopedCommandLine>();
    command_line_->GetProcessCommandLine()->InitFromArgv(
        {"", "--enable-lock-screen-apps", "--force-enable-stylus-tools"});

    ASSERT_TRUE(profile_manager_.SetUp());

    profile_ = profile_manager_.CreateTestingProfile("primary_profile");

    lock_screen_profile_ =
        profile_manager_.CreateTestingProfile(chrome::kInitialProfile);

    InitExtensionSystem(profile());
    InitExtensionSystem(lock_screen_profile()->GetOriginalProfile());

    // Initialize arc session manager - NoteTakingHelper expects it to be set.
    arc_session_manager_ = base::MakeUnique<arc::ArcSessionManager>(
        base::MakeUnique<arc::ArcSessionRunner>(
            base::Bind(&ArcSessionFactory)));

    chromeos::NoteTakingHelper::Initialize();
    chromeos::NoteTakingHelper::Get()->SetProfileWithEnabledLockScreenApps(
        profile());

    ResetAppManager();
  }

  void TearDown() override {
    // App manager has to be destroyed before NoteTakingHelper is shutdown - it
    // removes itself from the NoteTakingHelper observer list during its
    // destruction.
    app_manager_.reset();

    chromeos::NoteTakingHelper::Shutdown();
    extensions::ExtensionSystem::Get(profile())->Shutdown();
    extensions::ExtensionSystem::Get(lock_screen_profile())->Shutdown();
  }

  void InitExtensionSystem(Profile* profile) {
    extensions::TestExtensionSystem* extension_system =
        static_cast<extensions::TestExtensionSystem*>(
            extensions::ExtensionSystem::Get(profile));
    extension_system->CreateExtensionService(
        base::CommandLine::ForCurrentProcess(),
        profile->GetPath().Append("Extensions") /* install_directory */,
        false /* autoupdate_enabled */);
  }

  base::FilePath GetTestAppSourcePath(TestAppLocation location,
                                      Profile* profile,
                                      const std::string& id,
                                      const std::string& version) {
    switch (location) {
      case TestAppLocation::kUnpacked:
        return profile->GetPath().Append("Downloads").Append("app");
      case TestAppLocation::kInternal:
        return extensions::ExtensionSystem::Get(profile)
            ->extension_service()
            ->install_directory()
            .Append(id)
            .Append(version);
    }
    return base::FilePath();
  }

  base::FilePath GetLockScreenAppPath(const std::string& id,
                                      const std::string& version) {
    return GetLockScreenAppPathWithOriginalProfile(profile(), id, version);
  }

  base::FilePath GetLockScreenAppPathWithOriginalProfile(
      Profile* original_profile,
      const std::string& id,
      const std::string& version) {
    return GetLockScreenAppPathWithOriginalLocation(
        GetParam(), original_profile, id, version);
  }

  base::FilePath GetLockScreenAppPathWithOriginalLocation(
      TestAppLocation location,
      Profile* original_profile,
      const std::string& id,
      const std::string& version) {
    switch (location) {
      case TestAppLocation::kUnpacked:
        return original_profile->GetPath().Append("Downloads").Append("app");
      case TestAppLocation::kInternal:
        return extensions::ExtensionSystem::Get(lock_screen_profile())
            ->extension_service()
            ->install_directory()
            .Append(id)
            .Append(version + "_0");
    }
    return base::FilePath();
  }

  extensions::Manifest::Location GetAppLocation(TestAppLocation location) {
    switch (location) {
      case TestAppLocation::kUnpacked:
        return extensions::Manifest::UNPACKED;
      case TestAppLocation::kInternal:
        return extensions::Manifest::INTERNAL;
    }

    return extensions::Manifest::UNPACKED;
  }

  scoped_refptr<const extensions::Extension> CreateTestApp(
      const std::string& id,
      const std::string& version,
      bool supports_lock_screen) {
    return CreateTestAppInProfile(profile(), id, version, supports_lock_screen);
  }

  scoped_refptr<const extensions::Extension> CreateTestAppInProfile(
      Profile* profile,
      const std::string& id,
      const std::string& version,
      bool supports_lock_screen) {
    return CreateTestAppWithLocation(GetParam(), profile, id, version,
                                     supports_lock_screen);
  }

  scoped_refptr<const extensions::Extension> CreateTestAppWithLocation(
      TestAppLocation location,
      Profile* profile,
      const std::string& id,
      const std::string& version,
      bool supports_lock_screen) {
    std::unique_ptr<base::DictionaryValue> background =
        DictionaryBuilder()
            .Set("scripts", ListBuilder().Append("background.js").Build())
            .Build();
    std::unique_ptr<base::ListValue> action_handlers =
        ListBuilder()
            .Append(
                DictionaryBuilder()
                    .Set("action", "new_note")
                    .SetBoolean("enabled_on_lock_screen", supports_lock_screen)
                    .Build())
            .Build();

    DictionaryBuilder manifest_builder;
    manifest_builder.Set("name", "Note taking app")
        .Set("version", version)
        .Set("manifest_version", 2)
        .Set("app", DictionaryBuilder()
                        .Set("background", std::move(background))
                        .Build())
        .Set("permissions", ListBuilder().Append("lockScreen").Build())
        .Set("action_handlers", std::move(action_handlers));

    base::FilePath extension_path =
        GetTestAppSourcePath(location, profile, id, version);

    scoped_refptr<const extensions::Extension> extension =
        extensions::ExtensionBuilder()
            .SetManifest(manifest_builder.Build())
            .SetID(id)
            .SetPath(extension_path)
            .SetLocation(GetAppLocation(location))
            .Build();

    // Create the app path with required files - app manager *will* attempt to
    // load the app from the disk, so extension directory has to be present for
    // the load to succeed.
    base::File::Error error;
    if (!base::CreateDirectoryAndGetError(extension_path, &error)) {
      ADD_FAILURE() << "Failed to create path " << extension_path.value() << " "
                    << error;
      return nullptr;
    }

    JSONFileValueSerializer manifest_writer(
        extension_path.Append("manifest.json"));
    if (!manifest_writer.Serialize(*extension->manifest()->value())) {
      ADD_FAILURE() << "Failed to create manifest file";
      return nullptr;
    }

    if (base::WriteFile(extension_path.Append("background.js"), "{}", 2) != 2) {
      ADD_FAILURE() << "Failed to write background script file";
      return nullptr;
    }

    return extension;
  }

  TestingProfile* CreateSecondaryProfile() {
    TestingProfile* profile =
        profile_manager_.CreateTestingProfile("secondary_profile");
    InitExtensionSystem(profile);
    return profile;
  }

  scoped_refptr<const extensions::Extension> AddTestAppWithLockScreenSupport(
      Profile* profile,
      const std::string& app_id,
      const std::string& version,
      bool enable_on_lock_screen) {
    scoped_refptr<const extensions::Extension> app = CreateTestAppInProfile(
        profile, app_id, version, true /* supports_lock_screen*/);
    extensions::ExtensionSystem::Get(profile)
        ->extension_service()
        ->AddExtension(app.get());

    chromeos::NoteTakingHelper::Get()->SetPreferredApp(profile, app_id);
    chromeos::NoteTakingHelper::Get()->SetPreferredAppEnabledOnLockScreen(
        profile, enable_on_lock_screen);
    return app;
  }

  void InitializeAndStartAppManager(Profile* profile) {
    app_manager()->Initialize(profile, lock_screen_profile());
    app_manager()->Start(
        base::Bind(&LockScreenAppManagerImplTest::OnNoteTakingChanged,
                   base::Unretained(this)));
  }

  TestingProfile* profile() { return profile_; }
  TestingProfile* lock_screen_profile() { return lock_screen_profile_; }

  AppManager* app_manager() { return app_manager_.get(); }

  void ResetAppManager() { app_manager_ = base::MakeUnique<AppManagerImpl>(); }

  int note_taking_changed_count() const { return note_taking_changed_count_; }

  void ResetNoteTakingChangedCount() { note_taking_changed_count_ = 0; }

  // Waits for a round trip between file task runner used by the profile's
  // extension service and the main thread - used to ensure that all pending
  // file runner task finish,
  void RunExtensionServiceTaskRunner(Profile* profile) {
    base::RunLoop run_loop;
    extensions::ExtensionSystem::Get(profile)
        ->extension_service()
        ->GetFileTaskRunner()
        ->PostTaskAndReply(FROM_HERE, base::Bind(&base::DoNothing),
                           run_loop.QuitClosure());
    run_loop.Run();
  }

  bool IsInstallAsync() { return GetParam() != TestAppLocation::kUnpacked; }

  int NoteTakingChangedCountOnStart() { return IsInstallAsync() ? 1 : 0; }

 private:
  void OnNoteTakingChanged() { ++note_taking_changed_count_; }

  std::unique_ptr<base::test::ScopedCommandLine> command_line_;
  content::TestBrowserThreadBundle threads_;

  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  chromeos::ScopedTestUserManager user_manager_;

  TestingProfileManager profile_manager_;
  TestingProfile* profile_ = nullptr;
  TestingProfile* lock_screen_profile_ = nullptr;

  std::unique_ptr<arc::ArcServiceManager> arc_service_manager_;
  std::unique_ptr<arc::ArcSessionManager> arc_session_manager_;

  std::unique_ptr<AppManager> app_manager_;

  int note_taking_changed_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(LockScreenAppManagerImplTest);
};

}  // namespace

INSTANTIATE_TEST_CASE_P(Unpacked,
                        LockScreenAppManagerImplTest,
                        ::testing::Values(TestAppLocation::kUnpacked));
INSTANTIATE_TEST_CASE_P(Internal,
                        LockScreenAppManagerImplTest,
                        ::testing::Values(TestAppLocation::kInternal));

TEST_P(LockScreenAppManagerImplTest, StartAddsAppToTarget) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(note_taking_app->id(),
                                 note_taking_app->VersionString()),
            lock_app->path());

  app_manager()->Stop();

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, StartWhenLockScreenNotesNotEnabled) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          false /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  EXPECT_FALSE(lock_app);

  app_manager()->Stop();
  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, LockScreenNoteTakingDisabledWhileStarted) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(note_taking_app->id(),
                                 note_taking_app->VersionString()),
            lock_app->path());
  EXPECT_TRUE(base::PathExists(note_taking_app->path()));

  chromeos::NoteTakingHelper::Get()->SetPreferredAppEnabledOnLockScreen(
      profile(), false);

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());
  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  app_manager()->Stop();

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, LockScreenNoteTakingEnabledWhileStarted) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          false /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  chromeos::NoteTakingHelper::Get()->SetPreferredAppEnabledOnLockScreen(
      profile(), true);

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(note_taking_app->id(),
                                 note_taking_app->VersionString()),
            lock_app->path());
  EXPECT_TRUE(base::PathExists(note_taking_app->path()));

  app_manager()->Stop();

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, LockScreenNoteTakingChangedWhileStarted) {
  scoped_refptr<const extensions::Extension> dev_note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  scoped_refptr<const extensions::Extension> prod_note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(prod_note_taking_app->id(),
                                 prod_note_taking_app->VersionString()),
            lock_app->path());
  EXPECT_TRUE(base::PathExists(prod_note_taking_app->path()));

  chromeos::NoteTakingHelper::Get()->SetPreferredApp(
      profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId);

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kDevKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  // Verify prod app was unloaded from signin profile.
  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kDevKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);

  ASSERT_TRUE(lock_app);

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(dev_note_taking_app->id(),
                                 dev_note_taking_app->VersionString()),
            lock_app->path());

  app_manager()->Stop();
  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(dev_note_taking_app->path()));
  EXPECT_TRUE(base::PathExists(prod_note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, NoteTakingChangedToLockScreenSupported) {
  scoped_refptr<const extensions::Extension> dev_note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  scoped_refptr<const extensions::Extension> prod_note_taking_app =
      CreateTestAppInProfile(profile(),
                             chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             "1.0", false /* supports_lock_screen */);
  extensions::ExtensionSystem::Get(profile())
      ->extension_service()
      ->AddExtension(prod_note_taking_app.get());
  chromeos::NoteTakingHelper::Get()->SetPreferredApp(
      profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId);

  // Initialize app manager - the note taking should be disabled initially
  // because the preferred app (prod) is not enabled on lock screen.
  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());
  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(false, app_manager()->IsNoteTakingAppAvailable());

  // Setting dev app, which is enabled on lock screen, as preferred will enable
  // lock screen note taking,
  chromeos::NoteTakingHelper::Get()->SetPreferredApp(
      profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId);

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();
  // If test app is installed asynchronously. the app won't be enabled on
  // lock screen until extension service task runner tasks are run.
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();
  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kDevKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  // Verify the dev app copy is installed in the lock screen app profile.
  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kDevKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);
  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(dev_note_taking_app->id(),
                                 dev_note_taking_app->VersionString()),
            lock_app->path());

  // Verify the prod app was not coppied to the lock screen profile (for
  // unpacked apps, the lock screen extension path is the same as the original
  // app path, so it's expected to still exist).
  EXPECT_EQ(
      GetParam() == TestAppLocation::kUnpacked,
      base::PathExists(GetLockScreenAppPath(
          prod_note_taking_app->id(), prod_note_taking_app->VersionString())));

  app_manager()->Stop();

  // Stopping app manager will disable lock screen note taking.
  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  // Make sure original app paths are not deleted.
  EXPECT_TRUE(base::PathExists(dev_note_taking_app->path()));
  EXPECT_TRUE(base::PathExists(prod_note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, LockScreenNoteTakingReloadedWhileStarted) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);
  EXPECT_EQ("1.0", lock_app->VersionString());

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(note_taking_app->id(),
                                 note_taking_app->VersionString()),
            lock_app->path());
  EXPECT_TRUE(base::PathExists(note_taking_app->path()));

  extensions::ExtensionSystem::Get(profile())
      ->extension_service()
      ->UnloadExtension(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                        extensions::UnloadedExtensionReason::UPDATE);

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  // Verify prod app was unloaded from signin profile.
  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::EVERYTHING);
  EXPECT_FALSE(lock_app);

  // Add the app again.
  note_taking_app = CreateTestApp(
      chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.1", true);
  extensions::ExtensionSystem::Get(profile())
      ->extension_service()
      ->AddExtension(note_taking_app.get());

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(NoteTakingChangedCountOnStart(), note_taking_changed_count());
  ResetNoteTakingChangedCount();
  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);

  ASSERT_TRUE(lock_app);
  EXPECT_EQ("1.1", lock_app->VersionString());

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(GetLockScreenAppPath(note_taking_app->id(),
                                 note_taking_app->VersionString()),
            lock_app->path());

  app_manager()->Stop();
  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_TRUE(app_manager()->GetNoteTakingAppId().empty());

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest,
       NoteTakingAppChangeToUnpackedWhileActivating) {
  scoped_refptr<const extensions::Extension> initial_note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.1",
          true /* enable_on_lock_screen */);

  scoped_refptr<const extensions::Extension> final_note_taking_app =
      CreateTestAppWithLocation(TestAppLocation::kUnpacked, profile(),
                                chromeos::NoteTakingHelper::kDevKeepExtensionId,
                                "1.1", true /* enable_on_lock_screen */);
  extensions::ExtensionSystem::Get(profile())
      ->extension_service()
      ->AddExtension(final_note_taking_app.get());

  InitializeAndStartAppManager(profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  chromeos::NoteTakingHelper::Get()->SetPreferredApp(
      profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId);

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kDevKeepExtensionId,
            app_manager()->GetNoteTakingAppId());
  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(0, note_taking_changed_count());

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kDevKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kDevKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);
  EXPECT_EQ("1.1", lock_app->VersionString());

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(
      GetLockScreenAppPathWithOriginalLocation(
          TestAppLocation::kUnpacked, profile(), final_note_taking_app->id(),
          final_note_taking_app->VersionString()),
      lock_app->path());

  app_manager()->Stop();

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(initial_note_taking_app->path()));
  EXPECT_TRUE(base::PathExists(final_note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest,
       NoteTakingAppChangeToInternalWhileActivating) {
  scoped_refptr<const extensions::Extension> initial_note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.1",
          true /* enable_on_lock_screen */);

  scoped_refptr<const extensions::Extension> final_note_taking_app =
      CreateTestAppWithLocation(TestAppLocation::kInternal, profile(),
                                chromeos::NoteTakingHelper::kDevKeepExtensionId,
                                "1.1", true /* enable_on_lock_screen */);
  extensions::ExtensionSystem::Get(profile())
      ->extension_service()
      ->AddExtension(final_note_taking_app.get());

  InitializeAndStartAppManager(profile());

  EXPECT_EQ(0, note_taking_changed_count());
  EXPECT_EQ(!IsInstallAsync(), app_manager()->IsNoteTakingAppAvailable());

  chromeos::NoteTakingHelper::Get()->SetPreferredApp(
      profile(), chromeos::NoteTakingHelper::kDevKeepExtensionId);

  EXPECT_FALSE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();

  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_EQ(1, note_taking_changed_count());
  ResetNoteTakingChangedCount();

  EXPECT_TRUE(app_manager()->IsNoteTakingAppAvailable());
  EXPECT_EQ(chromeos::NoteTakingHelper::kDevKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kDevKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(lock_app);
  EXPECT_EQ("1.1", lock_app->VersionString());

  EXPECT_TRUE(base::PathExists(lock_app->path()));
  EXPECT_EQ(
      GetLockScreenAppPathWithOriginalLocation(
          TestAppLocation::kInternal, profile(), final_note_taking_app->id(),
          final_note_taking_app->VersionString()),
      lock_app->path());

  app_manager()->Stop();

  RunExtensionServiceTaskRunner(lock_screen_profile());
  RunExtensionServiceTaskRunner(profile());

  EXPECT_TRUE(base::PathExists(initial_note_taking_app->path()));
  EXPECT_TRUE(base::PathExists(final_note_taking_app->path()));
}

TEST_P(LockScreenAppManagerImplTest, ShutdownWhenStarted) {
  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.1",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  const extensions::Extension* lock_app =
      extensions::ExtensionRegistry::Get(lock_screen_profile())
          ->GetExtensionById(chromeos::NoteTakingHelper::kProdKeepExtensionId,
                             extensions::ExtensionRegistry::ENABLED);
  EXPECT_TRUE(lock_app);
}

TEST_P(LockScreenAppManagerImplTest, LaunchAppWhenEnabled) {
  TestEventRouter* event_router = static_cast<TestEventRouter*>(
      extensions::EventRouterFactory::GetInstance()->SetTestingFactoryAndUse(
          lock_screen_profile()->GetOriginalProfile(),
          &TestEventRouterFactoryFunction));
  ASSERT_TRUE(event_router);

  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          true /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  ASSERT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            app_manager()->GetNoteTakingAppId());

  EXPECT_TRUE(app_manager()->LaunchNoteTaking());

  ASSERT_EQ(1u, event_router->launched_apps().size());
  EXPECT_EQ(chromeos::NoteTakingHelper::kProdKeepExtensionId,
            event_router->launched_apps()[0]);
  event_router->ClearLaunchedApps();

  app_manager()->Stop();

  EXPECT_FALSE(app_manager()->LaunchNoteTaking());
  EXPECT_TRUE(event_router->launched_apps().empty());
}

TEST_P(LockScreenAppManagerImplTest, LaunchAppWhenNoLockScreenApp) {
  TestEventRouter* event_router = static_cast<TestEventRouter*>(
      extensions::EventRouterFactory::GetInstance()->SetTestingFactoryAndUse(
          lock_screen_profile()->GetOriginalProfile(),
          &TestEventRouterFactoryFunction));
  ASSERT_TRUE(event_router);

  scoped_refptr<const extensions::Extension> note_taking_app =
      AddTestAppWithLockScreenSupport(
          profile(), chromeos::NoteTakingHelper::kProdKeepExtensionId, "1.0",
          false /* enable_on_lock_screen */);

  InitializeAndStartAppManager(profile());
  RunExtensionServiceTaskRunner(lock_screen_profile());

  EXPECT_FALSE(app_manager()->LaunchNoteTaking());
  EXPECT_TRUE(event_router->launched_apps().empty());

  app_manager()->Stop();
  EXPECT_FALSE(app_manager()->LaunchNoteTaking());
  EXPECT_TRUE(event_router->launched_apps().empty());
}

}  // namespace lock_screen_apps
