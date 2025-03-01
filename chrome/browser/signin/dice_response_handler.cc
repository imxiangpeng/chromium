// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/dice_response_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/singleton.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_tracker_service_factory.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_client.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/browser/signin_metrics.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "google_apis/gaia/gaia_auth_fetcher.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/google_service_auth_error.h"

const int kDiceTokenFetchTimeoutSeconds = 10;

namespace {

class DiceResponseHandlerFactory : public BrowserContextKeyedServiceFactory {
 public:
  // Returns an instance of the factory singleton.
  static DiceResponseHandlerFactory* GetInstance() {
    return base::Singleton<DiceResponseHandlerFactory>::get();
  }

  static DiceResponseHandler* GetForProfile(Profile* profile) {
    return static_cast<DiceResponseHandler*>(
        GetInstance()->GetServiceForBrowserContext(profile, true));
  }

 private:
  friend struct base::DefaultSingletonTraits<DiceResponseHandlerFactory>;

  DiceResponseHandlerFactory()
      : BrowserContextKeyedServiceFactory(
            "DiceResponseHandler",
            BrowserContextDependencyManager::GetInstance()) {
    DependsOn(AccountTrackerServiceFactory::GetInstance());
    DependsOn(ChromeSigninClientFactory::GetInstance());
    DependsOn(ProfileOAuth2TokenServiceFactory::GetInstance());
    DependsOn(SigninManagerFactory::GetInstance());
  }

  ~DiceResponseHandlerFactory() override {}

  // BrowserContextKeyedServiceFactory:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override {
    if (context->IsOffTheRecord())
      return nullptr;

    Profile* profile = static_cast<Profile*>(context);
    return new DiceResponseHandler(
        ChromeSigninClientFactory::GetForProfile(profile),
        SigninManagerFactory::GetForProfile(profile),
        ProfileOAuth2TokenServiceFactory::GetForProfile(profile),
        AccountTrackerServiceFactory::GetForProfile(profile));
  }
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// DiceTokenFetcher
////////////////////////////////////////////////////////////////////////////////

DiceResponseHandler::DiceTokenFetcher::DiceTokenFetcher(
    const std::string& gaia_id,
    const std::string& email,
    const std::string& authorization_code,
    SigninClient* signin_client,
    DiceResponseHandler* dice_response_handler)
    : gaia_id_(gaia_id),
      email_(email),
      authorization_code_(authorization_code),
      dice_response_handler_(dice_response_handler),
      timeout_closure_(
          base::Bind(&DiceResponseHandler::DiceTokenFetcher::OnTimeout,
                     base::Unretained(this))) {
  DCHECK(dice_response_handler_);
  gaia_auth_fetcher_ = signin_client->CreateGaiaAuthFetcher(
      this, GaiaConstants::kChromeSource,
      signin_client->GetURLRequestContext());
  gaia_auth_fetcher_->StartAuthCodeForOAuth2TokenExchange(authorization_code_);
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, timeout_closure_.callback(),
      base::TimeDelta::FromSeconds(kDiceTokenFetchTimeoutSeconds));
}

DiceResponseHandler::DiceTokenFetcher::~DiceTokenFetcher() {}

void DiceResponseHandler::DiceTokenFetcher::OnTimeout() {
  gaia_auth_fetcher_.reset();
  timeout_closure_.Cancel();
  dice_response_handler_->OnTokenExchangeFailure(
      this, GoogleServiceAuthError(GoogleServiceAuthError::REQUEST_CANCELED));
  // |this| may be deleted at this point.
}

void DiceResponseHandler::DiceTokenFetcher::OnClientOAuthSuccess(
    const GaiaAuthConsumer::ClientOAuthResult& result) {
  gaia_auth_fetcher_.reset();
  timeout_closure_.Cancel();
  dice_response_handler_->OnTokenExchangeSuccess(this, gaia_id_, email_,
                                                 result);
  // |this| may be deleted at this point.
}

void DiceResponseHandler::DiceTokenFetcher::OnClientOAuthFailure(
    const GoogleServiceAuthError& error) {
  gaia_auth_fetcher_.reset();
  timeout_closure_.Cancel();
  dice_response_handler_->OnTokenExchangeFailure(this, error);
  // |this| may be deleted at this point.
}

////////////////////////////////////////////////////////////////////////////////
// DiceResponseHandler
////////////////////////////////////////////////////////////////////////////////

// static
DiceResponseHandler* DiceResponseHandler::GetForProfile(Profile* profile) {
  return DiceResponseHandlerFactory::GetForProfile(profile);
}

DiceResponseHandler::DiceResponseHandler(
    SigninClient* signin_client,
    SigninManager* signin_manager,
    ProfileOAuth2TokenService* profile_oauth2_token_service,
    AccountTrackerService* account_tracker_service)
    : signin_manager_(signin_manager),
      signin_client_(signin_client),
      token_service_(profile_oauth2_token_service),
      account_tracker_service_(account_tracker_service) {
  DCHECK(signin_client_);
  DCHECK(signin_manager_);
  DCHECK(token_service_);
  DCHECK(account_tracker_service_);
}

DiceResponseHandler::~DiceResponseHandler() {}

void DiceResponseHandler::ProcessDiceHeader(
    const signin::DiceResponseParams& dice_params) {
  DCHECK(signin::IsDiceFixAuthErrorsEnabled());
  switch (dice_params.user_intention) {
    case signin::DiceAction::SIGNIN:
      ProcessDiceSigninHeader(dice_params.signin_info.gaia_id,
                              dice_params.signin_info.email,
                              dice_params.signin_info.authorization_code);
      return;
    case signin::DiceAction::SIGNOUT: {
      const signin::DiceResponseParams::SignoutInfo& signout_info =
          dice_params.signout_info;
      DCHECK_GT(signout_info.gaia_id.size(), 0u);
      DCHECK_EQ(signout_info.gaia_id.size(), signout_info.email.size());
      DCHECK_EQ(signout_info.gaia_id.size(), signout_info.session_index.size());
      ProcessDiceSignoutHeader(signout_info.gaia_id, signout_info.email);
      return;
    }
    case signin::DiceAction::NONE:
      NOTREACHED() << "Invalid Dice response parameters.";
      return;
  }

  NOTREACHED();
  return;
}

size_t DiceResponseHandler::GetPendingDiceTokenFetchersCountForTesting() const {
  return token_fetchers_.size();
}

bool DiceResponseHandler::CanGetTokenForAccount(const std::string& gaia_id,
                                                const std::string& email) {
  if (signin::IsAccountConsistencyDiceEnabled())
    return true;

  // When using kDiceFixAuthErrors, only get a token if the account matches
  // the current Chrome account.
  DCHECK_EQ(signin::AccountConsistencyMethod::kDiceFixAuthErrors,
            signin::GetAccountConsistencyMethod());
  std::string account =
      account_tracker_service_->PickAccountIdForAccount(gaia_id, email);
  std::string chrome_account = signin_manager_->GetAuthenticatedAccountId();
  bool can_get_token = (chrome_account == account);
  VLOG_IF(1, !can_get_token)
      << "[Dice] Dropping Dice signin response for " << account;
  return can_get_token;
}

void DiceResponseHandler::ProcessDiceSigninHeader(
    const std::string& gaia_id,
    const std::string& email,
    const std::string& authorization_code) {
  DCHECK(!gaia_id.empty());
  DCHECK(!email.empty());
  DCHECK(!authorization_code.empty());

  if (!CanGetTokenForAccount(gaia_id, email))
    return;

  for (auto it = token_fetchers_.begin(); it != token_fetchers_.end(); ++it) {
    if ((it->get()->gaia_id() == gaia_id) && (it->get()->email() == email) &&
        (it->get()->authorization_code() == authorization_code)) {
      return;  // There is already a request in flight with the same parameters.
    }
  }

  token_fetchers_.push_back(base::MakeUnique<DiceTokenFetcher>(
      gaia_id, email, authorization_code, signin_client_, this));
}

void DiceResponseHandler::ProcessDiceSignoutHeader(
    const std::vector<std::string>& gaia_ids,
    const std::vector<std::string>& emails) {
  DCHECK_EQ(gaia_ids.size(), emails.size());
  if (!signin::IsAccountConsistencyDiceEnabled()) {
    // Ignore signout responses when using kDiceFixAuthErrors.
    DCHECK_EQ(signin::AccountConsistencyMethod::kDiceFixAuthErrors,
              signin::GetAccountConsistencyMethod());
    return;
  }

  // If one of the signed out accounts is the main Chrome account, then force a
  // complete signout. Otherwise simply revoke the corresponding tokens.
  std::string current_account = signin_manager_->GetAuthenticatedAccountId();
  std::vector<std::string> signed_out_accounts;
  for (unsigned int i = 0; i < gaia_ids.size(); ++i) {
    std::string signed_out_account =
        account_tracker_service_->PickAccountIdForAccount(gaia_ids[i],
                                                          emails[i]);
    if (signed_out_account == current_account) {
      VLOG(1) << "[Dice] Signing out all accounts.";
      signin_manager_->SignOut(signin_metrics::SERVER_FORCED_DISABLE,
                               signin_metrics::SignoutDelete::IGNORE_METRIC);
      // Cancel all Dice token fetches currently in flight.
      token_fetchers_.clear();
      return;
    } else {
      signed_out_accounts.push_back(signed_out_account);
    }
  }

  for (const auto& account : signed_out_accounts) {
    VLOG(1) << "[Dice]: Revoking token for account: " << account;
    token_service_->RevokeCredentials(account);
    // If a token fetch is in flight for the same account, cancel it.
    for (auto it = token_fetchers_.begin(); it != token_fetchers_.end(); ++it) {
      std::string token_fetcher_account_id =
          account_tracker_service_->PickAccountIdForAccount(
              it->get()->gaia_id(), it->get()->email());
      if (token_fetcher_account_id == account) {
        token_fetchers_.erase(it);
        break;
      }
    }
  }
}

void DiceResponseHandler::DeleteTokenFetcher(DiceTokenFetcher* token_fetcher) {
  for (auto it = token_fetchers_.begin(); it != token_fetchers_.end(); ++it) {
    if (it->get() == token_fetcher) {
      token_fetchers_.erase(it);
      return;
    }
  }
  NOTREACHED();
}

void DiceResponseHandler::OnTokenExchangeSuccess(
    DiceTokenFetcher* token_fetcher,
    const std::string& gaia_id,
    const std::string& email,
    const GaiaAuthConsumer::ClientOAuthResult& result) {
  if (!CanGetTokenForAccount(gaia_id, email))
    return;

  std::string account_id =
      account_tracker_service_->SeedAccountInfo(gaia_id, email);
  VLOG(1) << "[Dice] OAuth success for account: " << account_id;
  token_service_->UpdateCredentials(account_id, result.refresh_token);
  DeleteTokenFetcher(token_fetcher);
}

void DiceResponseHandler::OnTokenExchangeFailure(
    DiceTokenFetcher* token_fetcher,
    const GoogleServiceAuthError& error) {
  // TODO(droger): Handle authentication errors.
  VLOG(1) << "[Dice] OAuth failed with error: " << error.ToString();
  DeleteTokenFetcher(token_fetcher);
}
