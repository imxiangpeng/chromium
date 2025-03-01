// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/statistics_recorder.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/password_manager/password_manager_test_base.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/login/login_handler.h"
#include "chrome/browser/ui/login/login_handler_test_utils.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/content/browser/content_password_manager_driver.h"
#include "components/password_manager/content/browser/content_password_manager_driver_factory.h"
#include "components/password_manager/core/browser/login_model.h"
#include "components/password_manager/core/browser/test_password_store.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/filename_util.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"

using testing::_;

namespace {

// Fixture with the Form-Not-Secure in-field warning feature enabled.
class PasswordManagerBrowserTestWarning
    : public PasswordManagerBrowserTestBase {
 public:
  PasswordManagerBrowserTestWarning() {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // We need to set the feature state before the render process is created,
    // in order for it to inherit the feature state from the browser process.
    // SetUp() runs too early, and SetUpOnMainThread() runs too late.
    scoped_feature_list_.InitAndEnableFeature(
        security_state::kHttpFormWarningFeature);
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PasswordManagerBrowserTestWarning);
};

class MockLoginModelObserver : public password_manager::LoginModelObserver {
 public:
  MOCK_METHOD2(OnAutofillDataAvailableInternal,
               void(const base::string16&, const base::string16&));

 private:
  void OnLoginModelDestroying() override {}
};

GURL GetFileURL(const char* filename) {
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  base::FilePath path;
  PathService::Get(chrome::DIR_TEST_DATA, &path);
  path = path.AppendASCII("password").AppendASCII(filename);
  CHECK(base::PathExists(path));
  return net::FilePathToFileURL(path);
}

// Handles |request| to "/basic_auth". If "Authorization" header is present,
// responds with a non-empty HTTP 200 page (regardless of its value). Otherwise
// serves a Basic Auth challenge.
std::unique_ptr<net::test_server::HttpResponse> HandleTestAuthRequest(
    const net::test_server::HttpRequest& request) {
  if (!base::StartsWith(request.relative_url, "/basic_auth",
                        base::CompareCase::SENSITIVE))
    return std::unique_ptr<net::test_server::HttpResponse>();

  if (base::ContainsKey(request.headers, "Authorization")) {
    std::unique_ptr<net::test_server::BasicHttpResponse> http_response(
        new net::test_server::BasicHttpResponse);
    http_response->set_code(net::HTTP_OK);
    http_response->set_content("Success!");
    return std::move(http_response);
  } else {
    std::unique_ptr<net::test_server::BasicHttpResponse> http_response(
        new net::test_server::BasicHttpResponse);
    http_response->set_code(net::HTTP_UNAUTHORIZED);
    http_response->AddCustomHeader("WWW-Authenticate",
                                   "Basic realm=\"test realm\"");
    return std::move(http_response);
  }
}

class ObservingAutofillClient
    : public autofill::TestAutofillClient,
      public content::WebContentsUserData<ObservingAutofillClient> {
 public:
  ~ObservingAutofillClient() override {}

  // Wait until the autofill popup is shown.
  void WaitForAutofillPopup() {
    base::RunLoop run_loop;
    run_loop_ = &run_loop;
    run_loop.Run();
    DCHECK(!run_loop_);
  }

  bool popup_shown() const { return popup_shown_; }

  void ShowAutofillPopup(
      const gfx::RectF& element_bounds,
      base::i18n::TextDirection text_direction,
      const std::vector<autofill::Suggestion>& suggestions,
      base::WeakPtr<autofill::AutofillPopupDelegate> delegate) override {
    if (run_loop_)
      run_loop_->Quit();
    run_loop_ = nullptr;
    popup_shown_ = true;
  }

 private:
  explicit ObservingAutofillClient(content::WebContents* web_contents)
      : run_loop_(nullptr), popup_shown_(false) {}
  friend class content::WebContentsUserData<ObservingAutofillClient>;

  base::RunLoop* run_loop_;
  bool popup_shown_;

  DISALLOW_COPY_AND_ASSIGN(ObservingAutofillClient);
};

// For simplicity we assume that password store contains only 1 credential.
void CheckThatCredentialsStored(
    password_manager::TestPasswordStore* password_store,
    const base::string16& username,
    const base::string16& password) {
  auto& passwords_map = password_store->stored_passwords();
  ASSERT_EQ(1u, passwords_map.size());
  auto& passwords_vector = passwords_map.begin()->second;
  ASSERT_EQ(1u, passwords_vector.size());
  const autofill::PasswordForm& form = passwords_vector[0];
  EXPECT_EQ(username, form.username_value);
  EXPECT_EQ(password, form.password_value);
}

void TestPromptNotShown(const char* failure_message,
                        content::WebContents* web_contents,
                        content::RenderViewHost* rvh) {
  SCOPED_TRACE(testing::Message(failure_message));

  NavigationObserver observer(web_contents);
  std::string fill_and_submit =
      "document.getElementById('username_failed').value = 'temp';"
      "document.getElementById('password_failed').value = 'random';"
      "document.getElementById('failed_form').submit()";

  ASSERT_TRUE(content::ExecuteScript(rvh, fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(BubbleObserver(web_contents).IsShowingSavePrompt());
}

}  // namespace

DEFINE_WEB_CONTENTS_USER_DATA_KEY(ObservingAutofillClient);

namespace password_manager {

// Actual tests ---------------------------------------------------------------
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, PromptForNormalSubmit) {
  NavigateToFile("/password/password_form.html");

  // Fill a form and submit through a <input type="submit"> button. Nothing
  // special.
  NavigationObserver observer(WebContents());
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();

  // Save the password and check the store.
  BubbleObserver bubble_observer(WebContents());
  EXPECT_TRUE(bubble_observer.IsShowingSavePrompt());
  bubble_observer.AcceptSavePrompt();
  WaitForPasswordStore();

  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("random"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptIfFormReappeared) {
  NavigateToFile("/password/failed.html");
  TestPromptNotShown("normal form", WebContents(), RenderViewHost());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptIfFormReappearedWithPartsHidden) {
  NavigateToFile("/password/failed_partly_visible.html");
  TestPromptNotShown("partly visible form", WebContents(), RenderViewHost());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptIfFormReappearedInputOutsideFor) {
  NavigateToFile("/password/failed_input_outside.html");
  TestPromptNotShown("form with input outside", WebContents(),
                     RenderViewHost());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptIfPasswordFormManagerDestroyed) {
  NavigateToFile("/password/password_form.html");
  // Simulate the Credential Manager API essentially destroying all the
  // PasswordFormManager instances.
  ChromePasswordManagerClient::FromWebContents(WebContents())
      ->NotifyStorePasswordCalled();

  // Fill a form and submit through a <input type="submit"> button. The renderer
  // should not send "PasswordFormsParsed" messages after the page was loaded.
  NavigationObserver observer(WebContents());
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForSubmitWithInPageNavigation) {
  NavigateToFile("/password/password_navigate_before_submit.html");

  // Fill a form and submit through a <input type="submit"> button. Nothing
  // special. The form does an in-page navigation before submitting.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       LoginSuccessWithUnrelatedForm) {
  // Log in, see a form on the landing page. That form is not related to the
  // login form (=has a different action), so we should offer saving the
  // password.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_unrelated').value = 'temp';"
      "document.getElementById('password_unrelated').value = 'random';"
      "document.getElementById('submit_unrelated').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, LoginFailed) {
  // Log in, see a form on the landing page. That form is not related to the
  // login form (=has a different action), so we should offer saving the
  // password.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_failed').value = 'temp';"
      "document.getElementById('password_failed').value = 'random';"
      "document.getElementById('submit_failed').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, Redirects) {
  NavigateToFile("/password/password_form.html");

  // Fill a form and submit through a <input type="submit"> button. The form
  // points to a redirection page.
  NavigationObserver observer1(WebContents());
  std::string fill_and_submit =
      "document.getElementById('username_redirect').value = 'temp';"
      "document.getElementById('password_redirect').value = 'random';"
      "document.getElementById('submit_redirect').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer1.Wait();
  BubbleObserver bubble_observer(WebContents());
  EXPECT_TRUE(bubble_observer.IsShowingSavePrompt());

  // The redirection page now redirects via Javascript. We check that the
  // bubble stays.
  NavigationObserver observer2(WebContents());
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(), "window.location.href = 'done.html';"));
  observer2.Wait();
  EXPECT_TRUE(bubble_observer.IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForSubmitUsingJavaScript) {
  NavigateToFile("/password/password_form.html");

  // Fill a form and submit using <button> that calls submit() on the form.
  // This should work regardless of the type of element, as long as submit() is
  // called.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, PromptForDynamicForm) {
  // Adding a PSL matching form is a workaround explained later.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  GURL psl_orogin = embedded_test_server()->GetURL("psl.example.com", "/");
  signin_form.signon_realm = psl_orogin.spec();
  signin_form.origin = psl_orogin;
  signin_form.username_value = base::ASCIIToUTF16("unused_username");
  signin_form.password_value = base::ASCIIToUTF16("unused_password");
  password_store->AddLogin(signin_form);

  // Show the dynamic form.
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "example.com", "/password/dynamic_password_form.html"));
  ASSERT_TRUE(content::ExecuteScript(
      RenderViewHost(),
      "document.getElementById('create_form_button').click();"));

  // Blink has a timer for 0.3 seconds before it updates the browser with the
  // new dynamic form. We wait for the form being detected by observing the UI
  // state. The state changes due to the matching credential saved above. Later
  // the form submission is definitely noticed by the browser.
  BubbleObserver(WebContents()).WaitForManagementState();

  // Fill the dynamic password form and submit.
  NavigationObserver observer(WebContents());
  std::string fill_and_submit =
      "document.dynamic_form.username.value = 'tempro';"
      "document.dynamic_form.password.value = 'random';"
      "document.dynamic_form.submit()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();

  EXPECT_TRUE(BubbleObserver(WebContents()).IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoPromptForNavigation) {
  NavigateToFile("/password/password_form.html");

  // Don't fill the password form, just navigate away. Shouldn't prompt.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(), "window.location.href = 'done.html';"));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptForSubFrameNavigation) {
  NavigateToFile("/password/multi_frames.html");

  // If you are filling out a password form in one frame and a different frame
  // navigates, this should not trigger the infobar.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  observer.SetPathToWaitFor("/password/done.html");
  std::string fill =
      "var first_frame = document.getElementById('first_frame');"
      "var frame_doc = first_frame.contentDocument;"
      "frame_doc.getElementById('username_field').value = 'temp';"
      "frame_doc.getElementById('password_field').value = 'random';";
  std::string navigate_frame =
      "var second_iframe = document.getElementById('second_frame');"
      "second_iframe.contentWindow.location.href = 'done.html';";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill));
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), navigate_frame));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptAfterSubmitWithSubFrameNavigation) {
  NavigateToFile("/password/multi_frames.html");

  // Make sure that we prompt to save password even if a sub-frame navigation
  // happens first.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  observer.SetPathToWaitFor("/password/done.html");
  std::string navigate_frame =
      "var second_iframe = document.getElementById('second_frame');"
      "second_iframe.contentWindow.location.href = 'other.html';";
  std::string fill_and_submit =
      "var first_frame = document.getElementById('first_frame');"
      "var frame_doc = first_frame.contentDocument;"
      "frame_doc.getElementById('username_field').value = 'temp';"
      "frame_doc.getElementById('password_field').value = 'random';"
      "frame_doc.getElementById('input_submit_button').click();";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), navigate_frame));
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForFailedLoginFromMainFrameWithMultiFramesInPage) {
  NavigateToFile("/password/multi_frames.html");

  // Make sure that we don't prompt to save the password for a failed login
  // from the main frame with multiple frames in the same page.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_failed').value = 'temp';"
      "document.getElementById('password_failed').value = 'random';"
      "document.getElementById('submit_failed').click();";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForFailedLoginFromSubFrameWithMultiFramesInPage) {
  NavigateToFile("/password/multi_frames.html");

  // Make sure that we don't prompt to save the password for a failed login
  // from a sub-frame with multiple frames in the same page.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "var first_frame = document.getElementById('first_frame');"
      "var frame_doc = first_frame.contentDocument;"
      "frame_doc.getElementById('username_failed').value = 'temp';"
      "frame_doc.getElementById('password_failed').value = 'random';"
      "frame_doc.getElementById('submit_failed').click();";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.SetPathToWaitFor("/password/failed.html");
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, PromptForXHRSubmit) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Verify that we show the save password prompt if a form returns false
  // in its onsubmit handler but instead logs in/navigates via XHR.
  // Note that calling 'submit()' on a form with javascript doesn't call
  // the onsubmit handler, so we click the submit button instead.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForXHRWithoutOnSubmit) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Verify that if XHR navigation occurs and the form is properly filled out,
  // we try and save the password even though onsubmit hasn't been called.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_navigate =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "send_xhr()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_navigate));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForXHRWithNewPasswordsWithoutOnSubmit) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Verify that if XHR navigation occurs and the form is properly filled out,
  // we try and save the password even though onsubmit hasn't been called.
  // Specifically verify that the password form saving new passwords is treated
  // the same as a login form.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_navigate =
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_password_field').value = 'random';"
      "document.getElementById('confirmation_password_field').value = 'random';"
      "send_xhr()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_navigate));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForXHRSubmitWithoutNavigation) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if XHR without navigation occurs and the form has been filled
  // out we try and save the password. Note that in general the submission
  // doesn't need to be via form.submit(), but for testing purposes it's
  // necessary since we otherwise ignore changes made to the value of these
  // fields by script.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"XHR_FINISHED\"")
      break;
  }

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForXHRSubmitWithoutNavigation_SignupForm) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if XHR without navigation occurs and the form has been filled
  // out we try and save the password. Note that in general the submission
  // doesn't need to be via form.submit(), but for testing purposes it's
  // necessary since we otherwise ignore changes made to the value of these
  // fields by script.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_password_field').value = 'random';"
      "document.getElementById('confirmation_password_field').value = 'random';"
      "document.getElementById('signup_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"XHR_FINISHED\"")
      break;
  }

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptForXHRSubmitWithoutNavigationWithUnfilledForm) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if XHR without navigation occurs and the form has NOT been
  // filled out we don't prompt.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"XHR_FINISHED\"")
      break;
  }

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForXHRSubmitWithoutNavigationWithUnfilledForm_SignupForm) {
  NavigateToFile("/password/password_xhr_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if XHR without navigation occurs and the form has NOT been
  // filled out we don't prompt.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"XHR_FINISHED\"")
      break;
  }

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, PromptForFetchSubmit) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Verify that we show the save password prompt if a form returns false
  // in its onsubmit handler but instead logs in/navigates via Fetch.
  // Note that calling 'submit()' on a form with javascript doesn't call
  // the onsubmit handler, so we click the submit button instead.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForFetchWithoutOnSubmit) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Verify that if Fetch navigation occurs and the form is properly filled out,
  // we try and save the password even though onsubmit hasn't been called.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_navigate =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "send_fetch()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_navigate));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForFetchWithNewPasswordsWithoutOnSubmit) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Verify that if Fetch navigation occurs and the form is properly filled out,
  // we try and save the password even though onsubmit hasn't been called.
  // Specifically verify that the password form saving new passwords is treated
  // the same as a login form.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_navigate =
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_password_field').value = 'random';"
      "document.getElementById('confirmation_password_field').value = 'random';"
      "send_fetch()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_navigate));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForFetchSubmitWithoutNavigation) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if XHR without navigation occurs and the form has been filled
  // out we try and save the password. Note that in general the submission
  // doesn't need to be via form.submit(), but for testing purposes it's
  // necessary since we otherwise ignore changes made to the value of these
  // fields by script.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"FETCH_FINISHED\"")
      break;
  }

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForFetchSubmitWithoutNavigation_SignupForm) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Need to pay attention for a message that Fetch has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if Fetch without navigation occurs and the form has been filled
  // out we try and save the password. Note that in general the submission
  // doesn't need to be via form.submit(), but for testing purposes it's
  // necessary since we otherwise ignore changes made to the value of these
  // fields by script.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_password_field').value = 'random';"
      "document.getElementById('confirmation_password_field').value = 'random';"
      "document.getElementById('signup_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"FETCH_FINISHED\"")
      break;
  }

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForFetchSubmitWithoutNavigationWithUnfilledForm) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Need to pay attention for a message that Fetch has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if Fetch without navigation occurs and the form has NOT been
  // filled out we don't prompt.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"FETCH_FINISHED\"")
      break;
  }

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForFetchSubmitWithoutNavigationWithUnfilledForm_SignupForm) {
  NavigateToFile("/password/password_fetch_submit.html");

  // Need to pay attention for a message that Fetch has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  // Verify that if Fetch without navigation occurs and the form has NOT been
  // filled out we don't prompt.
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "navigate = false;"
      "document.getElementById('signup_username_field').value = 'temp';"
      "document.getElementById('signup_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"FETCH_FINISHED\"")
      break;
  }

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoPromptIfLinkClicked) {
  NavigateToFile("/password/password_form.html");

  // Verify that if the user takes a direct action to leave the page, we don't
  // prompt to save the password even if the form is already filled out.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_click_link =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('link').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_click_link));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       VerifyPasswordGenerationUpload) {
  // Prevent Autofill requests from actually going over the wire.
  net::TestURLFetcherFactory factory;
  // Disable Autofill requesting access to AddressBook data. This causes
  // the test to hang on Mac.
  autofill::test::DisableSystemServices(browser()->profile()->GetPrefs());

  // Visit a signup form.
  NavigateToFile("/password/signup_form.html");

  // Enter a password and save it.
  NavigationObserver first_observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('other_info').value = 'stuff';"
      "document.getElementById('username_field').value = 'my_username';"
      "document.getElementById('password_field').value = 'password';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));

  first_observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  // Now navigate to a login form that has similar HTML markup.
  NavigateToFile("/password/password_form.html");

  // Simulate a user click to force an autofill of the form's DOM value, not
  // just the suggested value.
  content::SimulateMouseClick(WebContents(), 0,
                              blink::WebMouseEvent::Button::kLeft);

  // The form should be filled with the previously submitted username.
  CheckElementValue("username_field", "my_username");
  CheckElementValue("password_field", "password");

  // Submit the form and verify that there is no infobar (as the password
  // has already been saved).
  NavigationObserver second_observer(WebContents());
  std::unique_ptr<BubbleObserver> second_prompt_observer(
      new BubbleObserver(WebContents()));
  std::string submit_form =
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), submit_form));
  second_observer.Wait();
  EXPECT_FALSE(second_prompt_observer->IsShowingSavePrompt());

  // Verify that we sent two pings to Autofill. One vote for of PASSWORD for
  // the current form, and one vote for ACCOUNT_CREATION_PASSWORD on the
  // original form since it has more than 2 text input fields and was used for
  // the first time on a different form.
  base::HistogramBase* upload_histogram =
      base::StatisticsRecorder::FindHistogram(
          "PasswordGeneration.UploadStarted");
  ASSERT_TRUE(upload_histogram);
  std::unique_ptr<base::HistogramSamples> snapshot =
      upload_histogram->SnapshotSamples();
  EXPECT_EQ(0, snapshot->GetCount(0 /* failure */));
  EXPECT_EQ(2, snapshot->GetCount(1 /* success */));

  autofill::test::ReenableSystemServices();
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForSubmitFromIframe) {
  NavigateToFile("/password/password_submit_from_iframe.html");

  // Submit a form in an iframe, then cause the whole page to navigate without a
  // user gesture. We expect the save password prompt to be shown here, because
  // some pages use such iframes for login forms.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "var iframe = document.getElementById('test_iframe');"
      "var iframe_doc = iframe.contentDocument;"
      "iframe_doc.getElementById('username_field').value = 'temp';"
      "iframe_doc.getElementById('password_field').value = 'random';"
      "iframe_doc.getElementById('submit_button').click()";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForInputElementWithoutName) {
  // Check that the prompt is shown for forms where input elements lack the
  // "name" attribute but the "id" is present.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field_no_name').value = 'temp';"
      "document.getElementById('password_field_no_name').value = 'random';"
      "document.getElementById('input_submit_button_no_name').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForInputElementWithoutId) {
  // Check that the prompt is shown for forms where input elements lack the
  // "id" attribute but the "name" attribute is present.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementsByName('username_field_no_id')[0].value = 'temp';"
      "document.getElementsByName('password_field_no_id')[0].value = 'random';"
      "document.getElementsByName('input_submit_button_no_id')[0].click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForInputElementWithoutIdAndName) {
  // Check that prompt is shown for forms where the input fields lack both
  // the "id" and the "name" attributes.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "var form = document.getElementById('testform_elements_no_id_no_name');"
      "var username = form.children[0];"
      "username.value = 'temp';"
      "var password = form.children[1];"
      "password.value = 'random';"
      "form.children[2].click()";  // form.children[2] is the submit button.
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  // Check that credentials are stored.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());

  WaitForPasswordStore();
  EXPECT_FALSE(password_store->IsEmpty());

  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("random"));
}

// Test for checking that no prompt is shown for URLs with file: scheme.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptForFileSchemeURLs) {
  GURL url = GetFileURL("password_form.html");
  ui_test_utils::NavigateToURL(browser(), url);

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptForLandingPageWithHTTPErrorStatusCode) {
  // Check that no prompt is shown for forms where the landing page has
  // HTTP status 404.
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field_http_error').value = 'temp';"
      "document.getElementById('password_field_http_error').value = 'random';"
      "document.getElementById('input_submit_button_http_error').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       DeleteFrameBeforeSubmit) {
  NavigateToFile("/password/multi_frames.html");

  NavigationObserver observer(WebContents());
  // Make sure we save some password info from an iframe and then destroy it.
  std::string save_and_remove =
      "var first_frame = document.getElementById('first_frame');"
      "var frame_doc = first_frame.contentDocument;"
      "frame_doc.getElementById('username_field').value = 'temp';"
      "frame_doc.getElementById('password_field').value = 'random';"
      "frame_doc.getElementById('input_submit_button').click();"
      "first_frame.parentNode.removeChild(first_frame);";
  // Submit from the main frame, but without navigating through the onsubmit
  // handler.
  std::string navigate_frame =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click();"
      "window.location.href = 'done.html';";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), save_and_remove));
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), navigate_frame));
  observer.Wait();
  // The only thing we check here is that there is no use-after-free reported.
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordValueAccessible) {
  // At first let us save a credential to the password store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.action = embedded_test_server()->base_url();
  signin_form.username_value = base::ASCIIToUTF16("admin");
  signin_form.password_value = base::ASCIIToUTF16("12345");
  password_store->AddLogin(signin_form);

  // Steps from https://crbug.com/337429#c37.
  // Navigate to the page, click a link that opens a second tab, reload the
  // first tab and observe that the password is accessible.
  NavigateToFile("/password/form_and_link.html");

  // Click on a link to open a new tab, then switch back to the first one.
  EXPECT_EQ(1, browser()->tab_strip_model()->count());
  std::string click =
      "document.getElementById('testlink').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), click));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  browser()->tab_strip_model()->ActivateTabAt(0, false);

  // Reload the original page to have the saved credentials autofilled.
  NavigationObserver reload_observer(WebContents());
  NavigateToFile("/password/form_and_link.html");
  reload_observer.Wait();

  // Wait until the username is filled, to make sure autofill kicked in.
  WaitForElementValue("username_field", "admin");
  // Now check that the password is not accessible yet.
  CheckElementValue("password_field", "");
  // Let the user interact with the page.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));
  // Wait until that interaction causes the password value to be revealed.
  WaitForElementValue("password_field", "12345");
  // And check that after the side-effects of the interaction took place, the
  // username value stays the same.
  CheckElementValue("username_field", "admin");
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordValueAccessibleOnSubmit) {
  // At first let us save a credential to the password store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.action = embedded_test_server()->base_url();
  signin_form.username_value = base::ASCIIToUTF16("admin");
  signin_form.password_value = base::ASCIIToUTF16("random_secret");
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/form_and_link.html");

  // Get the position of the 'submit' button.
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var submitRect = document.getElementById('input_submit_button')"
      ".getBoundingClientRect();"));

  int top;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(submitRect.top);",
      &top));
  int left;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send(submitRect.left);", &left));

  NavigationObserver submit_observer(WebContents());
  // Submit the form via a tap on the submit button.
  content::SimulateTapAt(WebContents(), gfx::Point(left + 1, top + 1));
  submit_observer.Wait();
  std::string query = WebContents()->GetURL().query();
  EXPECT_THAT(query, testing::HasSubstr("random_secret"));
}

// Test fix for crbug.com/338650.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       DontPromptForPasswordFormWithDefaultValue) {
  NavigateToFile("/password/password_form_with_default_value.html");

  // Don't prompt if we navigate away even if there is a password value since
  // it's not coming from the user.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  NavigateToFile("/password/done.html");
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       DontPromptForPasswordFormWithReadonlyPasswordField) {
  NavigateToFile("/password/password_form_with_password_readonly.html");

  // Fill a form and submit through a <input type="submit"> button. Nothing
  // special.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptWhenEnableAutomaticPasswordSavingSwitchIsNotSet) {
  NavigateToFile("/password/password_form.html");

  // Fill a form and submit through a <input type="submit"> button.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Test fix for crbug.com/368690.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoPromptWhenReloading) {
  NavigateToFile("/password/password_form.html");

  std::string fill =
      "document.getElementById('username_redirect').value = 'temp';"
      "document.getElementById('password_redirect').value = 'random';";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill));

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  GURL url = embedded_test_server()->GetURL("/password/password_form.html");
  chrome::NavigateParams params(browser(), url, ::ui::PAGE_TRANSITION_RELOAD);
  ui_test_utils::NavigateToURL(&params);
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

// Test that if a form gets dynamically added between the form parsing and
// rendering, and while the main frame still loads, it still is registered, and
// thus saving passwords from it works.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       FormsAddedBetweenParsingAndRendering) {
  NavigateToFile("/password/between_parsing_and_rendering.html");

  NavigationObserver observer(WebContents());
  std::string submit =
      "document.getElementById('username').value = 'temp';"
      "document.getElementById('password').value = 'random';"
      "document.getElementById('submit-button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), submit));
  observer.Wait();

  EXPECT_TRUE(BubbleObserver(WebContents()).IsShowingSavePrompt());
}

// Test that if a hidden form gets dynamically added between the form parsing
// and rendering, it still is registered, and autofilling works.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       HiddenFormAddedBetweenParsingAndRendering) {
  // At first let us save a credential to the password store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.action = embedded_test_server()->base_url();
  signin_form.username_value = base::ASCIIToUTF16("admin");
  signin_form.password_value = base::ASCIIToUTF16("12345");
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/between_parsing_and_rendering.html?hidden");

  std::string show_form =
      "document.getElementsByTagName('form')[0].style.display = 'block'";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), show_form));

  // Wait until the username is filled, to make sure autofill kicked in.
  WaitForElementValue("username", "admin");
  CheckElementValue("password", "12345");
}

// https://crbug.com/713645
// Navigate to a page that can't load some of the subresources. Create a hidden
// form when the body is loaded. Make the form visible. Chrome should autofill
// the form.
// The fact that the form is hidden isn't super important but reproduces the
// actual bug.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, SlowPageFill) {
  // At first let us save a credential to the password store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.action = embedded_test_server()->base_url();
  signin_form.username_value = base::ASCIIToUTF16("admin");
  signin_form.password_value = base::ASCIIToUTF16("12345");
  password_store->AddLogin(signin_form);

  GURL url =
      embedded_test_server()->GetURL("/password/infinite_password_form.html");
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), url, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_NONE);

  // Wait for autofill.
  BubbleObserver bubble_observer(WebContents());
  bubble_observer.WaitForManagementState();

  // Show the form and make sure that the password was autofilled.
  std::string show_form =
      "document.getElementsByTagName('form')[0].style.display = 'block'";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), show_form));

  CheckElementValue("username", "admin");
  CheckElementValue("password", "12345");
}

// Test that if there was no previous page load then the PasswordManagerDriver
// does not think that there were SSL errors on the current page. The test opens
// a new tab with a URL for which the embedded test server issues a basic auth
// challenge.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoLastLoadGoodLastLoad) {
  // We must use a new test server here because embedded_test_server() is
  // already started at this point and adding the request handler to it would
  // not be thread safe.
  net::EmbeddedTestServer http_test_server;

  // Teach the embedded server to handle requests by issuing the basic auth
  // challenge.
  http_test_server.RegisterRequestHandler(base::Bind(&HandleTestAuthRequest));
  ASSERT_TRUE(http_test_server.Start());

  LoginPromptBrowserTestObserver login_observer;
  // We need to register to all sources, because the navigation observer we are
  // interested in is for a new tab to be opened, and thus does not exist yet.
  login_observer.Register(content::NotificationService::AllSources());

  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS).get());
  ASSERT_TRUE(password_store->IsEmpty());

  // Navigate to a page requiring HTTP auth. Wait for the tab to get the correct
  // WebContents, but don't wait for navigation, which only finishes after
  // authentication.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), http_test_server.GetURL("/basic_auth"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_TAB);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::NavigationController* nav_controller = &tab->GetController();
  NavigationObserver nav_observer(tab);
  WindowedAuthNeededObserver auth_needed_observer(nav_controller);
  auth_needed_observer.Wait();

  WindowedAuthSuppliedObserver auth_supplied_observer(nav_controller);
  // Offer valid credentials on the auth challenge.
  ASSERT_EQ(1u, login_observer.handlers().size());
  LoginHandler* handler = *login_observer.handlers().begin();
  ASSERT_TRUE(handler);
  // Any username/password will work.
  handler->SetAuth(base::UTF8ToUTF16("user"), base::UTF8ToUTF16("pwd"));
  auth_supplied_observer.Wait();

  // The password manager should be working correctly.
  nav_observer.Wait();
  WaitForPasswordStore();
  BubbleObserver bubble_observer(tab);
  EXPECT_TRUE(bubble_observer.IsShowingSavePrompt());
  bubble_observer.AcceptSavePrompt();

  // Spin the message loop to make sure the password store had a chance to save
  // the password.
  WaitForPasswordStore();
  EXPECT_FALSE(password_store->IsEmpty());
}

// Fill out a form and click a button. The Javascript removes the form, creates
// a similar one with another action, fills it out and submits. Chrome can
// manage to detect the new one and create a complete matching
// PasswordFormManager. Otherwise, the all-but-action matching PFM should be
// used. Regardless of the internals the user sees the bubble in 100% cases.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PreferPasswordFormManagerWhichFinishedMatching) {
  NavigateToFile("/password/create_form_copy_on_submit.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string submit =
      "document.getElementById('username').value = 'overwrite_me';"
      "document.getElementById('password').value = 'random';"
      "document.getElementById('non-form-button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), submit));
  observer.Wait();

  WaitForPasswordStore();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Test that if login fails and content server pushes a different login form
// with action URL having different schemes. Heuristic shall be able
// identify such cases and *shall not* prompt to save incorrect password.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForLoginFailedAndServerPushSeperateLoginForm_HttpToHttps) {
  std::string path =
      "/password/separate_login_form_with_onload_submit_script.html";
  GURL http_url(embedded_test_server()->GetURL(path));
  ASSERT_TRUE(http_url.SchemeIs(url::kHttpScheme));

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  ui_test_utils::NavigateToURL(browser(), http_url);

  observer.SetPathToWaitFor("/password/done_and_separate_login_form.html");
  observer.Wait();

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForLoginFailedAndServerPushSeperateLoginForm_HttpsToHttp) {
  // This test case cannot inject the scripts via content::ExecuteScript() in
  // files served through HTTPS. Therefore the scripts are made part of the HTML
  // site and executed on load.
  std::string path =
      "/password/separate_login_form_with_onload_submit_script.html";
  GURL https_url(https_test_server().GetURL(path));
  ASSERT_TRUE(https_url.SchemeIs(url::kHttpsScheme));

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  ui_test_utils::NavigateToURL(browser(), https_url);

  observer.SetPathToWaitFor("/password/done_and_separate_login_form.html");
  observer.Wait();

  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

// Tests whether a attempted submission of a malicious credentials gets blocked.
// This simulates a case which is described in http://crbug.com/571580.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    NoPromptForSeperateLoginFormWhenSwitchingFromHttpsToHttp) {
  std::string path = "/password/password_form.html";
  GURL https_url(https_test_server().GetURL(path));
  ASSERT_TRUE(https_url.SchemeIs(url::kHttpsScheme));

  NavigationObserver form_observer(WebContents());
  ui_test_utils::NavigateToURL(browser(), https_url);
  form_observer.Wait();

  std::string fill_and_submit_redirect =
      "document.getElementById('username_redirect').value = 'user';"
      "document.getElementById('password_redirect').value = 'password';"
      "document.getElementById('submit_redirect').click()";
  ASSERT_TRUE(
      content::ExecuteScript(RenderViewHost(), fill_and_submit_redirect));

  NavigationObserver redirect_observer(WebContents());
  redirect_observer.SetPathToWaitFor("/password/redirect.html");
  redirect_observer.Wait();

  // Normally the redirect happens to done.html. Here an attack is simulated
  // that hijacks the redirect to a attacker controlled page.
  GURL http_url(
      embedded_test_server()->GetURL("/password/simple_password.html"));
  std::string attacker_redirect =
      "window.location.href = '" + http_url.spec() + "';";
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       attacker_redirect));

  NavigationObserver attacker_observer(WebContents());
  attacker_observer.SetPathToWaitFor("/password/simple_password.html");
  attacker_observer.Wait();

  std::string fill_and_submit_attacker_form =
      "document.getElementById('username_field').value = 'attacker_username';"
      "document.getElementById('password_field').value = 'attacker_password';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(
      content::ExecuteScript(RenderViewHost(), fill_and_submit_attacker_form));

  NavigationObserver done_observer(WebContents());
  done_observer.SetPathToWaitFor("/password/done.html");
  done_observer.Wait();

  WaitForPasswordStore();
  BubbleObserver prompt_observer(WebContents());
  EXPECT_TRUE(prompt_observer.IsShowingSavePrompt());
  prompt_observer.AcceptSavePrompt();

  // Wait for password store and check that credentials are stored.
  WaitForPasswordStore();
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  EXPECT_FALSE(password_store->IsEmpty());
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("user"),
                             base::ASCIIToUTF16("password"));
}

// Tests that after HTTP -> HTTPS migration the credential is autofilled.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       HttpMigratedCredentialAutofilled) {
  // Add an http credential to the password store.
  GURL https_origin = https_test_server().base_url();
  ASSERT_TRUE(https_origin.SchemeIs(url::kHttpsScheme));
  GURL::Replacements rep;
  rep.SetSchemeStr(url::kHttpScheme);
  GURL http_origin = https_origin.ReplaceComponents(rep);
  autofill::PasswordForm http_form;
  http_form.signon_realm = http_origin.spec();
  http_form.origin = http_origin;
  // Assume that the previous action was already HTTPS one matching the current
  // page.
  http_form.action = https_origin;
  http_form.username_value = base::ASCIIToUTF16("user");
  http_form.password_value = base::ASCIIToUTF16("12345");
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS).get());
  password_store->AddLogin(http_form);

  NavigationObserver form_observer(WebContents());
  ui_test_utils::NavigateToURL(
      browser(), https_test_server().GetURL("/password/password_form.html"));
  form_observer.Wait();
  WaitForPasswordStore();

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));
  WaitForElementValue("username_field", "user");
  CheckElementValue("password_field", "12345");
}

// Tests that obsolete HTTP credentials are moved when a site migrated to HTTPS
// and has HSTS enabled.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ObsoleteHttpCredentialMovedOnMigrationToHstsSite) {
  // Add an http credential to the password store.
  GURL https_origin = https_test_server().base_url();
  ASSERT_TRUE(https_origin.SchemeIs(url::kHttpsScheme));
  GURL::Replacements rep;
  rep.SetSchemeStr(url::kHttpScheme);
  GURL http_origin = https_origin.ReplaceComponents(rep);
  autofill::PasswordForm http_form;
  http_form.signon_realm = http_origin.spec();
  http_form.origin = http_origin;
  http_form.username_value = base::ASCIIToUTF16("user");
  http_form.password_value = base::ASCIIToUTF16("12345");
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  password_store->AddLogin(http_form);

  // Treat the host of the HTTPS test server as HSTS.
  AddHSTSHost(https_test_server().host_port_pair().host());

  // Navigate to HTTPS page and trigger the migration.
  NavigationObserver form_observer(WebContents());
  ui_test_utils::NavigateToURL(
      browser(), https_test_server().GetURL("/password/password_form.html"));
  form_observer.Wait();

  // Issue the query for HTTPS credentials.
  WaitForPasswordStore();

  // Realize there are no HTTPS credentials and issue the query for HTTP
  // credentials instead.
  WaitForPasswordStore();

  // Sync with IO thread before continuing. This is necessary, because the
  // credential migration triggers a query for the HSTS state which gets
  // executed on the IO thread. The actual task is empty, because only the reply
  // is relevant. By the time the reply is executed it is guaranteed that the
  // migration is completed.
  base::RunLoop run_loop;
  content::BrowserThread::PostTaskAndReply(content::BrowserThread::IO,
                                           FROM_HERE, base::BindOnce([]() {}),
                                           run_loop.QuitClosure());
  run_loop.Run();

  // Migration updates should touch the password store.
  WaitForPasswordStore();
  // Only HTTPS passwords should be present.
  EXPECT_TRUE(
      password_store->stored_passwords().at(http_origin.spec()).empty());
  EXPECT_FALSE(
      password_store->stored_passwords().at(https_origin.spec()).empty());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptWhenPasswordFormWithoutUsernameFieldSubmitted) {
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS).get());

  EXPECT_TRUE(password_store->IsEmpty());

  NavigateToFile("/password/form_with_only_password_field.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string submit =
      "document.getElementById('password').value = 'password';"
      "document.getElementById('submit-button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), submit));
  observer.Wait();

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  WaitForPasswordStore();
  EXPECT_FALSE(password_store->IsEmpty());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForPasswordFormWithoutUsernameField) {
  std::string submit =
      "document.getElementById('password').value = 'mypassword';"
      "document.getElementById('submit-button').click();";
  VerifyPasswordIsSavedAndFilled("/password/form_with_only_password_field.html",
                                 submit, "password", "mypassword");
}

// Test that if a form gets autofilled, then it gets autofilled on re-creation
// as well.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ReCreatedFormsGetFilled) {
  // At first let us save a credential to the password store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.action = embedded_test_server()->base_url();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("random");
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/dynamic_password_form.html");
  const std::string create_form =
      "document.getElementById('create_form_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), create_form));
  // Wait until the username is filled, to make sure autofill kicked in.
  WaitForElementValue("username_id", "temp");

  // Now the form gets deleted and created again. It should get autofilled
  // again.
  const std::string delete_form =
      "var form = document.getElementById('dynamic_form_id');"
      "form.parentNode.removeChild(form);";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), delete_form));
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), create_form));
  WaitForElementValue("username_id", "temp");
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForPushStateWhenFormDisappears) {
  NavigateToFile("/password/password_push_state.html");

  // Verify that we show the save password prompt if 'history.pushState()'
  // is called after form submission is suppressed by, for example, calling
  // preventDefault() in a form's submit event handler.
  // Note that calling 'submit()' on a form with javascript doesn't call
  // the onsubmit handler, so we click the submit button instead.
  // Also note that the prompt will only show up if the form disappers
  // after submission
  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Similar to the case above, but this time the form persists after
// 'history.pushState()'. And save password prompt should not show up
// in this case.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoPromptForPushStateWhenFormPersists) {
  NavigateToFile("/password/password_push_state.html");

  // Set |should_delete_testform| to false to keep submitted form visible after
  // history.pushsTate();
  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "should_delete_testform = false;"
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

// The password manager should distinguish forms with empty actions. After
// successful login, the login form disappears, but the another one shouldn't be
// recognized as the login form. The save prompt should appear.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForPushStateWhenFormWithEmptyActionDisappears) {
  NavigateToFile("/password/password_push_state.html");

  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('ea_username_field').value = 'temp';"
      "document.getElementById('ea_password_field').value = 'random';"
      "document.getElementById('ea_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Similar to the case above, but this time the form persists after
// 'history.pushState()'. The password manager should find the login form even
// if the action of the form is empty. Save password prompt should not show up.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForPushStateWhenFormWithEmptyActionPersists) {
  NavigateToFile("/password/password_push_state.html");

  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "should_delete_testform = false;"
      "document.getElementById('ea_username_field').value = 'temp';"
      "document.getElementById('ea_password_field').value = 'random';"
      "document.getElementById('ea_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

// Current and target URLs contain different parameters and references. This
// test checks that parameters and references in origins are ignored for
// form origin comparison.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    PromptForPushStateWhenFormDisappears_ParametersInOrigins) {
  NavigateToFile("/password/password_push_state.html?login#r");

  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "add_parameters_to_target_url = true;"
      "document.getElementById('pa_username_field').value = 'temp';"
      "document.getElementById('pa_password_field').value = 'random';"
      "document.getElementById('pa_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Similar to the case above, but this time the form persists after
// 'history.pushState()'. The password manager should find the login form even
// if target and current URLs contain different parameters or references.
// Save password prompt should not show up.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    PromptForPushStateWhenFormPersists_ParametersInOrigins) {
  NavigateToFile("/password/password_push_state.html?login#r");

  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "should_delete_testform = false;"
      "add_parameters_to_target_url = true;"
      "document.getElementById('pa_username_field').value = 'temp';"
      "document.getElementById('pa_password_field').value = 'random';"
      "document.getElementById('pa_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       InFrameNavigationDoesNotClearPopupState) {
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("random123");
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/password_form.html");

  // Mock out the AutofillClient so we know how long to wait. Unfortunately
  // there isn't otherwise a good event to wait on to verify that the popup
  // would have been shown.
  password_manager::ContentPasswordManagerDriverFactory* driver_factory =
      password_manager::ContentPasswordManagerDriverFactory::FromWebContents(
          WebContents());
  ObservingAutofillClient::CreateForWebContents(WebContents());
  ObservingAutofillClient* observing_autofill_client =
      ObservingAutofillClient::FromWebContents(WebContents());
  password_manager::ContentPasswordManagerDriver* driver =
      driver_factory->GetDriverForFrame(RenderViewHost()->GetMainFrame());
  DCHECK(driver);
  driver->GetPasswordAutofillManager()->set_autofill_client(
      observing_autofill_client);

  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var usernameRect = document.getElementById('username_field')"
      ".getBoundingClientRect();"));

  // Trigger in page navigation.
  std::string in_page_navigate = "location.hash = '#blah';";
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       in_page_navigate));

  // Click on the username field to display the popup.
  int top;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send(usernameRect.top);", &top));
  int left;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send(usernameRect.left);", &left));

  content::SimulateMouseClickAt(WebContents(), 0,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(left + 1, top + 1));
  // Make sure the popup would be shown.
  observing_autofill_client->WaitForAutofillPopup();
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangePwdFormBubbleShown) {
  NavigateToFile("/password/password_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('chg_username_field').value = 'temp';"
      "document.getElementById('chg_password_field').value = 'random';"
      "document.getElementById('chg_new_password_1').value = 'random1';"
      "document.getElementById('chg_new_password_2').value = 'random1';"
      "document.getElementById('chg_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangePwdFormPushStateBubbleShown) {
  NavigateToFile("/password/password_push_state.html");

  NavigationObserver observer(WebContents());
  observer.set_quit_on_entry_committed(true);
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('chg_username_field').value = 'temp';"
      "document.getElementById('chg_password_field').value = 'random';"
      "document.getElementById('chg_new_password_1').value = 'random1';"
      "document.getElementById('chg_new_password_2').value = 'random1';"
      "document.getElementById('chg_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoPromptOnBack) {
  // Go to a successful landing page through submitting first, so that it is
  // reachable through going back, and the remembered page transition is form
  // submit. There is no need to submit non-empty strings.
  NavigateToFile("/password/password_form.html");

  NavigationObserver dummy_submit_observer(WebContents());
  std::string just_submit =
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), just_submit));
  dummy_submit_observer.Wait();

  // Now go to a page with a form again, fill the form, and go back instead of
  // submitting it.
  NavigateToFile("/password/dummy_submit.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  // The (dummy) submit is necessary to provisionally save the typed password. A
  // user typing in the password field would not need to submit to provisionally
  // save it, but the script cannot trigger that just by assigning to the
  // field's value.
  std::string fill_and_back =
      "document.getElementById('password_field').value = 'random';"
      "document.getElementById('input_submit_button').click();"
      "window.history.back();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_back));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
}

// Regression test for http://crbug.com/452306
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangingTextToPasswordFieldOnSignupForm) {
  NavigateToFile("/password/signup_form.html");

  // In this case, pretend that username_field is actually a password field
  // that starts as a text field to simulate placeholder.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string change_and_submit =
      "document.getElementById('other_info').value = 'username';"
      "document.getElementById('username_field').type = 'password';"
      "document.getElementById('username_field').value = 'mypass';"
      "document.getElementById('password_field').value = 'mypass';"
      "document.getElementById('testform').submit();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), change_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Regression test for http://crbug.com/451631
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       SavingOnManyPasswordFieldsTest) {
  // Simulate Macy's registration page, which contains the normal 2 password
  // fields for confirming the new password plus 2 more fields for security
  // questions and credit card. Make sure that saving works correctly for such
  // sites.
  NavigateToFile("/password/many_password_signup_form.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'username';"
      "document.getElementById('password_field').value = 'mypass';"
      "document.getElementById('confirm_field').value = 'mypass';"
      "document.getElementById('security_answer').value = 'hometown';"
      "document.getElementById('SSN').value = '1234';"
      "document.getElementById('testform').submit();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       SaveWhenIFrameDestroyedOnFormSubmit) {
  NavigateToFile("/password/frame_detached_on_submit.html");

  // Need to pay attention for a message that XHR has finished since there
  // is no navigation to wait for.
  content::DOMMessageQueue message_queue;

  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "var iframe = document.getElementById('login_iframe');"
      "var frame_doc = iframe.contentDocument;"
      "frame_doc.getElementById('username_field').value = 'temp';"
      "frame_doc.getElementById('password_field').value = 'random';"
      "frame_doc.getElementById('submit_button').click();";

  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  std::string message;
  while (message_queue.WaitForMessage(&message)) {
    if (message == "\"SUBMISSION_FINISHED\"")
      break;
  }

  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Tests that if a site embeds the login and signup forms into one <form>, the
// login form still gets autofilled.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForLoginSignupForm) {
  std::string submit =
      "document.getElementById('username').value = 'myusername';"
      "document.getElementById('password').value = 'mypassword';"
      "document.getElementById('submit').click();";
  VerifyPasswordIsSavedAndFilled("/password/login_signup_form.html",
                                 submit, "password", "mypassword");
}

// Check that we can fill in cases where <base href> is set and the action of
// the form is not set. Regression test for https://crbug.com/360230.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       BaseTagWithNoActionTest) {
  std::string submit =
      "document.getElementById('username_field').value = 'myusername';"
      "document.getElementById('password_field').value = 'mypassword';"
      "document.getElementById('submit_button').click();";
  VerifyPasswordIsSavedAndFilled("/password/password_xhr_submit.html",
                                 submit, "password_field", "mypassword");
}

// Check that a password form in an iframe of different origin will not be
// filled in until a user interact with the form.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       CrossSiteIframeNotFillTest) {
  // Here we need to dynamically create the iframe because the port
  // embedded_test_server ran on was dynamically allocated, so the iframe's src
  // attribute can only be determined at run time.
  NavigateToFile("/password/password_form_in_crosssite_iframe.html");
  NavigationObserver ifrm_observer(WebContents());
  ifrm_observer.SetPathToWaitFor("/password/crossite_iframe_content.html");
  std::string create_iframe = base::StringPrintf(
      "create_iframe("
          "'http://randomsite.net:%d/password/crossite_iframe_content.html');",
      embedded_test_server()->port());
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       create_iframe));
  ifrm_observer.Wait();

  // Store a password for autofill later
  NavigationObserver init_observer(WebContents());
  init_observer.SetPathToWaitFor("/password/done.html");
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string init_form = "sendMessage('fill_and_submit');";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), init_form));
  init_observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  // Visit the form again
  NavigationObserver reload_observer(WebContents());
  NavigateToFile("/password/password_form_in_crosssite_iframe.html");
  reload_observer.Wait();

  NavigationObserver ifrm_observer_2(WebContents());
  ifrm_observer_2.SetPathToWaitFor("/password/crossite_iframe_content.html");
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       create_iframe));
  ifrm_observer_2.Wait();

  // Verify username is not autofilled
  std::string empty_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), "sendMessage('get_username');", &empty_username));
  ASSERT_EQ("", empty_username);
  // Verify password is not autofilled
  std::string empty_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), "sendMessage('get_password');", &empty_password));
  ASSERT_EQ("", empty_password);

  // Simulate the user interaction in the iframe and verify autofill is not
  // triggered. Note this check is only best-effort because we don't know how
  // long to wait before we are certain that no autofill will be triggered.
  // Theoretically unexpected autofill can happen after this check.
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var iframeRect = document.getElementById("
      "'iframe').getBoundingClientRect();"));
  int top;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(iframeRect.top);",
      &top));
  int left;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send(iframeRect.left);", &left));

  content::SimulateMouseClickAt(WebContents(), 0,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(left + 1, top + 1));
  // Verify username is not autofilled
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), "sendMessage('get_username');", &empty_username));
  ASSERT_EQ("", empty_username);
  // Verify password is not autofilled
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), "sendMessage('get_password');", &empty_password));
  ASSERT_EQ("", empty_password);
}

// Check that a password form in an iframe of same origin will not be
// filled in until user interact with the iframe.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       SameOriginIframeAutoFillTest) {
  // Visit the sign-up form to store a password for autofill later
  NavigateToFile("/password/password_form_in_same_origin_iframe.html");
  NavigationObserver observer(WebContents());
  observer.SetPathToWaitFor("/password/done.html");
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));

  std::string submit =
      "var ifrmDoc = document.getElementById('iframe').contentDocument;"
      "ifrmDoc.getElementById('username_field').value = 'temp';"
      "ifrmDoc.getElementById('password_field').value = 'pa55w0rd';"
      "ifrmDoc.getElementById('input_submit_button').click();";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  // Visit the form again
  NavigationObserver reload_observer(WebContents());
  NavigateToFile("/password/password_form_in_same_origin_iframe.html");
  reload_observer.Wait();

  // Verify username is autofilled
  CheckElementValue("iframe", "username_field", "temp");

  // Verify password is not autofilled
  CheckElementValue("iframe", "password_field", "");

  // Simulate the user interaction in the iframe which should trigger autofill.
  // Click in the middle of the frame to avoid the border.
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var iframeRect = document.getElementById("
      "'iframe').getBoundingClientRect();"));
  int y;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send((iframeRect.top +"
      "iframeRect.bottom) / 2);",
      &y));
  int x;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(),
      "window.domAutomationController.send((iframeRect.left + iframeRect.right)"
      "/ 2);",
      &x));

  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(x, y));
  // Verify password has been autofilled
  WaitForElementValue("iframe", "password_field", "pa55w0rd");

  // Verify username has been autofilled
  CheckElementValue("iframe", "username_field", "temp");
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, NoFormElementTest) {
  std::string submit =
      "document.getElementById('username_field').value = 'myusername';"
      "document.getElementById('password_field').value = 'mypassword';"
      "send_xhr();";
  VerifyPasswordIsSavedAndFilled("/password/no_form_element.html",
                                 submit,
                                 "password_field",
                                 "mypassword");
}

// The password manager driver will kill processes when they try to access
// passwords of sites other than the site the process is dedicated to, under
// site isolation.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       CrossSitePasswordEnforcement) {
  // The code under test is only active under site isolation.
  if (!content::AreAllSitesIsolatedForTesting()) {
    return;
  }

  // Navigate the main frame.
  GURL main_frame_url = embedded_test_server()->GetURL(
      "/password/password_form_in_crosssite_iframe.html");
  NavigationObserver observer(WebContents());
  ui_test_utils::NavigateToURL(browser(), main_frame_url);
  observer.Wait();

  // Create an iframe and navigate cross-site.
  NavigationObserver iframe_observer(WebContents());
  iframe_observer.SetPathToWaitFor("/password/crossite_iframe_content.html");
  GURL iframe_url = embedded_test_server()->GetURL(
      "foo.com", "/password/crossite_iframe_content.html");
  std::string create_iframe =
      base::StringPrintf("create_iframe('%s');", iframe_url.spec().c_str());
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       create_iframe));
  iframe_observer.Wait();

  // The iframe should get its own process.
  content::RenderFrameHost* main_frame = WebContents()->GetMainFrame();
  content::RenderFrameHost* iframe = iframe_observer.render_frame_host();
  content::SiteInstance* main_site_instance = main_frame->GetSiteInstance();
  content::SiteInstance* iframe_site_instance = iframe->GetSiteInstance();
  EXPECT_NE(main_site_instance, iframe_site_instance);
  EXPECT_NE(main_frame->GetProcess(), iframe->GetProcess());

  content::RenderProcessHostWatcher iframe_killed(
      iframe->GetProcess(),
      content::RenderProcessHostWatcher::WATCH_FOR_PROCESS_EXIT);

  // Try to get cross-site passwords from the subframe's process and wait for it
  // to be killed.
  std::vector<autofill::PasswordForm> password_forms;
  password_forms.push_back(autofill::PasswordForm());
  password_forms.back().origin = main_frame_url;
  ContentPasswordManagerDriverFactory* factory =
      ContentPasswordManagerDriverFactory::FromWebContents(WebContents());
  EXPECT_TRUE(factory);
  ContentPasswordManagerDriver* driver = factory->GetDriverForFrame(iframe);
  EXPECT_TRUE(driver);
  driver->PasswordFormsParsed(password_forms);

  iframe_killed.Wait();
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangePwdNoAccountStored) {
  NavigateToFile("/password/password_form.html");

  // Fill a form and submit through a <input type="submit"> button.
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));

  std::string fill_and_submit =
      "document.getElementById('chg_password_wo_username_field').value = "
      "'old_pw';"
      "document.getElementById('chg_new_password_wo_username_1').value = "
      "'new_pw';"
      "document.getElementById('chg_new_password_wo_username_2').value = "
      "'new_pw';"
      "document.getElementById('chg_submit_wo_username_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  // No credentials stored before, so save bubble is shown.
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();
  // Check that credentials are stored.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  WaitForPasswordStore();
  EXPECT_FALSE(password_store->IsEmpty());
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16(""),
                             base::ASCIIToUTF16("new_pw"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangePwd1AccountStored) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.password_value = base::ASCIIToUTF16("pw");
  signin_form.username_value = base::ASCIIToUTF16("temp");
  password_store->AddLogin(signin_form);

  // Check that password update bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit_change_password =
      "document.getElementById('chg_password_wo_username_field').value = "
      "'random';"
      "document.getElementById('chg_new_password_wo_username_1').value = "
      "'new_pw';"
      "document.getElementById('chg_new_password_wo_username_2').value = "
      "'new_pw';"
      "document.getElementById('chg_submit_wo_username_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(),
                                     fill_and_submit_change_password));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingUpdatePrompt());

  // We emulate that the user clicks "Update" button.
  const autofill::PasswordForm& pending_credentials =
      ManagePasswordsUIController::FromWebContents(WebContents())
          ->GetPendingPassword();
  prompt_observer->AcceptUpdatePrompt(pending_credentials);

  WaitForPasswordStore();
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("new_pw"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordOverridenUpdateBubbleShown) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("pw");
  password_store->AddLogin(signin_form);

  // Disable autofill. If a password is autofilled then all the Javacript
  // changes are discarded. The test would not be able to feed the new password
  // below.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(features::kFillOnAccountSelect);

  // Check that password update bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'new_pw';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  // The stored password "pw" was overriden with "new_pw", so update prompt is
  // expected.
  EXPECT_TRUE(prompt_observer->IsShowingUpdatePrompt());

  const autofill::PasswordForm stored_form =
      password_store->stored_passwords().begin()->second[0];
  prompt_observer->AcceptUpdatePrompt(stored_form);
  WaitForPasswordStore();
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("new_pw"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordNotOverridenUpdateBubbleNotShown) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("pw");
  password_store->AddLogin(signin_form);

  // Check that password update bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username_field').value = 'temp';"
      "document.getElementById('password_field').value = 'pw';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  // The stored password "pw" was not overriden, so update prompt is not
  // expected.
  EXPECT_FALSE(prompt_observer->IsShowingUpdatePrompt());
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("pw"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       ChangePwdWhenTheFormContainNotUsernameTextfield) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.password_value = base::ASCIIToUTF16("pw");
  signin_form.username_value = base::ASCIIToUTF16("temp");
  password_store->AddLogin(signin_form);

  // Check that password update bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit_change_password =
      "document.getElementById('chg_text_field').value = '3';"
      "document.getElementById('chg_password_withtext_field').value"
      " = 'random';"
      "document.getElementById('chg_new_password_withtext_username_1').value"
      " = 'new_pw';"
      "document.getElementById('chg_new_password_withtext_username_2').value"
      " = 'new_pw';"
      "document.getElementById('chg_submit_withtext_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(),
                                     fill_and_submit_change_password));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingUpdatePrompt());

  const autofill::PasswordForm stored_form =
      password_store->stored_passwords().begin()->second[0];
  prompt_observer->AcceptUpdatePrompt(stored_form);
  WaitForPasswordStore();
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("new_pw"));
}

// Test whether the password form with the username and password fields having
// ambiguity in id attribute gets autofilled correctly.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    AutofillSuggestionsForPasswordFormWithAmbiguousIdAttribute) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having ambiguous Ids for username and
  // password fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById('ambiguous_form').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("myusername", actual_username);

  std::string get_password =
      "window.domAutomationController.send("
      "  document.getElementById('ambiguous_form').elements[1].value);";
  std::string actual_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_password, &actual_password));
  EXPECT_EQ("mypassword", actual_password);
}

// Test whether the password form having username and password fields without
// name and id attribute gets autofilled correctly.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    AutofillSuggestionsForPasswordFormWithoutNameOrIdAttribute) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having no Ids for username and password
  // fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById('no_name_id_form').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("myusername", actual_username);

  std::string get_password =
      "window.domAutomationController.send("
      "  document.getElementById('no_name_id_form').elements[1].value);";
  std::string actual_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_password, &actual_password));
  EXPECT_EQ("mypassword", actual_password);
}

// Test whether the change password form having username and password fields
// without name and id attribute gets autofilled correctly.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForChangePwdWithEmptyNames) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having no Ids for username and password
  // fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_autocomplete').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("myusername", actual_username);

  std::string get_password =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_autocomplete').elements[1].value);";
  std::string actual_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_password, &actual_password));
  EXPECT_EQ("mypassword", actual_password);

  std::string get_new_password =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_autocomplete').elements[2].value);";
  std::string new_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_new_password, &new_password));
  EXPECT_EQ("", new_password);
}

// Test whether the change password form having username and password fields
// with empty names but having |autocomplete='current-password'| gets autofilled
// correctly.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    AutofillSuggestionsForChangePwdWithEmptyNamesAndAutocomplete) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having no Ids for username and password
  // fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById('change_pwd').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("myusername", actual_username);

  std::string get_password =
      "window.domAutomationController.send("
      "  document.getElementById('change_pwd').elements[1].value);";
  std::string actual_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_password, &actual_password));
  EXPECT_EQ("mypassword", actual_password);

  std::string get_new_password =
      "window.domAutomationController.send("
      "  document.getElementById('change_pwd').elements[2].value);";
  std::string new_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_new_password, &new_password));
  EXPECT_EQ("", new_password);
}

// Test whether the change password form having username and password fields
// with empty names but having only new password fields having
// |autocomplete='new-password'| atrribute do not get autofilled.
IN_PROC_BROWSER_TEST_F(
    PasswordManagerBrowserTestBase,
    AutofillSuggestionsForChangePwdWithEmptyNamesButOnlyNewPwdField) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having no Ids for username and password
  // fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_old_pwd').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("", actual_username);

  std::string get_new_password =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_old_pwd').elements[1].value);";
  std::string new_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_new_password, &new_password));
  EXPECT_EQ("", new_password);

  std::string get_retype_password =
      "window.domAutomationController.send("
      "  document.getElementById("
      "    'change_pwd_but_no_old_pwd').elements[2].value);";
  std::string retyped_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_retype_password, &retyped_password));
  EXPECT_EQ("", retyped_password);
}

// When there are multiple LoginModelObservers (e.g., multiple HTTP auth dialogs
// as in http://crbug.com/537823), ensure that credentials from PasswordStore
// distributed to them are filtered by the realm.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       BasicAuthSeparateRealms) {
  // We must use a new test server here because embedded_test_server() is
  // already started at this point and adding the request handler to it would
  // not be thread safe.
  net::EmbeddedTestServer http_test_server;
  http_test_server.RegisterRequestHandler(base::Bind(&HandleTestAuthRequest));
  ASSERT_TRUE(http_test_server.Start());

  // Save credentials for "test realm" in the store.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm creds;
  creds.scheme = autofill::PasswordForm::SCHEME_BASIC;
  creds.signon_realm = http_test_server.base_url().spec() + "test realm";
  creds.password_value = base::ASCIIToUTF16("pw");
  creds.username_value = base::ASCIIToUTF16("temp");
  password_store->AddLogin(creds);
  WaitForPasswordStore();
  ASSERT_FALSE(password_store->IsEmpty());

  // In addition to the LoginModelObserver created automatically for the HTTP
  // auth dialog, also create a mock observer, for a different realm.
  MockLoginModelObserver mock_login_model_observer;
  PasswordManager* password_manager =
      static_cast<password_manager::PasswordManagerClient*>(
          ChromePasswordManagerClient::FromWebContents(WebContents()))
          ->GetPasswordManager();
  autofill::PasswordForm other_form(creds);
  other_form.signon_realm = "https://example.com/other realm";
  password_manager->AddObserverAndDeliverCredentials(&mock_login_model_observer,
                                                     other_form);
  // The mock observer should not receive the stored credentials.
  EXPECT_CALL(mock_login_model_observer, OnAutofillDataAvailableInternal(_, _))
      .Times(0);

  // Now wait until the navigation to the test server causes a HTTP auth dialog
  // to appear.
  content::NavigationController* nav_controller =
      &WebContents()->GetController();
  WindowedAuthNeededObserver auth_needed_observer(nav_controller);
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), http_test_server.GetURL("/basic_auth"),
      WindowOpenDisposition::CURRENT_TAB, ui_test_utils::BROWSER_TEST_NONE);
  auth_needed_observer.Wait();

  // The auth dialog caused a query to PasswordStore, make sure it was
  // processed.
  WaitForPasswordStore();

  password_manager->RemoveObserver(&mock_login_model_observer);
}

// Test whether the password form which is loaded as hidden is autofilled
// correctly. This happens very often in situations when in order to sign-in the
// user clicks a sign-in button and a hidden passsword form becomes visible.
// This test differs from AutofillSuggestionsForProblematicPasswordForm in that
// the form is hidden and in that test only some fields are hidden.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsHiddenPasswordForm) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the hidden password form and verify whether username and
  // password is autofilled.
  NavigateToFile("/password/password_form.html");

  CheckElementValue("hidden_password_form_username", "myusername");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling the password.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  WaitForElementValue("hidden_password_form_password", "mypassword");
}

// Test whether the password form with the problematic invisible password field
// gets autofilled correctly.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForProblematicPasswordForm) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form with a hidden password field and verify
  // whether username and password is autofilled.
  NavigateToFile("/password/password_form.html");

  CheckElementValue("form_with_hidden_password_username", "myusername");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling the password.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  WaitForElementValue("form_with_hidden_password_password", "mypassword");
}

// Test whether the password form with the problematic invisible password field
// in ambiguous password form gets autofilled correctly.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForProblematicAmbiguousPasswordForm) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::PasswordStore> password_store =
      PasswordStoreFactory::GetForProfile(browser()->profile(),
                                          ServiceAccessType::IMPLICIT_ACCESS);
  autofill::PasswordForm login_form;
  login_form.signon_realm = embedded_test_server()->base_url().spec();
  login_form.action = embedded_test_server()->GetURL("/password/done.html");
  login_form.username_value = base::ASCIIToUTF16("myusername");
  login_form.password_value = base::ASCIIToUTF16("mypassword");
  password_store->AddLogin(login_form);

  // Now, navigate to the password form having ambiguous Ids for username and
  // password fields and verify whether username and password is autofilled.
  NavigateToFile("/password/ambiguous_password_form.html");

  // Let the user interact with the page, so that DOM gets modification events,
  // needed for autofilling fields.
  content::SimulateMouseClickAt(
      WebContents(), 0, blink::WebMouseEvent::Button::kLeft, gfx::Point(1, 1));

  std::string get_username =
      "window.domAutomationController.send("
      "  document.getElementById('hidden_password_form').elements[0].value);";
  std::string actual_username;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_username, &actual_username));
  EXPECT_EQ("myusername", actual_username);

  std::string get_password =
      "window.domAutomationController.send("
      "  document.getElementById('hidden_password_form').elements[2].value);";
  std::string actual_password;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractString(
      RenderFrameHost(), get_password, &actual_password));
  EXPECT_EQ("mypassword", actual_password);
}

// Check that the internals page contains logs from the renderer.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, InternalsPage_Renderer) {
  // Open the internals page.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://password-manager-internals"),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  content::WebContents* internals_web_contents = WebContents();

  // Open some page with a HTML form.
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/password/password_form.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  content::WebContents* forms_web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  // The renderer queries the availability of logging on start-up. However, it
  // can take too long to propagate that message from the browser back to the
  // renderer. The renderer might have attempted logging in the meantime.
  // Therefore the page with the form is reloaded to increase the likelihood
  // that the availability query was answered before the logging during page
  // load.
  NavigationObserver observer(forms_web_contents);
  forms_web_contents->ReloadFocusedFrame(false);
  observer.Wait();

  std::string find_logs =
      "var text = document.getElementById('log-entries').innerText;"
      "var logs_found = /PasswordAutofillAgent::/.test(text);"
      "window.domAutomationController.send(logs_found);";
  bool logs_found = false;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractBool(
      internals_web_contents->GetMainFrame(), find_logs, &logs_found));
  EXPECT_TRUE(logs_found);
}

// Check that the internals page contains logs from the browser.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, InternalsPage_Browser) {
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("chrome://password-manager-internals"),
      WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);
  content::WebContents* internals_web_contents = WebContents();

  ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/password/password_form.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_NAVIGATION);

  std::string find_logs =
      "var text = document.getElementById('log-entries').innerText;"
      "var logs_found = /PasswordManager::/.test(text);"
      "window.domAutomationController.send(logs_found);";
  bool logs_found = false;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractBool(
      internals_web_contents->GetMainFrame(), find_logs, &logs_found));
  EXPECT_TRUE(logs_found);
}

// Tests that submitted credentials are saved on a password form without
// username element when there are no stored credentials.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordRetryFormSaveNoUsernameCredentials) {
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  // Check that password save bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('retry_password_field').value = 'pw';"
      "document.getElementById('retry_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
  prompt_observer->AcceptSavePrompt();

  WaitForPasswordStore();
  CheckThatCredentialsStored(password_store.get(), base::string16(),
                             base::ASCIIToUTF16("pw"));
}

// Tests that no bubble shown when a password form without username submitted
// and there is stored credentials with the same password.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordRetryFormNoBubbleWhenPasswordTheSame) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("pw");
  password_store->AddLogin(signin_form);
  signin_form.username_value = base::ASCIIToUTF16("temp1");
  signin_form.password_value = base::ASCIIToUTF16("pw1");
  password_store->AddLogin(signin_form);

  // Check that no password bubble is shown when the submitted password is the
  // same in one of the stored credentials.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('retry_password_field').value = 'pw';"
      "document.getElementById('retry_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());
  EXPECT_FALSE(prompt_observer->IsShowingUpdatePrompt());
}

// Tests that the update bubble shown when a password form without username is
// submitted and there are stored credentials but with different password.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PasswordRetryFormUpdateBubbleShown) {
  // At first let us save credentials to the PasswordManager.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.username_value = base::ASCIIToUTF16("temp");
  signin_form.password_value = base::ASCIIToUTF16("pw");
  password_store->AddLogin(signin_form);

  // Check that password update bubble is shown.
  NavigateToFile("/password/password_form.html");
  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('retry_password_field').value = 'new_pw';"
      "document.getElementById('retry_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  // The new password "new_pw" is used, so update prompt is expected.
  EXPECT_TRUE(prompt_observer->IsShowingUpdatePrompt());

  const autofill::PasswordForm stored_form =
      password_store->stored_passwords().begin()->second[0];
  prompt_observer->AcceptUpdatePrompt(stored_form);

  WaitForPasswordStore();
  CheckThatCredentialsStored(password_store.get(), base::ASCIIToUTF16("temp"),
                             base::ASCIIToUTF16("new_pw"));
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       NoCrashWhenNavigatingWithOpenAccountPicker) {
  // Save credentials with 'skip_zero_click'.
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.password_value = base::ASCIIToUTF16("password");
  signin_form.username_value = base::ASCIIToUTF16("user");
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.skip_zero_click = true;
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/password_form.html");

  // Call the API to trigger the notification to the client, which raises the
  // account picker dialog.
  ASSERT_TRUE(content::ExecuteScript(
      RenderViewHost(),
      "navigator.credentials.get({password: true})"));

  // Navigate while the picker is open.
  NavigateToFile("/password/password_form.html");

  // No crash!
}

// Tests that the prompt to save the password is still shown if the fields have
// the "autocomplete" attribute set off.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       PromptForSubmitWithAutocompleteOff) {
  NavigateToFile("/password/password_autocomplete_off_test.html");

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit =
      "document.getElementById('username').value = 'temp';"
      "document.getElementById('password').value = 'random';"
      "document.getElementById('submit').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(), fill_and_submit));
  observer.Wait();
  EXPECT_TRUE(prompt_observer->IsShowingSavePrompt());
}

// Tests that password suggestions still work if the fields have the
// "autocomplete" attribute set to off.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       AutofillSuggestionsForPasswordFormWithAutocompleteOff) {
  std::string submit =
      "document.getElementById('username').value = 'temp';"
      "document.getElementById('password').value = 'mypassword';"
      "document.getElementById('submit').click();";
  VerifyPasswordIsSavedAndFilled(
      "/password/password_autocomplete_off_test.html", submit, "password",
      "mypassword");
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       SkipZeroClickNotToggledAfterSuccessfulSubmissionWithAPI)
{
  // Save credentials with 'skip_zero_click'
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.password_value = base::ASCIIToUTF16("password");
  signin_form.username_value = base::ASCIIToUTF16("user");
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.skip_zero_click = true;
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/password_form.html");

  // Call the API to trigger the notification to the client.
  ASSERT_TRUE(content::ExecuteScript(
      RenderViewHost(),
      "navigator.credentials.get({password: true, unmediated: true })"));

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit_change_password =
      "document.getElementById('username_field').value = 'user';"
      "document.getElementById('password_field').value = 'password';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(),
                                     fill_and_submit_change_password));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());

  // Verify that the form's 'skip_zero_click' is not updated.
  auto& passwords_map = password_store->stored_passwords();
  ASSERT_EQ(1u, passwords_map.size());
  auto& passwords_vector = passwords_map.begin()->second;
  ASSERT_EQ(1u, passwords_vector.size());
  const autofill::PasswordForm& form = passwords_vector[0];
  EXPECT_EQ(base::ASCIIToUTF16("user"), form.username_value);
  EXPECT_EQ(base::ASCIIToUTF16("password"), form.password_value);
  EXPECT_TRUE(form.skip_zero_click);
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase,
                       SkipZeroClickNotToggledAfterSuccessfulAutofill) {
  // Save credentials with 'skip_zero_click'
  scoped_refptr<password_manager::TestPasswordStore> password_store =
      static_cast<password_manager::TestPasswordStore*>(
          PasswordStoreFactory::GetForProfile(
              browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS)
              .get());
  autofill::PasswordForm signin_form;
  signin_form.signon_realm = embedded_test_server()->base_url().spec();
  signin_form.password_value = base::ASCIIToUTF16("password");
  signin_form.username_value = base::ASCIIToUTF16("user");
  signin_form.origin = embedded_test_server()->base_url();
  signin_form.skip_zero_click = true;
  password_store->AddLogin(signin_form);

  NavigateToFile("/password/password_form.html");

  // No API call.

  NavigationObserver observer(WebContents());
  std::unique_ptr<BubbleObserver> prompt_observer(
      new BubbleObserver(WebContents()));
  std::string fill_and_submit_change_password =
      "document.getElementById('username_field').value = 'user';"
      "document.getElementById('password_field').value = 'password';"
      "document.getElementById('input_submit_button').click()";
  ASSERT_TRUE(content::ExecuteScript(RenderViewHost(),
                                     fill_and_submit_change_password));
  observer.Wait();
  EXPECT_FALSE(prompt_observer->IsShowingSavePrompt());

  // Verify that the form's 'skip_zero_click' is not updated.
  auto& passwords_map = password_store->stored_passwords();
  ASSERT_EQ(1u, passwords_map.size());
  auto& passwords_vector = passwords_map.begin()->second;
  ASSERT_EQ(1u, passwords_vector.size());
  const autofill::PasswordForm& form = passwords_vector[0];
  EXPECT_EQ(base::ASCIIToUTF16("user"), form.username_value);
  EXPECT_EQ(base::ASCIIToUTF16("password"), form.password_value);
  EXPECT_TRUE(form.skip_zero_click);
}

IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestBase, ReattachWebContents) {
  auto detached_web_contents = base::WrapUnique(content::WebContents::Create(
      content::WebContents::CreateParams(WebContents()->GetBrowserContext())));
  NavigationObserver observer(detached_web_contents.get());
  detached_web_contents->GetController().LoadURL(
      embedded_test_server()->GetURL("/password/multi_frames.html"),
      content::Referrer(), ::ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
  observer.Wait();
  // Ensure that there is at least one more frame created than just the main
  // frame.
  EXPECT_LT(1u, detached_web_contents->GetAllFrames().size());

  auto* tab_strip_model = browser()->tab_strip_model();
  // Check that the autofill and password manager driver factories are notified
  // about all frames, not just the main one. The factories should receive
  // messages for non-main frames, in particular
  // AutofillHostMsg_PasswordFormsParsed. If that were the first time the
  // factories hear about such frames, this would crash.
  tab_strip_model->AddWebContents(detached_web_contents.release(), -1,
                                  ::ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
                                  TabStripModel::ADD_ACTIVE);
}

// Verify the Form-Not-Secure warning is shown on a non-secure username field.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestWarning,
                       ShowFormNotSecureOnUsernameField) {
  ASSERT_TRUE(
      base::FeatureList::IsEnabled(security_state::kHttpFormWarningFeature));

  // We need to serve from a non-localhost context for the form to be treated as
  // Not Secure.
  NavigationObserver observer(WebContents());
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "example.com", "/password/password_form.html"));
  observer.Wait();

  // Mock the autofill client.
  password_manager::ContentPasswordManagerDriverFactory* driver_factory =
      password_manager::ContentPasswordManagerDriverFactory::FromWebContents(
          WebContents());
  ObservingAutofillClient::CreateForWebContents(WebContents());
  ObservingAutofillClient* observing_autofill_client =
      ObservingAutofillClient::FromWebContents(WebContents());
  password_manager::ContentPasswordManagerDriver* driver =
      driver_factory->GetDriverForFrame(RenderViewHost()->GetMainFrame());
  DCHECK(driver);
  driver->GetPasswordAutofillManager()->set_autofill_client(
      observing_autofill_client);

  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var inputRect = document.getElementById('username_field_no_name')"
      ".getBoundingClientRect();"));

  // Click on the username field to verify the warning is shown.
  int top;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(inputRect.top);",
      &top));
  int left;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(inputRect.left);",
      &left));

  const char kHistogram[] =
      "PasswordManager.ShowedFormNotSecureWarningOnCurrentNavigation";
  base::HistogramTester histograms;

  content::SimulateMouseClickAt(WebContents(), 0,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(left + 1, top + 1));
  // Ensure the warning would be shown.
  observing_autofill_client->WaitForAutofillPopup();
  // Ensure the histogram was updated.
  histograms.ExpectUniqueSample(kHistogram, true, 1);
}

// Verify the Form-Not-Secure warning is not shown on a non-credential field.
IN_PROC_BROWSER_TEST_F(PasswordManagerBrowserTestWarning,
                       DoNotShowFormNotSecureOnUnrelatedField) {
  ASSERT_TRUE(
      base::FeatureList::IsEnabled(security_state::kHttpFormWarningFeature));

  // We need to serve from a non-localhost context for the form to be treated as
  // Not Secure.
  NavigationObserver observer(WebContents());
  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "example.com", "/password/password_form.html"));
  observer.Wait();

  // Mock the autofill client.
  password_manager::ContentPasswordManagerDriverFactory* driver_factory =
      password_manager::ContentPasswordManagerDriverFactory::FromWebContents(
          WebContents());
  ObservingAutofillClient::CreateForWebContents(WebContents());
  ObservingAutofillClient* observing_autofill_client =
      ObservingAutofillClient::FromWebContents(WebContents());
  password_manager::ContentPasswordManagerDriver* driver =
      driver_factory->GetDriverForFrame(RenderViewHost()->GetMainFrame());
  DCHECK(driver);
  driver->GetPasswordAutofillManager()->set_autofill_client(
      observing_autofill_client);

  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(
      RenderFrameHost(),
      "var inputRect = document.getElementById('ef_extra')"
      ".getBoundingClientRect();"));

  // Click on the non-username text field.
  int top;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(inputRect.top);",
      &top));
  int left;
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGestureAndExtractInt(
      RenderFrameHost(), "window.domAutomationController.send(inputRect.left);",
      &left));

  const char kHistogram[] =
      "PasswordManager.ShowedFormNotSecureWarningOnCurrentNavigation";
  base::HistogramTester histograms;

  content::SimulateMouseClickAt(WebContents(), 0,
                                blink::WebMouseEvent::Button::kLeft,
                                gfx::Point(left + 1, top + 1));
  // Force a round-trip.
  ASSERT_TRUE(content::ExecuteScriptWithoutUserGesture(RenderFrameHost(),
                                                       "var noop = 'noop';"));
  // Ensure the warning was not triggered.
  content::RunAllBlockingPoolTasksUntilIdle();
  ASSERT_FALSE(observing_autofill_client->popup_shown());
  // Ensure the histogram remains empty.
  histograms.ExpectTotalCount(kHistogram, 0);
}

}  // namespace password_manager
