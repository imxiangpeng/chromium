// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The passwords in the tests below are all empty because PasswordStoreDefault
// does not store the actual passwords on OS X (they are stored in the Keychain
// instead). We could special-case it, but it is easier to just have empty
// passwords. This will not be needed anymore if crbug.com/466638 is fixed.

#include <stddef.h>

#include <utility>

#include "base/bind.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/os_crypt/os_crypt_mocker.h"
#include "components/password_manager/core/browser/android_affiliation/affiliated_match_helper.h"
#include "components/password_manager/core/browser/android_affiliation/affiliation_service.h"
#include "components/password_manager/core/browser/android_affiliation/mock_affiliated_match_helper.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_reuse_detector.h"
#include "components/password_manager/core/browser/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store_default.h"
#include "components/password_manager/core/browser/password_store_signin_notifier.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using autofill::PasswordForm;
using base::WaitableEvent;
using testing::_;
using testing::DoAll;
using testing::WithArg;

namespace password_manager {

namespace {

constexpr const char kTestWebRealm1[] = "https://one.example.com/";
constexpr const char kTestWebOrigin1[] = "https://one.example.com/origin";
constexpr const char kTestWebRealm2[] = "https://two.example.com/";
constexpr const char kTestWebOrigin2[] = "https://two.example.com/origin";
constexpr const char kTestWebRealm3[] = "https://three.example.com/";
constexpr const char kTestWebOrigin3[] = "https://three.example.com/origin";
constexpr const char kTestWebRealm5[] = "https://five.example.com/";
constexpr const char kTestWebOrigin5[] = "https://five.example.com/origin";
constexpr const char kTestPSLMatchingWebRealm[] = "https://psl.example.com/";
constexpr const char kTestPSLMatchingWebOrigin[] =
    "https://psl.example.com/origin";
constexpr const char kTestUnrelatedWebRealm[] = "https://notexample.com/";
constexpr const char kTestUnrelatedWebOrigin[] = "https:/notexample.com/origin";
constexpr const char kTestSameOrganizationNameWebRealm[] =
    "https://example.appspot.com/";
constexpr const char kTestSameOrganizationNameWebOrigin[] =
    "https://example.appspot.com/origin";
constexpr const char kTestInsecureWebRealm[] = "http://one.example.com/";
constexpr const char kTestInsecureWebOrigin[] = "http://one.example.com/origin";
constexpr const char kTestAndroidRealm1[] =
    "android://hash@com.example.android/";
constexpr const char kTestAndroidRealm2[] =
    "android://hash@com.example.two.android/";
constexpr const char kTestAndroidRealm3[] =
    "android://hash@com.example.three.android/";
constexpr const char kTestUnrelatedAndroidRealm[] =
    "android://hash@com.notexample.android/";
constexpr const char kTestAndroidName1[] = "Example Android App 1";
constexpr const char kTestAndroidIconURL1[] = "https://example.com/icon_1.png";
constexpr const char kTestAndroidName2[] = "Example Android App 2";
constexpr const char kTestAndroidIconURL2[] = "https://example.com/icon_2.png";

class MockPasswordStoreConsumer : public PasswordStoreConsumer {
 public:
  MOCK_METHOD1(OnGetPasswordStoreResultsConstRef,
               void(const std::vector<std::unique_ptr<PasswordForm>>&));

  // GMock cannot mock methods with move-only args.
  void OnGetPasswordStoreResults(
      std::vector<std::unique_ptr<PasswordForm>> results) override {
    OnGetPasswordStoreResultsConstRef(results);
  }
};

class MockPasswordStoreSigninNotifier : public PasswordStoreSigninNotifier {
 public:
  MOCK_METHOD1(SubscribeToSigninEvents, void(PasswordStore* store));
  MOCK_METHOD0(UnsubscribeFromSigninEvents, void());
};

class StartSyncFlareMock {
 public:
  StartSyncFlareMock() {}
  ~StartSyncFlareMock() {}

  MOCK_METHOD1(StartSyncFlare, void(syncer::ModelType));
};

}  // namespace

class PasswordStoreTest : public testing::Test {
 protected:
  PasswordStoreTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    // Mock OSCrypt. There is a call to OSCrypt on initializling
    // PasswordReuseDetector, so it should be mocked.
    OSCryptMocker::SetUp();
  }

  void TearDown() override {
    ASSERT_TRUE(temp_dir_.Delete());
    OSCryptMocker::TearDown();
  }

  base::FilePath test_login_db_file_path() const {
    return temp_dir_.GetPath().Append(FILE_PATH_LITERAL("login_test"));
  }

  base::test::ScopedTaskEnvironment scoped_task_environment_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(PasswordStoreTest, IgnoreOldWwwGoogleLogins) {
  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  const time_t cutoff = 1325376000;  // 00:00 Jan 1 2012 UTC
  static const PasswordFormData form_data[] = {
      // A form on https://www.google.com/ older than the cutoff. Will be
      // ignored.
      {PasswordForm::SCHEME_HTML, "https://www.google.com",
       "https://www.google.com/origin", "https://www.google.com/action",
       L"submit_element", L"username_element", L"password_element",
       L"username_value_1", L"", true, cutoff - 1},
      // A form on https://www.google.com/ older than the cutoff. Will be
      // ignored.
      {PasswordForm::SCHEME_HTML, "https://www.google.com",
       "https://www.google.com/origin", "https://www.google.com/action",
       L"submit_element", L"username_element", L"password_element",
       L"username_value_2", L"", true, cutoff - 1},
      // A form on https://www.google.com/ newer than the cutoff.
      {PasswordForm::SCHEME_HTML, "https://www.google.com",
       "https://www.google.com/origin", "https://www.google.com/action",
       L"submit_element", L"username_element", L"password_element",
       L"username_value_3", L"", true, cutoff + 1},
      // A form on https://accounts.google.com/ older than the cutoff.
      {PasswordForm::SCHEME_HTML, "https://accounts.google.com",
       "https://accounts.google.com/origin",
       "https://accounts.google.com/action", L"submit_element",
       L"username_element", L"password_element", L"username_value", L"", true,
       cutoff - 1},
      // A form on http://bar.example.com/ older than the cutoff.
      {PasswordForm::SCHEME_HTML, "http://bar.example.com",
       "http://bar.example.com/origin", "http://bar.example.com/action",
       L"submit_element", L"username_element", L"password_element",
       L"username_value", L"", true, cutoff - 1},
  };

  // Build the forms vector and add the forms to the store.
  std::vector<std::unique_ptr<PasswordForm>> all_forms;
  for (size_t i = 0; i < arraysize(form_data); ++i) {
    all_forms.push_back(FillPasswordFormWithData(form_data[i]));
    store->AddLogin(*all_forms.back());
  }
  base::RunLoop().RunUntilIdle();

  // We expect to get back only the "recent" www.google.com login.
  // Theoretically these should never actually exist since there are no longer
  // any login forms on www.google.com to save, but we technically allow them.
  // We should not get back the older saved password though.
  const PasswordStore::FormDigest www_google = {
      PasswordForm::SCHEME_HTML, "https://www.google.com", GURL()};
  std::vector<std::unique_ptr<PasswordForm>> www_google_expected;
  www_google_expected.push_back(base::MakeUnique<PasswordForm>(*all_forms[2]));

  // We should still get the accounts.google.com login even though it's older
  // than our cutoff - this is the new location of all Google login forms.
  const PasswordStore::FormDigest accounts_google = {
      PasswordForm::SCHEME_HTML, "https://accounts.google.com", GURL()};
  std::vector<std::unique_ptr<PasswordForm>> accounts_google_expected;
  accounts_google_expected.push_back(
      base::MakeUnique<PasswordForm>(*all_forms[3]));

  // Same thing for a generic saved login.
  const PasswordStore::FormDigest bar_example = {
      PasswordForm::SCHEME_HTML, "http://bar.example.com", GURL()};
  std::vector<std::unique_ptr<PasswordForm>> bar_example_expected;
  bar_example_expected.push_back(base::MakeUnique<PasswordForm>(*all_forms[4]));

  MockPasswordStoreConsumer consumer;
  testing::InSequence s;
  EXPECT_CALL(consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&www_google_expected)))
      .RetiresOnSaturation();
  EXPECT_CALL(consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&accounts_google_expected)))
      .RetiresOnSaturation();
  EXPECT_CALL(consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&bar_example_expected)))
      .RetiresOnSaturation();

  store->GetLogins(www_google, &consumer);
  store->GetLogins(accounts_google, &consumer);
  store->GetLogins(bar_example, &consumer);

  base::RunLoop().RunUntilIdle();

  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

TEST_F(PasswordStoreTest, StartSyncFlare) {
  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  StartSyncFlareMock mock;
  store->Init(
      base::Bind(&StartSyncFlareMock::StartSyncFlare, base::Unretained(&mock)),
      nullptr);
  {
    PasswordForm form;
    form.origin = GURL("http://accounts.google.com/LoginAuth");
    form.signon_realm = "http://accounts.google.com/";
    EXPECT_CALL(mock, StartSyncFlare(syncer::PASSWORDS));
    store->AddLogin(form);
    base::RunLoop().RunUntilIdle();
  }
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

TEST_F(PasswordStoreTest, GetLoginImpl) {
  /* clang-format off */
  static const PasswordFormData kTestCredential = {
      PasswordForm::SCHEME_HTML,
      kTestWebRealm1,
      kTestWebOrigin1,
      "", L"", L"username_element",  L"password_element",
      L"username_value",
      L"", true, 1};
  /* clang-format on */

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  // For each attribute in the primary key, create one form that mismatches on
  // that attribute.
  std::unique_ptr<PasswordForm> test_form(
      FillPasswordFormWithData(kTestCredential));
  std::unique_ptr<PasswordForm> mismatching_form_1(
      new PasswordForm(*test_form));
  mismatching_form_1->signon_realm = kTestPSLMatchingWebRealm;
  std::unique_ptr<PasswordForm> mismatching_form_2(
      new PasswordForm(*test_form));
  mismatching_form_2->origin = GURL(kTestPSLMatchingWebOrigin);
  std::unique_ptr<PasswordForm> mismatching_form_3(
      new PasswordForm(*test_form));
  mismatching_form_3->username_element = base::ASCIIToUTF16("other_element");
  std::unique_ptr<PasswordForm> mismatching_form_4(
      new PasswordForm(*test_form));
  mismatching_form_4->password_element = base::ASCIIToUTF16("other_element");
  std::unique_ptr<PasswordForm> mismatching_form_5(
      new PasswordForm(*test_form));
  mismatching_form_5->username_value =
      base::ASCIIToUTF16("other_username_value");

  store->AddLogin(*mismatching_form_1);
  store->AddLogin(*mismatching_form_2);
  store->AddLogin(*mismatching_form_3);
  store->AddLogin(*mismatching_form_4);
  store->AddLogin(*mismatching_form_5);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(store->GetLoginImpl(*test_form));

  store->AddLogin(*test_form);
  base::RunLoop().RunUntilIdle();
  std::unique_ptr<PasswordForm> returned_form = store->GetLoginImpl(*test_form);
  ASSERT_TRUE(returned_form);
  EXPECT_EQ(*test_form, *returned_form);

  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

TEST_F(PasswordStoreTest, UpdateLoginPrimaryKeyFields) {
  /* clang-format off */
  static const PasswordFormData kTestCredentials[] = {
      // The old credential.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm1,
       kTestWebOrigin1,
       "", L"", L"username_element_1",  L"password_element_1",
       L"username_value_1",
       L"", true, 1},
      // The new credential with different values for all primary key fields.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm2,
       kTestWebOrigin2,
       "", L"", L"username_element_2",  L"password_element_2",
       L"username_value_2",
       L"", true, 1}};
  /* clang-format on */

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  std::unique_ptr<PasswordForm> old_form(
      FillPasswordFormWithData(kTestCredentials[0]));
  store->AddLogin(*old_form);
  base::RunLoop().RunUntilIdle();

  MockPasswordStoreObserver mock_observer;
  store->AddObserver(&mock_observer);

  std::unique_ptr<PasswordForm> new_form(
      FillPasswordFormWithData(kTestCredentials[1]));
  EXPECT_CALL(mock_observer, OnLoginsChanged(testing::SizeIs(2u)));
  PasswordForm old_primary_key;
  old_primary_key.signon_realm = old_form->signon_realm;
  old_primary_key.origin = old_form->origin;
  old_primary_key.username_element = old_form->username_element;
  old_primary_key.username_value = old_form->username_value;
  old_primary_key.password_element = old_form->password_element;
  store->UpdateLoginWithPrimaryKey(*new_form, old_primary_key);
  base::RunLoop().RunUntilIdle();

  MockPasswordStoreConsumer mock_consumer;
  std::vector<std::unique_ptr<PasswordForm>> expected_forms;
  expected_forms.push_back(std::move(new_form));
  EXPECT_CALL(mock_consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&expected_forms)));
  store->GetAutofillableLogins(&mock_consumer);
  base::RunLoop().RunUntilIdle();

  store->RemoveObserver(&mock_observer);
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

// Verify that RemoveLoginsCreatedBetween() fires the completion callback after
// deletions have been performed and notifications have been sent out. Whether
// the correct logins are removed or not is verified in detail in other tests.
TEST_F(PasswordStoreTest, RemoveLoginsCreatedBetweenCallbackIsCalled) {
  /* clang-format off */
  static const PasswordFormData kTestCredential =
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm1,
       kTestWebOrigin1,
       "", L"", L"username_element_1",  L"password_element_1",
       L"username_value_1",
       L"", true, 1};
  /* clang-format on */

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::WrapUnique(new LoginDatabase(test_login_db_file_path()))));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  std::unique_ptr<PasswordForm> test_form(
      FillPasswordFormWithData(kTestCredential));
  store->AddLogin(*test_form);
  base::RunLoop().RunUntilIdle();

  MockPasswordStoreObserver mock_observer;
  store->AddObserver(&mock_observer);

  EXPECT_CALL(mock_observer, OnLoginsChanged(testing::SizeIs(1u)));
  store->RemoveLoginsCreatedBetween(
      base::Time::FromDoubleT(0), base::Time::FromDoubleT(2),
      base::MessageLoop::current()->QuitWhenIdleClosure());
  base::RunLoop().Run();
  testing::Mock::VerifyAndClearExpectations(&mock_observer);

  store->RemoveObserver(&mock_observer);
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

// When no Android applications are actually affiliated with the realm of the
// observed form, GetLoginsWithAffiliations() should still return the exact and
// PSL matching results, but not any stored Android credentials.
TEST_F(PasswordStoreTest, GetLoginsWithoutAffiliations) {
  /* clang-format off */
  static const PasswordFormData kTestCredentials[] = {
      // Credential that is an exact match of the observed form.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm1,
       kTestWebOrigin1,
       "", L"", L"",  L"",
       L"username_value_1",
       L"", true, 1},
      // Credential that is a PSL match of the observed form.
      {PasswordForm::SCHEME_HTML,
       kTestPSLMatchingWebRealm,
       kTestPSLMatchingWebOrigin,
       "", L"", L"",  L"",
       L"username_value_2",
       L"", true, 1},
      // Credential for an unrelated Android application.
      {PasswordForm::SCHEME_HTML,
       kTestUnrelatedAndroidRealm,
       "", "", L"", L"", L"",
       L"username_value_3",
       L"", true, 1}};
  /* clang-format on */

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::WrapUnique(new LoginDatabase(test_login_db_file_path()))));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  MockAffiliatedMatchHelper* mock_helper = new MockAffiliatedMatchHelper;
  store->SetAffiliatedMatchHelper(base::WrapUnique(mock_helper));

  std::vector<std::unique_ptr<PasswordForm>> all_credentials;
  for (size_t i = 0; i < arraysize(kTestCredentials); ++i) {
    all_credentials.push_back(FillPasswordFormWithData(kTestCredentials[i]));
    store->AddLogin(*all_credentials.back());
    base::RunLoop().RunUntilIdle();
  }

  PasswordStore::FormDigest observed_form = {
      PasswordForm::SCHEME_HTML, kTestWebRealm1, GURL(kTestWebOrigin1)};

  MockPasswordStoreConsumer mock_consumer;
  std::vector<std::unique_ptr<PasswordForm>> expected_results;
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[0]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[1]));
  for (const auto& result : expected_results) {
    if (result->signon_realm != observed_form.signon_realm)
      result->is_public_suffix_match = true;
  }

  std::vector<std::string> no_affiliated_android_realms;
  mock_helper->ExpectCallToGetAffiliatedAndroidRealms(
      observed_form, no_affiliated_android_realms);

  EXPECT_CALL(mock_consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&expected_results)));
  store->GetLogins(observed_form, &mock_consumer);
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

// There are 3 Android applications affiliated with the realm of the observed
// form, with the PasswordStore having credentials for two of these (even two
// credentials for one). GetLoginsWithAffiliations() should return the exact,
// and PSL matching credentials, and the credentials for these two Android
// applications, but not for the unaffiliated Android application.
TEST_F(PasswordStoreTest, GetLoginsWithAffiliations) {
  static constexpr const struct {
    PasswordFormData form_data;
    bool use_federated_login;
  } kTestCredentials[] = {
      // Credential that is an exact match of the observed form.
      {
          {PasswordForm::SCHEME_HTML, kTestWebRealm1, kTestWebOrigin1, "", L"",
           L"", L"", L"username_value_1", L"", true, 1},
          false,
      },
      // Credential that is a PSL match of the observed form.
      {
          {PasswordForm::SCHEME_HTML, kTestPSLMatchingWebRealm,
           kTestPSLMatchingWebOrigin, "", L"", L"", L"", L"username_value_2",
           L"", true, 1},
          false,
      },
      // Credential for an Android application affiliated with the realm of the
      // observed from.
      {
          {PasswordForm::SCHEME_HTML, kTestAndroidRealm1, "", "", L"", L"", L"",
           L"username_value_3", L"", true, 1},
          false,
      },
      // Second credential for the same Android application.
      {
          {PasswordForm::SCHEME_HTML, kTestAndroidRealm1, "", "", L"", L"", L"",
           L"username_value_3b", L"", true, 1},
          false,
      },
      // Third credential for the same application which is username-only.
      {
          {PasswordForm::SCHEME_USERNAME_ONLY, kTestAndroidRealm1, "", "", L"",
           L"", L"", L"username_value_3c", L"", true, 1},
          false,
      },
      // Credential for another Android application affiliated with the realm
      // of the observed from.
      {
          {PasswordForm::SCHEME_HTML, kTestAndroidRealm2, "", "", L"", L"", L"",
           L"username_value_4", L"", true, 1},
          false,
      },
      // Federated credential for this second Android application.
      {
          {PasswordForm::SCHEME_HTML, kTestAndroidRealm2, "", "", L"", L"", L"",
           L"username_value_4b", L"", true, 1},
          true,
      },
      // Credential for an unrelated Android application.
      {
          {PasswordForm::SCHEME_HTML, kTestUnrelatedAndroidRealm, "", "", L"",
           L"", L"", L"username_value_5", L"", true, 1},
          false,
      }};

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::WrapUnique(new LoginDatabase(test_login_db_file_path()))));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  MockAffiliatedMatchHelper* mock_helper = new MockAffiliatedMatchHelper;
  store->SetAffiliatedMatchHelper(base::WrapUnique(mock_helper));

  std::vector<std::unique_ptr<PasswordForm>> all_credentials;
  for (const auto& i : kTestCredentials) {
    all_credentials.push_back(
        FillPasswordFormWithData(i.form_data, i.use_federated_login));
    store->AddLogin(*all_credentials.back());
    base::RunLoop().RunUntilIdle();
  }

  PasswordStore::FormDigest observed_form = {
      PasswordForm::SCHEME_HTML, kTestWebRealm1, GURL(kTestWebOrigin1)};

  MockPasswordStoreConsumer mock_consumer;
  std::vector<std::unique_ptr<PasswordForm>> expected_results;
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[0]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[1]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[2]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[3]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[5]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*all_credentials[6]));

  for (const auto& result : expected_results) {
    if (result->signon_realm != observed_form.signon_realm &&
        !IsValidAndroidFacetURI(result->signon_realm))
      result->is_public_suffix_match = true;
    if (IsValidAndroidFacetURI(result->signon_realm))
      result->is_affiliation_based_match = true;
  }

  std::vector<std::string> affiliated_android_realms;
  affiliated_android_realms.push_back(kTestAndroidRealm1);
  affiliated_android_realms.push_back(kTestAndroidRealm2);
  affiliated_android_realms.push_back(kTestAndroidRealm3);
  mock_helper->ExpectCallToGetAffiliatedAndroidRealms(
      observed_form, affiliated_android_realms);

  EXPECT_CALL(mock_consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&expected_results)));

  store->GetLogins(observed_form, &mock_consumer);
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

// This test must use passwords, which are not stored on Mac, therefore the test
// is disabled on Mac. This should not be a huge issue as functionality in the
// platform-independent base class is tested. See also the file-level comment.
#if defined(OS_MACOSX)
#define MAYBE_UpdatePasswordsStoredForAffiliatedWebsites \
  DISABLED_UpdatePasswordsStoredForAffiliatedWebsites
#else
#define MAYBE_UpdatePasswordsStoredForAffiliatedWebsites \
  UpdatePasswordsStoredForAffiliatedWebsites
#endif

// When the password stored for an Android application is updated, credentials
// with the same username stored for affiliated web sites should also be updated
// automatically.
TEST_F(PasswordStoreTest, MAYBE_UpdatePasswordsStoredForAffiliatedWebsites) {
  const wchar_t kTestUsername[] = L"username_value_1";
  const wchar_t kTestOtherUsername[] = L"username_value_2";
  const wchar_t kTestOldPassword[] = L"old_password_value";
  const wchar_t kTestNewPassword[] = L"new_password_value";
  const wchar_t kTestOtherPassword[] = L"other_password_value";

  /* clang-format off */
  static const PasswordFormData kTestCredentials[] = {
      // The credential for the Android application that will be updated
      // explicitly so it can be tested if the changes are correctly propagated
      // to affiliated Web credentials.
      {PasswordForm::SCHEME_HTML,
       kTestAndroidRealm1,
       "", "", L"", L"", L"",
       kTestUsername,
       kTestOldPassword, true, 2},

      // --- Positive samples --- Credentials that the password update should be
      // automatically propagated to.

      // Credential for an affiliated web site with the same username.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm1,
       kTestWebOrigin1,
       "", L"", L"",  L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for another affiliated web site with the same username.
      // Although the password is different than the current/old password for
      // the Android application, it should be updated regardless.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm2,
       kTestWebOrigin2,
       "", L"", L"",  L"",
       kTestUsername,
       kTestOtherPassword, true, 1},

      // --- Negative samples --- Credentials that the password update should
      // not be propagated to.

      // Credential for another affiliated web site, but one that already has
      // the new password.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm3,
       kTestWebOrigin3,
       "", L"", L"",  L"",
       kTestUsername,
       kTestNewPassword, true, 1},
      // Credential for the HTTP version of an affiliated web site.
      {PasswordForm::SCHEME_HTML,
       kTestInsecureWebRealm,
       kTestInsecureWebOrigin,
       "", L"", L"",  L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for an affiliated web site, but with a different username.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm1,
       kTestWebOrigin1,
       "", L"", L"",  L"",
       kTestOtherUsername,
       kTestOldPassword, true, 1},
      // Credential for a web site that is a PSL match to a web sites affiliated
      // with the Android application.
      {PasswordForm::SCHEME_HTML,
       kTestPSLMatchingWebRealm,
       kTestPSLMatchingWebOrigin,
       "poisoned", L"poisoned", L"",  L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for an unrelated web site.
      {PasswordForm::SCHEME_HTML,
       kTestUnrelatedWebRealm,
       kTestUnrelatedWebOrigin,
       "", L"", L"",  L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for an affiliated Android application.
      {PasswordForm::SCHEME_HTML,
       kTestAndroidRealm2,
       "", "", L"", L"", L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for an unrelated Android application.
      {PasswordForm::SCHEME_HTML,
       kTestUnrelatedAndroidRealm,
       "", "", L"", L"", L"",
       kTestUsername,
       kTestOldPassword, true, 1},
      // Credential for an affiliated web site with the same username, but one
      // that was updated at the same time via Sync as the Android credential.
      {PasswordForm::SCHEME_HTML,
       kTestWebRealm5,
       kTestWebOrigin5,
       "", L"", L"",  L"",
       kTestUsername,
       kTestOtherPassword, true, 2}};
  /* clang-format on */

  // The number of positive samples in |kTestCredentials|.
  const size_t kExpectedNumberOfPropagatedUpdates = 2u;

  const bool kFalseTrue[] = {false, true};
  for (bool propagation_enabled : kFalseTrue) {
    for (bool test_remove_and_add_login : kFalseTrue) {
      SCOPED_TRACE(testing::Message("propagation_enabled: ")
                   << propagation_enabled);
      SCOPED_TRACE(testing::Message("test_remove_and_add_login: ")
                   << test_remove_and_add_login);

      scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
          base::SequencedTaskRunnerHandle::Get(),
          base::SequencedTaskRunnerHandle::Get(),
          base::WrapUnique(new LoginDatabase(test_login_db_file_path()))));
      store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);
      store->RemoveLoginsCreatedBetween(base::Time(), base::Time::Max(),
                                        base::Closure());

      // Set up the initial test data set.
      std::vector<std::unique_ptr<PasswordForm>> all_credentials;
      for (size_t i = 0; i < arraysize(kTestCredentials); ++i) {
        all_credentials.push_back(
            FillPasswordFormWithData(kTestCredentials[i]));
        all_credentials.back()->date_synced =
            all_credentials.back()->date_created;
        store->AddLogin(*all_credentials.back());
        base::RunLoop().RunUntilIdle();
      }

      // The helper must be injected after the initial test data is set up,
      // otherwise it will already start propagating updates as new Android
      // credentials are added.
      MockAffiliatedMatchHelper* mock_helper = new MockAffiliatedMatchHelper;
      store->SetAffiliatedMatchHelper(base::WrapUnique(mock_helper));
      store->enable_propagating_password_changes_to_web_credentials(
          propagation_enabled);

      // Calculate how the correctly updated test data set should look like.
      size_t expected_number_of_propageted_updates =
          propagation_enabled ? kExpectedNumberOfPropagatedUpdates : 0u;
      std::vector<std::unique_ptr<PasswordForm>>
          expected_credentials_after_update;
      for (size_t i = 0; i < all_credentials.size(); ++i) {
        expected_credentials_after_update.push_back(
            base::MakeUnique<PasswordForm>(*all_credentials[i]));
        if (i < 1 + expected_number_of_propageted_updates) {
          expected_credentials_after_update.back()->password_value =
              base::WideToUTF16(kTestNewPassword);
        }
      }

      if (propagation_enabled) {
        std::vector<std::string> affiliated_web_realms;
        affiliated_web_realms.push_back(kTestWebRealm1);
        affiliated_web_realms.push_back(kTestWebRealm2);
        affiliated_web_realms.push_back(kTestWebRealm3);
        affiliated_web_realms.push_back(kTestWebRealm5);
        mock_helper->ExpectCallToGetAffiliatedWebRealms(
            PasswordStore::FormDigest(*expected_credentials_after_update[0]),
            affiliated_web_realms);
      }

      // Explicitly update the Android credential, wait until things calm down,
      // then query all passwords and expect that:
      //   1.) The positive samples in |kTestCredentials| have the new password,
      //       but the negative samples do not.
      //   2.) Change notifications are sent about the updates. Note that as the
      //       test interacts with the (Update|Add)LoginSync methods, only the
      //       derived changes should trigger notifications, the first one would
      //       normally be trigger by Sync.
      MockPasswordStoreObserver mock_observer;
      store->AddObserver(&mock_observer);
      if (propagation_enabled) {
        EXPECT_CALL(mock_observer, OnLoginsChanged(testing::SizeIs(
                                       expected_number_of_propageted_updates)));
      }
      if (test_remove_and_add_login) {
        store->RemoveLoginSync(*all_credentials[0]);
        store->AddLoginSync(*expected_credentials_after_update[0]);
      } else {
        store->UpdateLoginSync(*expected_credentials_after_update[0]);
      }
      base::RunLoop().RunUntilIdle();
      store->RemoveObserver(&mock_observer);

      MockPasswordStoreConsumer mock_consumer;
      EXPECT_CALL(mock_consumer, OnGetPasswordStoreResultsConstRef(
                                     UnorderedPasswordFormElementsAre(
                                         &expected_credentials_after_update)));
      store->GetAutofillableLogins(&mock_consumer);
      store->ShutdownOnUIThread();
      base::RunLoop().RunUntilIdle();
    }
  }
}

TEST_F(PasswordStoreTest, GetLoginsWithAffiliationAndBrandingInformation) {
  static const PasswordFormData kTestCredentials[] = {
      {PasswordForm::SCHEME_HTML, kTestAndroidRealm1, "", "", L"", L"", L"",
       L"username_value_1", L"", true, 1},
      {PasswordForm::SCHEME_HTML, kTestAndroidRealm2, "", "", L"", L"", L"",
       L"username_value_2", L"", true, 1},
      {PasswordForm::SCHEME_HTML, kTestAndroidRealm3, "", "", L"", L"", L"",
       L"username_value_3", L"", true, 1},
      {PasswordForm::SCHEME_HTML, kTestWebRealm1, kTestWebOrigin1, "", L"", L"",
       L"", L"username_value_4", L"", true, 1}};

  for (bool blacklisted : {false, true}) {
    SCOPED_TRACE(testing::Message("use blacklisted logins: ") << blacklisted);
    scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
        base::SequencedTaskRunnerHandle::Get(),
        base::SequencedTaskRunnerHandle::Get(),
        base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
    store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);
    store->RemoveLoginsCreatedBetween(base::Time(), base::Time::Max(),
                                      base::Closure());

    std::vector<std::unique_ptr<PasswordForm>> all_credentials;
    for (const auto& test_credential : kTestCredentials) {
      all_credentials.push_back(FillPasswordFormWithData(test_credential));
      all_credentials.back()->blacklisted_by_user = blacklisted;
      store->AddLogin(*all_credentials.back());
      base::RunLoop().RunUntilIdle();
    }

    MockPasswordStoreConsumer mock_consumer;
    std::vector<std::unique_ptr<PasswordForm>> expected_results;
    for (const auto& credential : all_credentials)
      expected_results.push_back(base::MakeUnique<PasswordForm>(*credential));

    std::vector<MockAffiliatedMatchHelper::AffiliationAndBrandingInformation>
        affiliation_info_for_results = {
            {kTestWebRealm1, kTestAndroidName1, GURL(kTestAndroidIconURL1)},
            {kTestWebRealm2, kTestAndroidName2, GURL(kTestAndroidIconURL2)},
            {/* Pretend affiliation or branding info is unavailable. */},
            {/* Pretend affiliation or branding info is unavailable. */}};

    auto mock_helper = base::MakeUnique<MockAffiliatedMatchHelper>();
    mock_helper->ExpectCallToInjectAffiliationAndBrandingInformation(
        affiliation_info_for_results);
    store->SetAffiliatedMatchHelper(std::move(mock_helper));
    for (size_t i = 0; i < expected_results.size(); ++i) {
      expected_results[i]->affiliated_web_realm =
          affiliation_info_for_results[i].affiliated_web_realm;
      expected_results[i]->app_display_name =
          affiliation_info_for_results[i].app_display_name;
      expected_results[i]->app_icon_url =
          affiliation_info_for_results[i].app_icon_url;
    }

    EXPECT_CALL(mock_consumer,
                OnGetPasswordStoreResultsConstRef(
                    UnorderedPasswordFormElementsAre(&expected_results)));
    if (blacklisted) {
      store->GetBlacklistLoginsWithAffiliationAndBrandingInformation(
          &mock_consumer);
    } else {
      store->GetAutofillableLoginsWithAffiliationAndBrandingInformation(
          &mock_consumer);
    }

    // Since GetAutofillableLoginsWithAffiliationAndBrandingInformation
    // schedules a request for affiliation information to UI thread, don't
    // shutdown UI thread until there are no tasks in the UI queue.
    base::RunLoop().RunUntilIdle();
    store->ShutdownOnUIThread();
    base::RunLoop().RunUntilIdle();
  }
}

TEST_F(PasswordStoreTest, GetLoginsForSameOrganizationName) {
  static constexpr const PasswordFormData kSameOrganizationCredentials[] = {
      // Credential that is an exact match of the observed form.
      {PasswordForm::SCHEME_HTML, kTestWebRealm1, kTestWebOrigin1, "", L"", L"",
       L"", L"username_value_1", L"", true, 1},
      // Credential that is a PSL match of the observed form.
      {PasswordForm::SCHEME_HTML, kTestPSLMatchingWebRealm,
       kTestPSLMatchingWebOrigin, "", L"", L"", L"", L"username_value_2", L"",
       true, 1},
      // Credential for the HTTP version of the observed form. (Should not be
      // filled, but returned as part of same-organization-name matches).
      {PasswordForm::SCHEME_HTML, kTestInsecureWebRealm, kTestInsecureWebOrigin,
       "", L"", L"", L"", L"username_value_3", L"", true, 1},
      // Credential for a signon realm with a different TLD, but same
      // organization identifying name.
      {PasswordForm::SCHEME_HTML, kTestSameOrganizationNameWebRealm,
       kTestSameOrganizationNameWebOrigin, "", L"", L"", L"",
       L"username_value_4", L"", true, 1},
  };

  static constexpr const PasswordFormData kNotSameOrganizationCredentials[] = {
      // Unrelated Web credential.
      {PasswordForm::SCHEME_HTML, kTestUnrelatedWebRealm,
       kTestUnrelatedWebOrigin, "", L"", L"", L"", L"username_value_5", L"",
       true, 1},
      // Credential for an affiliated Android application.
      {PasswordForm::SCHEME_HTML, kTestAndroidRealm1, "", "", L"", L"", L"",
       L"username_value_6", L"", true, 1}};

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  std::vector<std::unique_ptr<PasswordForm>> expected_results;
  for (const auto& form_data : kSameOrganizationCredentials) {
    expected_results.push_back(FillPasswordFormWithData(form_data));
    store->AddLogin(*expected_results.back());
  }

  for (const auto& form_data : kNotSameOrganizationCredentials) {
    store->AddLogin(*FillPasswordFormWithData(form_data));
  }

  const std::string observed_form_realm = kTestWebRealm1;
  MockPasswordStoreConsumer mock_consumer;
  EXPECT_CALL(mock_consumer,
              OnGetPasswordStoreResultsConstRef(
                  UnorderedPasswordFormElementsAre(&expected_results)));
  store->GetLoginsForSameOrganizationName(observed_form_realm, &mock_consumer);
  base::RunLoop().RunUntilIdle();
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

// TODO(crbug.com/706392): Fix password reuse detection for Android.
#if !defined(OS_ANDROID) && !defined(OS_IOS)
TEST_F(PasswordStoreTest, CheckPasswordReuse) {
  static constexpr PasswordFormData kTestCredentials[] = {
      {PasswordForm::SCHEME_HTML, "https://www.google.com",
       "https://www.google.com", "", L"", L"", L"", L"", L"password", true, 1},
      {PasswordForm::SCHEME_HTML, "https://facebook.com",
       "https://facebook.com", "", L"", L"", L"", L"", L"topsecret", true, 1}};

  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));
  store->Init(syncer::SyncableService::StartSyncFlare(), nullptr);

  for (const auto& test_credentials : kTestCredentials) {
    auto credentials = FillPasswordFormWithData(test_credentials);
    store->AddLogin(*credentials);
    base::RunLoop().RunUntilIdle();
  }

  static constexpr struct {
    const wchar_t* input;
    const char* domain;
    const wchar_t* reused_password;  // Set to nullptr if no reuse is expected.
    const char* reuse_domain;
  } kReuseTestData[] = {
      {L"12345password", "https://evil.com", L"password", "google.com"},
      {L"1234567890", "https://evil.com", nullptr, nullptr},
      {L"topsecret", "https://m.facebook.com", nullptr, nullptr},
  };

  for (const auto& test_data : kReuseTestData) {
    MockPasswordReuseDetectorConsumer mock_consumer;
    if (test_data.reused_password) {
      EXPECT_CALL(mock_consumer,
                  OnReuseFound(base::WideToUTF16(test_data.reused_password),
                               std::string(test_data.reuse_domain), 2, 1));
    } else {
      EXPECT_CALL(mock_consumer, OnReuseFound(_, _, _, _)).Times(0);
    }

    store->CheckReuse(base::WideToUTF16(test_data.input), test_data.domain,
                      &mock_consumer);
    base::RunLoop().RunUntilIdle();
  }

  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}
#endif

#if defined(SYNC_PASSWORD_REUSE_DETECTION_ENABLED)
TEST_F(PasswordStoreTest, SavingClearingSyncPassword) {
  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));

  TestingPrefServiceSimple prefs;
  prefs.registry()->RegisterStringPref(prefs::kSyncPasswordHash, std::string(),
                                       PrefRegistry::NO_REGISTRATION_FLAGS);
  prefs.registry()->RegisterStringPref(prefs::kSyncPasswordLengthAndHashSalt,
                                       std::string(),
                                       PrefRegistry::NO_REGISTRATION_FLAGS);
  ASSERT_FALSE(prefs.HasPrefPath(prefs::kSyncPasswordHash));
  store->Init(syncer::SyncableService::StartSyncFlare(), &prefs);

  const base::string16 sync_password = base::ASCIIToUTF16("password");
  const base::string16 input = base::ASCIIToUTF16("123password");
  store->SaveSyncPasswordHash(sync_password);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(prefs.HasPrefPath(prefs::kSyncPasswordHash));

  // Check that sync password reuse is found.
  MockPasswordReuseDetectorConsumer mock_consumer;
  EXPECT_CALL(
      mock_consumer,
      OnReuseFound(sync_password, std::string(kSyncPasswordDomain), 1, 0));
  store->CheckReuse(input, "https://facebook.com", &mock_consumer);
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&mock_consumer);

  // Check that no sync password reuse is found after clearing the saved sync
  // password hash.
  store->ClearSyncPasswordHash();
  EXPECT_FALSE(prefs.HasPrefPath(prefs::kSyncPasswordHash));
  EXPECT_CALL(mock_consumer, OnReuseFound(_, _, _, _)).Times(0);
  store->CheckReuse(input, "https://facebook.com", &mock_consumer);
  base::RunLoop().RunUntilIdle();

  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}

TEST_F(PasswordStoreTest, SubscriptionAndUnsubscriptionFromSignInEvents) {
  scoped_refptr<PasswordStoreDefault> store(new PasswordStoreDefault(
      base::SequencedTaskRunnerHandle::Get(),
      base::SequencedTaskRunnerHandle::Get(),
      base::MakeUnique<LoginDatabase>(test_login_db_file_path())));

  std::unique_ptr<MockPasswordStoreSigninNotifier> notifier =
      base::MakeUnique<MockPasswordStoreSigninNotifier>();
  MockPasswordStoreSigninNotifier* notifier_weak = notifier.get();

  // Check that |store| is subscribed to sign-in events.
  EXPECT_CALL(*notifier_weak, SubscribeToSigninEvents(store.get()));
  store->SetPasswordStoreSigninNotifier(std::move(notifier));
  testing::Mock::VerifyAndClearExpectations(store.get());

  // Check that |store| is unsubscribed from sign-in events on shutdown.
  EXPECT_CALL(*notifier_weak, UnsubscribeFromSigninEvents());
  store->ShutdownOnUIThread();
  base::RunLoop().RunUntilIdle();
}
#endif

}  // namespace password_manager
