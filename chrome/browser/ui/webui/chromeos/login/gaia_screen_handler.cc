// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/gaia_screen_handler.h"

#include "ash/system/devicetype_utils.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_scheduler/post_task.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "chrome/browser/chromeos/login/lock_screen_utils.h"
#include "chrome/browser/chromeos/login/screens/network_error.h"
#include "chrome/browser/chromeos/login/ui/user_adding_screen.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/net/network_portal_detector_impl.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/proto/chrome_device_policy.pb.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/ui/webui/chromeos/login/enrollment_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "chrome/browser/ui/webui/signin/signin_utils.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/login/auth/authpolicy_login_helper.h"
#include "chromeos/login/auth/user_context.h"
#include "chromeos/settings/cros_settings_names.h"
#include "chromeos/system/devicetype.h"
#include "chromeos/system/version_loader.h"
#include "components/login/localized_values_builder.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_manager.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_urls.h"
#include "ui/base/ime/chromeos/input_method_manager.h"
#include "ui/base/ime/chromeos/input_method_util.h"

using content::BrowserThread;
namespace em = enterprise_management;

namespace chromeos {

namespace {

const char kJsScreenPath[] = "login.GaiaSigninScreen";
const char kAuthIframeParentName[] = "signin-frame";

const char kRestrictiveProxyURL[] = "https://www.google.com/generate_204";

const char kEndpointGen[] = "1.0";

// The possible modes that the Gaia signin screen can be in.
enum GaiaScreenMode {
  // Default Gaia authentication will be used.
  GAIA_SCREEN_MODE_DEFAULT = 0,

  // Gaia offline mode will be used.
  GAIA_SCREEN_MODE_OFFLINE = 1,

  // An interstitial page will be used before SAML redirection.
  GAIA_SCREEN_MODE_SAML_INTERSTITIAL = 2,

  // Offline UI for Active Directory authentication.
  GAIA_SCREEN_MODE_AD = 3,
};

policy::DeviceMode GetDeviceMode() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  return connector->GetDeviceMode();
}

GaiaScreenMode GetGaiaScreenMode(const std::string& email, bool use_offline) {
  if (GetDeviceMode() == policy::DEVICE_MODE_ENTERPRISE_AD)
    return GAIA_SCREEN_MODE_AD;

  if (use_offline)
    return GAIA_SCREEN_MODE_OFFLINE;

  int authentication_behavior = 0;
  CrosSettings::Get()->GetInteger(kLoginAuthenticationBehavior,
                                  &authentication_behavior);
  if (authentication_behavior ==
      em::LoginAuthenticationBehaviorProto::SAML_INTERSTITIAL) {
    if (email.empty())
      return GAIA_SCREEN_MODE_SAML_INTERSTITIAL;

    // If there's a populated email, we must check first that this user is using
    // SAML in order to decide whether to show the interstitial page.
    const user_manager::User* user = user_manager::UserManager::Get()->FindUser(
        user_manager::known_user::GetAccountId(email, std::string() /* id */,
                                               AccountType::UNKNOWN));

    if (user && user->using_saml())
      return GAIA_SCREEN_MODE_SAML_INTERSTITIAL;
  }

  return GAIA_SCREEN_MODE_DEFAULT;
}

std::string GetEnterpriseDisplayDomain() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  return connector->GetEnterpriseDisplayDomain();
}

std::string GetEnterpriseEnrollmentDomain() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  return connector->GetEnterpriseEnrollmentDomain();
}

std::string GetRealm() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  return connector->GetRealm();
}

std::string GetChromeType() {
  switch (chromeos::GetDeviceType()) {
    case chromeos::DeviceType::kChromebox:
      return "chromebox";
    case chromeos::DeviceType::kChromebase:
      return "chromebase";
    case chromeos::DeviceType::kChromebit:
      return "chromebit";
    case chromeos::DeviceType::kChromebook:
      return "chromebook";
    default:
      return "chromedevice";
  }
}

void UpdateAuthParams(base::DictionaryValue* params,
                      bool is_restrictive_proxy) {
  CrosSettings* cros_settings = CrosSettings::Get();
  bool allow_new_user = true;
  cros_settings->GetBoolean(kAccountsPrefAllowNewUser, &allow_new_user);
  bool allow_guest = true;
  cros_settings->GetBoolean(kAccountsPrefAllowGuest, &allow_guest);
  params->SetBoolean("guestSignin", allow_guest);

  // nosignup flow if new users are not allowed.
  if (!allow_new_user || is_restrictive_proxy)
    params->SetString("flow", "nosignup");

  // Allow supervised user creation only if:
  // 1. Enterprise managed device > is allowed by policy.
  // 2. Consumer device > owner exists.
  // 3. New users are allowed by owner.
  // 4. Supervised users are allowed by owner.
  ChromeUserManager* user_manager = ChromeUserManager::Get();
  bool supervised_users_can_create =
      base::FeatureList::IsEnabled(features::kSupervisedUserCreation) &&
      user_manager->AreSupervisedUsersAllowed() && allow_new_user &&
      !user_manager->GetUsersAllowedForSupervisedUsersCreation().empty();
  params->SetBoolean("supervisedUsersCanCreate", supervised_users_can_create);

  // Now check whether we're in multi-profiles user adding scenario and
  // disable GAIA right panel features if that's the case.
  if (UserAddingScreen::Get()->IsRunning()) {
    params->SetBoolean("guestSignin", false);
    params->SetBoolean("supervisedUsersCanCreate", false);
  }
}

void RecordSAMLScrapingVerificationResultInHistogram(bool success) {
  UMA_HISTOGRAM_BOOLEAN("ChromeOS.SAML.Scraping.VerificationResult", success);
}

// The Task posted to PostTaskAndReply in StartClearingDnsCache on the IO
// thread.
void ClearDnsCache(IOThread* io_thread) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (browser_shutdown::IsTryingToQuit())
    return;

  io_thread->ClearHostCache(base::Callback<bool(const std::string&)>());
}

void PushFrontIMIfNotExists(const std::string& input_method,
                            std::vector<std::string>* input_methods) {
  if (input_method.empty())
    return;

  if (!base::ContainsValue(*input_methods, input_method))
    input_methods->insert(input_methods->begin(), input_method);
}

bool IsOnline(NetworkPortalDetector::CaptivePortalStatus status) {
  return status == NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_ONLINE;
}

}  // namespace

// A class that's used to specify the way how Gaia should be loaded.
struct GaiaScreenHandler::GaiaContext {
  GaiaContext();

  // Forces Gaia to reload.
  bool force_reload = false;

  // Whether Gaia should be loaded in offline mode.
  bool use_offline = false;

  // Email of the current user.
  std::string email;

  // GAIA ID of the current user.
  std::string gaia_id;

  // GAPS cookie.
  std::string gaps_cookie;
};

GaiaScreenHandler::GaiaContext::GaiaContext() {}

GaiaScreenHandler::GaiaScreenHandler(
    CoreOobeView* core_oobe_view,
    const scoped_refptr<NetworkStateInformer>& network_state_informer)
    : BaseScreenHandler(kScreenId),
      network_state_informer_(network_state_informer),
      core_oobe_view_(core_oobe_view),
      weak_factory_(this) {
  DCHECK(network_state_informer_.get());
  set_call_js_prefix(kJsScreenPath);
}

GaiaScreenHandler::~GaiaScreenHandler() {
  if (network_portal_detector_)
    network_portal_detector_->RemoveObserver(this);
}

void GaiaScreenHandler::MaybePreloadAuthExtension() {
  // We shall not have network portal detector initialized, which unnecessarily
  // polls captive portal checking URL if we don't need to load gaia. See
  // go/bad-portal for more context.
  if (!signin_screen_handler_->ShouldLoadGaia())
    return;

  VLOG(1) << "MaybePreloadAuthExtension";

  if (!network_portal_detector_) {
    NetworkPortalDetectorImpl* detector = new NetworkPortalDetectorImpl(
        g_browser_process->system_request_context(), false);
    detector->set_portal_test_url(GURL(kRestrictiveProxyURL));
    network_portal_detector_.reset(detector);
    network_portal_detector_->AddObserver(this);
    network_portal_detector_->Enable(true);
  }

  // If cookies clearing was initiated or |dns_clear_task_running_| then auth
  // extension showing has already been initiated and preloading is pointless.
  if (!gaia_silent_load_ && !cookies_cleared_ && !dns_clear_task_running_ &&
      network_state_informer_->state() == NetworkStateInformer::ONLINE) {
    gaia_silent_load_ = true;
    gaia_silent_load_network_ = network_state_informer_->network_path();
    LoadAuthExtension(true /* force */, false /* offline */);
  }
}

void GaiaScreenHandler::DisableRestrictiveProxyCheckForTest() {
  disable_restrictive_proxy_check_for_test_ = true;
}

void GaiaScreenHandler::LoadGaia(const GaiaContext& context) {
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BACKGROUND},
      base::Bind(&version_loader::GetVersion, version_loader::VERSION_SHORT),
      base::Bind(&GaiaScreenHandler::LoadGaiaWithVersion,
                 weak_factory_.GetWeakPtr(), context));
}

void GaiaScreenHandler::LoadGaiaWithVersion(
    const GaiaContext& context,
    const std::string& platform_version) {
  base::DictionaryValue params;

  params.SetBoolean("forceReload", context.force_reload);
  params.SetString("gaiaId", context.gaia_id);
  params.SetBoolean("readOnlyEmail", true);
  params.SetString("email", context.email);
  params.SetString("gapsCookie", context.gaps_cookie);

  UpdateAuthParams(&params, IsRestrictiveProxy());

  GaiaScreenMode screen_mode = GetGaiaScreenMode(context.email,
                                                 context.use_offline);
  params.SetInteger("screenMode", screen_mode);

  if (screen_mode == GAIA_SCREEN_MODE_AD && !authpolicy_login_helper_)
    authpolicy_login_helper_ = base::MakeUnique<AuthPolicyLoginHelper>();

  if (screen_mode != GAIA_SCREEN_MODE_OFFLINE) {
    const std::string app_locale = g_browser_process->GetApplicationLocale();
    if (!app_locale.empty())
      params.SetString("hl", app_locale);
  }

  std::string realm(GetRealm());
  if (!realm.empty()) {
    params.SetString("realm", realm);
  }

  const std::string enterprise_display_domain(GetEnterpriseDisplayDomain());
  const std::string enterprise_enrollment_domain(
      GetEnterpriseEnrollmentDomain());
  if (!enterprise_display_domain.empty())
    params.SetString("enterpriseDisplayDomain", enterprise_display_domain);
  if (!enterprise_enrollment_domain.empty()) {
    params.SetString("enterpriseEnrollmentDomain",
                     enterprise_enrollment_domain);
  }

  params.SetString("chromeType", GetChromeType());
  params.SetString("clientId",
                   GaiaUrls::GetInstance()->oauth2_chrome_client_id());
  params.SetString("clientVersion", version_info::GetVersionNumber());
  if (!platform_version.empty())
    params.SetString("platformVersion", platform_version);
  params.SetString("releaseChannel", chrome::GetChannelString());
  params.SetString("endpointGen", kEndpointGen);

  std::string email_domain;
  if (CrosSettings::Get()->GetString(kAccountsPrefLoginScreenDomainAutoComplete,
                                     &email_domain) &&
      !email_domain.empty()) {
    params.SetString("emailDomain", email_domain);
  }

  params.SetString("gaiaUrl", GaiaUrls::GetInstance()->gaia_url().spec());

  if (use_easy_bootstrap_) {
    params.SetBoolean("useEafe", true);
    // Easy login overrides.
    std::string eafe_url = "https://easylogin.corp.google.com/";
    base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    if (command_line->HasSwitch(switches::kEafeUrl))
      eafe_url = command_line->GetSwitchValueASCII(switches::kEafeUrl);
    std::string eafe_path = "planters/cbaudioChrome";
    if (command_line->HasSwitch(switches::kEafePath))
      eafe_path = command_line->GetSwitchValueASCII(switches::kEafePath);

    params.SetString("gaiaUrl", eafe_url);
    params.SetString("gaiaPath", eafe_path);
  }

  // Easy bootstrap is not v2-compatible
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kCrosGaiaApiV1) ||
      use_easy_bootstrap_) {
    params.SetString("chromeOSApiVersion", "1");
  } else {
    // This enables GLIF MM UI for the online Gaia screen by default.
    // (see https://crbug.com/709244 ).
    params.SetString("chromeOSApiVersion", "2");
  }

  frame_state_ = FRAME_STATE_LOADING;
  CallJS("loadAuthExtension", params);
}

void GaiaScreenHandler::ReloadGaia(bool force_reload) {
  if (frame_state_ == FRAME_STATE_LOADING && !force_reload) {
    VLOG(1) << "Skipping reloading of Gaia since gaia is loading.";
    return;
  }
  NetworkStateInformer::State state = network_state_informer_->state();
  if (state != NetworkStateInformer::ONLINE &&
      !signin_screen_handler_->proxy_auth_dialog_need_reload_) {
    VLOG(1) << "Skipping reloading of Gaia since network state="
            << NetworkStateInformer::StatusString(state);
    return;
  }

  signin_screen_handler_->proxy_auth_dialog_need_reload_ = false;
  VLOG(1) << "Reloading Gaia.";
  frame_state_ = FRAME_STATE_LOADING;
  LoadAuthExtension(force_reload, false /* offline */);
}

void GaiaScreenHandler::MonitorOfflineIdle(bool is_online) {
  CallJS("monitorOfflineIdle", is_online);
}

void GaiaScreenHandler::DeclareLocalizedValues(
    ::login::LocalizedValuesBuilder* builder) {
  builder->Add("signinScreenTitle", IDS_SIGNIN_SCREEN_TITLE_TAB_PROMPT);
  builder->Add("guestSignin", IDS_BROWSE_WITHOUT_SIGNING_IN_HTML);
  builder->Add("backButton", IDS_ACCNAME_BACK);
  builder->Add("closeButton", IDS_CLOSE);
  builder->Add("whitelistErrorConsumer", IDS_LOGIN_ERROR_WHITELIST);
  builder->Add("whitelistErrorEnterprise",
               IDS_ENTERPRISE_LOGIN_ERROR_WHITELIST);
  builder->Add("tryAgainButton", IDS_WHITELIST_ERROR_TRY_AGAIN_BUTTON);
  builder->Add("learnMoreButton", IDS_WHITELIST_ERROR_LEARN_MORE_BUTTON);
  builder->Add("gaiaLoading", IDS_LOGIN_GAIA_LOADING_MESSAGE);

  // Strings used by the SAML fatal error dialog.
  builder->Add("fatalErrorMessageNoAccountDetails",
               IDS_LOGIN_FATAL_ERROR_NO_ACCOUNT_DETAILS);
  builder->Add("fatalErrorMessageNoPassword",
               IDS_LOGIN_FATAL_ERROR_NO_PASSWORD);
  builder->Add("fatalErrorMessageVerificationFailed",
               IDS_LOGIN_FATAL_ERROR_PASSWORD_VERIFICATION);
  builder->Add("fatalErrorMessageInsecureURL",
               IDS_LOGIN_FATAL_ERROR_TEXT_INSECURE_URL);
  builder->Add("fatalErrorDoneButton", IDS_DONE);
  builder->Add("fatalErrorTryAgainButton",
               IDS_LOGIN_FATAL_ERROR_TRY_AGAIN_BUTTON);

  builder->AddF("loginWelcomeMessage", IDS_LOGIN_WELCOME_MESSAGE,
                ash::GetChromeOSDeviceTypeResourceId());
  builder->Add("offlineLoginEmail", IDS_OFFLINE_LOGIN_EMAIL);
  builder->Add("offlineLoginPassword", IDS_OFFLINE_LOGIN_PASSWORD);
  builder->Add("offlineLoginInvalidEmail", IDS_OFFLINE_LOGIN_INVALID_EMAIL);
  builder->Add("offlineLoginInvalidPassword",
               IDS_OFFLINE_LOGIN_INVALID_PASSWORD);
  builder->Add("offlineLoginNextBtn", IDS_OFFLINE_LOGIN_NEXT_BUTTON_TEXT);
  builder->Add("offlineLoginForgotPasswordBtn",
               IDS_OFFLINE_LOGIN_FORGOT_PASSWORD_BUTTON_TEXT);
  builder->Add("offlineLoginForgotPasswordDlg",
               IDS_OFFLINE_LOGIN_FORGOT_PASSWORD_DIALOG_TEXT);
  builder->Add("offlineLoginCloseBtn", IDS_OFFLINE_LOGIN_CLOSE_BUTTON_TEXT);
  builder->Add("enterpriseInfoMessage", IDS_LOGIN_DEVICE_MANAGED_BY_NOTICE);
  builder->Add("samlInterstitialMessage",
                IDS_LOGIN_SAML_INTERSTITIAL_MESSAGE);
  builder->Add("samlInterstitialChangeAccountLink",
               IDS_LOGIN_SAML_INTERSTITIAL_CHANGE_ACCOUNT_LINK_TEXT);
  builder->Add("samlInterstitialNextBtn",
               IDS_LOGIN_SAML_INTERSTITIAL_NEXT_BUTTON_TEXT);

  builder->Add("adAuthWelcomeMessage", IDS_AD_DOMAIN_AUTH_WELCOME_MESSAGE);
  builder->Add("adAuthLoginUsername", IDS_AD_AUTH_LOGIN_USER);
  builder->Add("adLoginPassword", IDS_AD_LOGIN_PASSWORD);
}

void GaiaScreenHandler::Initialize() {
}

void GaiaScreenHandler::RegisterMessages() {
  AddCallback("webviewLoadAborted",
              &GaiaScreenHandler::HandleWebviewLoadAborted);
  AddCallback("completeLogin", &GaiaScreenHandler::HandleCompleteLogin);
  AddCallback("completeAuthentication",
              &GaiaScreenHandler::HandleCompleteAuthentication);
  AddCallback("completeAuthenticationAuthCodeOnly",
              &GaiaScreenHandler::HandleCompleteAuthenticationAuthCodeOnly);
  AddCallback("usingSAMLAPI", &GaiaScreenHandler::HandleUsingSAMLAPI);
  AddCallback("scrapedPasswordCount",
              &GaiaScreenHandler::HandleScrapedPasswordCount);
  AddCallback("scrapedPasswordVerificationFailed",
              &GaiaScreenHandler::HandleScrapedPasswordVerificationFailed);
  AddCallback("loginWebuiReady", &GaiaScreenHandler::HandleGaiaUIReady);
  AddCallback("toggleEasyBootstrap",
              &GaiaScreenHandler::HandleToggleEasyBootstrap);
  AddCallback("identifierEntered", &GaiaScreenHandler::HandleIdentifierEntered);
  AddCallback("updateOfflineLogin",
              &GaiaScreenHandler::set_offline_login_is_active);
  AddCallback("authExtensionLoaded",
              &GaiaScreenHandler::HandleAuthExtensionLoaded);
  AddCallback("completeAdAuthentication",
              &GaiaScreenHandler::HandleCompleteAdAuthentication);
  AddCallback("completeAdPasswordChange",
              &GaiaScreenHandler::HandleCompleteAdPasswordChange);
  AddCallback("cancelAdAuthentication",
              &GaiaScreenHandler::HandleCancelActiveDirectoryAuth);
}

void GaiaScreenHandler::OnPortalDetectionCompleted(
    const NetworkState* network,
    const NetworkPortalDetector::CaptivePortalState& state) {
  VLOG(1) << "OnPortalDetectionCompleted "
          << NetworkPortalDetector::CaptivePortalStatusString(state.status);

  const NetworkPortalDetector::CaptivePortalStatus previous_status =
      captive_portal_status_;
  captive_portal_status_ = state.status;
  if (offline_login_is_active() ||
      IsOnline(captive_portal_status_) == IsOnline(previous_status) ||
      disable_restrictive_proxy_check_for_test_ ||
      GetCurrentScreen() != kScreenId)
    return;

  LoadAuthExtension(true /* force */, false /* offline */);
}

void GaiaScreenHandler::HandleIdentifierEntered(const std::string& user_email) {
  if (!Delegate()->IsUserWhitelisted(user_manager::known_user::GetAccountId(
          user_email, std::string() /* id */, AccountType::UNKNOWN)))
    ShowWhitelistCheckFailedError();
}

void GaiaScreenHandler::HandleAuthExtensionLoaded() {
  VLOG(1) << "Auth extension finished loading";
  auth_extension_being_loaded_ = false;
}

void GaiaScreenHandler::HandleWebviewLoadAborted(
    const std::string& error_reason_str) {
  // TODO(nkostylev): Switch to int code once webview supports that.
  // http://crbug.com/470483
  if (error_reason_str == "ERR_ABORTED") {
    LOG(WARNING) << "Ignoring Gaia webview error: " << error_reason_str;
    return;
  }

  // TODO(nkostylev): Switch to int code once webview supports that.
  // http://crbug.com/470483
  // Extract some common codes used by SigninScreenHandler for now.
  if (error_reason_str == "ERR_NAME_NOT_RESOLVED")
    frame_error_ = net::ERR_NAME_NOT_RESOLVED;
  else if (error_reason_str == "ERR_INTERNET_DISCONNECTED")
    frame_error_ = net::ERR_INTERNET_DISCONNECTED;
  else if (error_reason_str == "ERR_NETWORK_CHANGED")
    frame_error_ = net::ERR_NETWORK_CHANGED;
  else if (error_reason_str == "ERR_INTERNET_DISCONNECTED")
    frame_error_ = net::ERR_INTERNET_DISCONNECTED;
  else if (error_reason_str == "ERR_PROXY_CONNECTION_FAILED")
    frame_error_ = net::ERR_PROXY_CONNECTION_FAILED;
  else if (error_reason_str == "ERR_TUNNEL_CONNECTION_FAILED")
    frame_error_ = net::ERR_TUNNEL_CONNECTION_FAILED;
  else
    frame_error_ = net::ERR_INTERNET_DISCONNECTED;

  LOG(ERROR) << "Gaia webview error: " << error_reason_str;
  NetworkError::ErrorReason error_reason =
      NetworkError::ERROR_REASON_FRAME_ERROR;
  frame_state_ = FRAME_STATE_ERROR;
  UpdateState(error_reason);
}

AccountId GaiaScreenHandler::GetAccountId(
    const std::string& authenticated_email,
    const std::string& id,
    const AccountType& account_type) const {
  const std::string canonicalized_email =
      gaia::CanonicalizeEmail(gaia::SanitizeEmail(authenticated_email));

  const AccountId account_id = user_manager::known_user::GetAccountId(
      authenticated_email, id, account_type);

  if (account_id.GetUserEmail() != canonicalized_email) {
    LOG(WARNING) << "Existing user '" << account_id.GetUserEmail()
                 << "' authenticated by alias '" << canonicalized_email << "'.";
  }

  return account_id;
}

void GaiaScreenHandler::DoAdAuth(
    const std::string& username,
    const Key& key,
    authpolicy::ErrorType error,
    const authpolicy::ActiveDirectoryAccountInfo& account_info) {
  switch (error) {
    case authpolicy::ERROR_NONE: {
      DCHECK(account_info.has_account_id() &&
             !account_info.account_id().empty());
      const AccountId account_id(GetAccountId(
          username, account_info.account_id(), AccountType::ACTIVE_DIRECTORY));
      Delegate()->SetDisplayAndGivenName(account_info.display_name(),
                                         account_info.given_name());
      UserContext user_context(account_id);
      user_context.SetKey(key);
      user_context.SetAuthFlow(UserContext::AUTH_FLOW_ACTIVE_DIRECTORY);
      user_context.SetIsUsingOAuth(false);
      user_context.SetUserType(
          user_manager::UserType::USER_TYPE_ACTIVE_DIRECTORY);
      Delegate()->CompleteLogin(user_context);
      break;
    }
    case authpolicy::ERROR_PASSWORD_EXPIRED:
      core_oobe_view_->ShowActiveDirectoryPasswordChangeScreen(username);
      break;
    case authpolicy::ERROR_PARSE_UPN_FAILED:
    case authpolicy::ERROR_BAD_USER_NAME:
      CallJS("invalidateAd", username,
             static_cast<int>(ActiveDirectoryErrorState::BAD_USERNAME));
      return;
    case authpolicy::ERROR_BAD_PASSWORD:
      CallJS("invalidateAd", username,
             static_cast<int>(ActiveDirectoryErrorState::BAD_PASSWORD));
    case authpolicy::ERROR_UNKNOWN:
    case authpolicy::ERROR_DBUS_FAILURE:
    case authpolicy::ERROR_CANNOT_RESOLVE_KDC:
    case authpolicy::ERROR_KINIT_FAILED:
    case authpolicy::ERROR_NET_FAILED:
    case authpolicy::ERROR_SMBCLIENT_FAILED:
    case authpolicy::ERROR_PARSE_FAILED:
    case authpolicy::ERROR_PARSE_PREG_FAILED:
    case authpolicy::ERROR_BAD_GPOS:
    case authpolicy::ERROR_LOCAL_IO:
    case authpolicy::ERROR_NOT_JOINED:
    case authpolicy::ERROR_NOT_LOGGED_IN:
    case authpolicy::ERROR_STORE_POLICY_FAILED:
      LoadAuthExtension(true, false /* offline */);
      break;
    default:
      // TODO(rsorokin): Proper error handling.
      DLOG(WARNING) << "Unhandled error code: " << error;
      LoadAuthExtension(true, false /* offline */);
  }
}

void GaiaScreenHandler::HandleCompleteAdAuthentication(
    const std::string& username,
    const std::string& password) {
  Delegate()->SetDisplayEmail(username);
  set_populated_email(username);
  DCHECK(authpolicy_login_helper_);
  authpolicy_login_helper_->AuthenticateUser(
      username, std::string() /* object_guid */, password,
      base::BindOnce(&GaiaScreenHandler::DoAdAuth, weak_factory_.GetWeakPtr(),
                     username, Key(password)));
}

void GaiaScreenHandler::HandleCompleteAdPasswordChange(
    const std::string& username,
    const std::string& old_password,
    const std::string& new_password) {
  Delegate()->SetDisplayEmail(username);
  set_populated_email(username);

  DCHECK(authpolicy_login_helper_);
  authpolicy_login_helper_->AuthenticateUser(
      username, std::string() /* object_guid */,
      old_password + "\n" + new_password + "\n" + new_password,
      base::BindOnce(&GaiaScreenHandler::DoAdAuth, weak_factory_.GetWeakPtr(),
                     username, Key(new_password)));
}

void GaiaScreenHandler::HandleCancelActiveDirectoryAuth() {
  DCHECK(authpolicy_login_helper_);
  authpolicy_login_helper_->CancelRequestsAndRestart();
}

void GaiaScreenHandler::HandleCompleteAuthentication(
    const std::string& gaia_id,
    const std::string& email,
    const std::string& password,
    const std::string& auth_code,
    bool using_saml,
    const std::string& gaps_cookie) {
  if (!Delegate())
    return;

  DCHECK(!email.empty());
  DCHECK(!gaia_id.empty());
  const std::string sanitized_email = gaia::SanitizeEmail(email);
  Delegate()->SetDisplayEmail(sanitized_email);

  UserContext user_context(GetAccountId(email, gaia_id, AccountType::GOOGLE));
  user_context.SetKey(Key(password));
  user_context.SetAuthCode(auth_code);
  user_context.SetAuthFlow(using_saml
                               ? UserContext::AUTH_FLOW_GAIA_WITH_SAML
                               : UserContext::AUTH_FLOW_GAIA_WITHOUT_SAML);
  user_context.SetGAPSCookie(gaps_cookie);
  Delegate()->CompleteLogin(user_context);
}

void GaiaScreenHandler::HandleCompleteAuthenticationAuthCodeOnly(
    const std::string& auth_code) {
  if (!Delegate())
    return;

  UserContext user_context;
  user_context.SetAuthFlow(UserContext::AUTH_FLOW_EASY_BOOTSTRAP);
  user_context.SetAuthCode(auth_code);
  Delegate()->CompleteLogin(user_context);
}

void GaiaScreenHandler::HandleCompleteLogin(const std::string& gaia_id,
                                            const std::string& typed_email,
                                            const std::string& password,
                                            bool using_saml) {
  DoCompleteLogin(gaia_id, typed_email, password, using_saml);
}

void GaiaScreenHandler::HandleUsingSAMLAPI() {
  SetSAMLPrincipalsAPIUsed(true);
}

void GaiaScreenHandler::HandleScrapedPasswordCount(int password_count) {
  SetSAMLPrincipalsAPIUsed(false);
  // Use a histogram that has 11 buckets, one for each of the values in [0, 9]
  // and an overflow bucket at the end.
  UMA_HISTOGRAM_ENUMERATION(
      "ChromeOS.SAML.Scraping.PasswordCount", std::min(password_count, 10), 11);
  if (password_count == 0)
    HandleScrapedPasswordVerificationFailed();
}

void GaiaScreenHandler::HandleScrapedPasswordVerificationFailed() {
  RecordSAMLScrapingVerificationResultInHistogram(false);
}

void GaiaScreenHandler::HandleToggleEasyBootstrap() {
  use_easy_bootstrap_ = !use_easy_bootstrap_;
  LoadAuthExtension(true /* force */, false /* offline */);
}

void GaiaScreenHandler::HandleGaiaUIReady() {
  VLOG(1) << "Gaia is loaded";

  // As we could miss and window.onload could already be called, restore
  // focus to current pod (see crbug/175243).
  if (gaia_silent_load_)
    signin_screen_handler_->RefocusCurrentPod();

  frame_error_ = net::OK;
  frame_state_ = FRAME_STATE_LOADED;

  if (network_state_informer_->state() == NetworkStateInformer::ONLINE)
    UpdateState(NetworkError::ERROR_REASON_UPDATE);

  if (test_expects_complete_login_)
    SubmitLoginFormForTest();
  if (Delegate())
    Delegate()->OnGaiaScreenReady();
}

void GaiaScreenHandler::DoCompleteLogin(const std::string& gaia_id,
                                        const std::string& typed_email,
                                        const std::string& password,
                                        bool using_saml) {
  if (!Delegate())
    return;

  if (using_saml && !using_saml_api_)
    RecordSAMLScrapingVerificationResultInHistogram(true);

  DCHECK(!typed_email.empty());
  DCHECK(!gaia_id.empty());
  const std::string sanitized_email = gaia::SanitizeEmail(typed_email);
  Delegate()->SetDisplayEmail(sanitized_email);
  UserContext user_context(
      GetAccountId(typed_email, gaia_id, AccountType::GOOGLE));
  user_context.SetKey(Key(password));
  user_context.SetAuthFlow(using_saml
                               ? UserContext::AUTH_FLOW_GAIA_WITH_SAML
                               : UserContext::AUTH_FLOW_GAIA_WITHOUT_SAML);
  Delegate()->CompleteLogin(user_context);

  if (test_expects_complete_login_) {
    VLOG(2) << "Complete test login for " << typed_email
            << ", requested=" << test_user_;

    test_expects_complete_login_ = false;
    test_user_.clear();
    test_pass_.clear();
  }
}

void GaiaScreenHandler::StartClearingDnsCache() {
  if (dns_clear_task_running_ || !g_browser_process->io_thread())
    return;

  dns_cleared_ = false;
  BrowserThread::PostTaskAndReply(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(&ClearDnsCache, g_browser_process->io_thread()),
      base::BindOnce(&GaiaScreenHandler::OnDnsCleared,
                     weak_factory_.GetWeakPtr()));
  dns_clear_task_running_ = true;
}

void GaiaScreenHandler::OnDnsCleared() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  dns_clear_task_running_ = false;
  dns_cleared_ = true;
  ShowGaiaScreenIfReady();
}

void GaiaScreenHandler::StartClearingCookies(
    const base::Closure& on_clear_callback) {
  cookies_cleared_ = false;
  ProfileHelper* profile_helper = ProfileHelper::Get();
  LOG_ASSERT(Profile::FromWebUI(web_ui()) ==
             profile_helper->GetSigninProfile());
  profile_helper->ClearSigninProfile(
      base::Bind(&GaiaScreenHandler::OnCookiesCleared,
                 weak_factory_.GetWeakPtr(), on_clear_callback));
}

void GaiaScreenHandler::OnCookiesCleared(
    const base::Closure& on_clear_callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  cookies_cleared_ = true;
  on_clear_callback.Run();
}

void GaiaScreenHandler::ShowSigninScreenForTest(const std::string& username,
                                                const std::string& password) {
  VLOG(2) << "ShowSigninScreenForTest for user " << username
          << ", frame_state=" << frame_state();

  test_user_ = username;
  test_pass_ = password;
  test_expects_complete_login_ = true;

  // Submit login form for test if gaia is ready. If gaia is loading, login
  // will be attempted in HandleLoginWebuiReady after gaia is ready. Otherwise,
  // reload gaia then follow the loading case.
  if (frame_state() == GaiaScreenHandler::FRAME_STATE_LOADED) {
    SubmitLoginFormForTest();
  } else if (frame_state() != GaiaScreenHandler::FRAME_STATE_LOADING) {
    signin_screen_handler_->OnShowAddUser();
  }
}

void GaiaScreenHandler::SubmitLoginFormForTest() {
  VLOG(2) << "Submit login form for test, user=" << test_user_;

  content::RenderFrameHost* frame =
      signin::GetAuthFrame(web_ui()->GetWebContents(), kAuthIframeParentName);

  std::string code =
      "document.getElementById('identifier').value = '" + test_user_ + "';"
      "document.getElementById('nextButton').click();";
  frame->ExecuteJavaScriptForTests(base::ASCIIToUTF16(code));

  if (!test_pass_.empty()) {
    code = "document.getElementById('password').value = '" + test_pass_ + "';";
    code += "document.getElementById('nextButton').click();";
    frame->ExecuteJavaScriptForTests(base::ASCIIToUTF16(code));
  }

  // Test properties are cleared in HandleCompleteLogin because the form
  // submission might fail and login will not be attempted after reloading
  // if they are cleared here.
}

void GaiaScreenHandler::SetSAMLPrincipalsAPIUsed(bool api_used) {
  using_saml_api_ = api_used;
  UMA_HISTOGRAM_BOOLEAN("ChromeOS.SAML.APIUsed", api_used);
}

void GaiaScreenHandler::ShowGaiaAsync() {
  show_when_dns_and_cookies_cleared_ = true;
  if (gaia_silent_load_ && populated_email_.empty()) {
    dns_cleared_ = true;
    cookies_cleared_ = true;
    ShowGaiaScreenIfReady();
  } else {
    StartClearingDnsCache();
    StartClearingCookies(base::Bind(&GaiaScreenHandler::ShowGaiaScreenIfReady,
                                    weak_factory_.GetWeakPtr()));
  }
}

void GaiaScreenHandler::CancelShowGaiaAsync() {
  show_when_dns_and_cookies_cleared_ = false;
}

void GaiaScreenHandler::ShowGaiaScreenIfReady() {
  if (!dns_cleared_ ||
      !cookies_cleared_ ||
      !show_when_dns_and_cookies_cleared_ ||
      !Delegate()) {
    return;
  }

  std::string active_network_path = network_state_informer_->network_path();
  if (gaia_silent_load_ &&
      (network_state_informer_->state() != NetworkStateInformer::ONLINE ||
       gaia_silent_load_network_ != active_network_path)) {
    // Network has changed. Force Gaia reload.
    gaia_silent_load_ = false;
  }

  // Note that LoadAuthExtension clears |populated_email_|.
  if (populated_email_.empty()) {
    Delegate()->LoadSigninWallpaper();
  } else {
    Delegate()->LoadWallpaper(user_manager::known_user::GetAccountId(
        populated_email_, std::string() /* id */, AccountType::UNKNOWN));
  }

  input_method::InputMethodManager* imm =
      input_method::InputMethodManager::Get();

  scoped_refptr<input_method::InputMethodManager::State> gaia_ime_state =
      imm->GetActiveIMEState()->Clone();
  imm->SetState(gaia_ime_state);

  // Set Least Recently Used input method for the user.
  if (!populated_email_.empty()) {
    lock_screen_utils::SetUserInputMethod(populated_email_,
                                          gaia_ime_state.get());
  } else {
    std::vector<std::string> input_methods;
    if (gaia_ime_state->GetAllowedInputMethods().empty()) {
      input_methods =
          imm->GetInputMethodUtil()->GetHardwareLoginInputMethodIds();
    } else {
      input_methods = gaia_ime_state->GetAllowedInputMethods();
    }
    const std::string owner_im = lock_screen_utils::GetUserLastInputMethod(
        user_manager::UserManager::Get()->GetOwnerAccountId().GetUserEmail());
    const std::string system_im = g_browser_process->local_state()->GetString(
        language_prefs::kPreferredKeyboardLayout);

    PushFrontIMIfNotExists(owner_im, &input_methods);
    PushFrontIMIfNotExists(system_im, &input_methods);

    gaia_ime_state->EnableLoginLayouts(
        g_browser_process->GetApplicationLocale(), input_methods);

    if (!system_im.empty()) {
      gaia_ime_state->ChangeInputMethod(system_im, false /* show_message */);
    } else if (!owner_im.empty()) {
      gaia_ime_state->ChangeInputMethod(owner_im, false /* show_message */);
    }
  }

  LoadAuthExtension(!gaia_silent_load_ /* force */, false /* offline */);
  signin_screen_handler_->UpdateUIState(
      SigninScreenHandler::UI_STATE_GAIA_SIGNIN, nullptr);
  core_oobe_view_->UpdateKeyboardState();

  if (gaia_silent_load_) {
    // The variable is assigned to false because silently loaded Gaia page was
    // used.
    gaia_silent_load_ = false;
  }
  UpdateState(NetworkError::ERROR_REASON_UPDATE);

  if (core_oobe_view_) {
    PrefService* prefs = g_browser_process->local_state();
    if (prefs->GetBoolean(prefs::kFactoryResetRequested)) {
      core_oobe_view_->ShowDeviceResetScreen();
    } else if (prefs->GetBoolean(prefs::kDebuggingFeaturesRequested)) {
      core_oobe_view_->ShowEnableDebuggingScreen();
    }
  }
}

void GaiaScreenHandler::ShowWhitelistCheckFailedError() {
  base::DictionaryValue params;
  params.SetBoolean("enterpriseManaged",
                    g_browser_process->platform_part()
                        ->browser_policy_connector_chromeos()
                        ->IsEnterpriseManaged());
  CallJS("showWhitelistCheckFailedError", true, params);
}

void GaiaScreenHandler::LoadAuthExtension(bool force,
                                          bool offline) {
  VLOG(1) << "LoadAuthExtension, force: " << force
          << ", offline: " << offline;

  if (auth_extension_being_loaded_) {
    VLOG(1) << "Skip loading the Auth extension as it's already being loaded";
    return;
  }

  auth_extension_being_loaded_ = true;
  GaiaContext context;
  context.force_reload = force;
  context.use_offline = offline;
  context.email = populated_email_;

  std::string gaia_id;
  if (!context.email.empty() &&
      user_manager::known_user::FindGaiaID(
          AccountId::FromUserEmail(context.email), &gaia_id)) {
    context.gaia_id = gaia_id;
  }

  if (!context.email.empty()) {
    context.gaps_cookie = user_manager::known_user::GetGAPSCookie(
        AccountId::FromUserEmail(gaia::CanonicalizeEmail(context.email)));
  }

  populated_email_.clear();

  LoadGaia(context);
}

void GaiaScreenHandler::UpdateState(NetworkError::ErrorReason reason) {
  if (signin_screen_handler_)
    signin_screen_handler_->UpdateState(reason);
}

SigninScreenHandlerDelegate* GaiaScreenHandler::Delegate() {
  return signin_screen_handler_->delegate_;
}

bool GaiaScreenHandler::IsRestrictiveProxy() const {
  return !disable_restrictive_proxy_check_for_test_ &&
         !IsOnline(captive_portal_status_);
}

}  // namespace chromeos
