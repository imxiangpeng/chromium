// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/payments/payment_request.h"

#include <algorithm>

#include "base/containers/adapters.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/region_data_loader_impl.h"
#include "components/autofill/core/browser/validation.h"
#include "components/payments/core/autofill_payment_instrument.h"
#include "components/payments/core/currency_formatter.h"
#include "components/payments/core/payment_request_data_util.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/autofill/validation_rules_storage_factory.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/payments/payment_request_util.h"
#include "ios/chrome/browser/signin/signin_manager_factory.h"
#include "ios/web/public/payments/payment_request.h"
#include "ios/web/public/web_state/web_state.h"
#include "third_party/libaddressinput/chromium/chrome_metadata_source.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/source.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/storage.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

std::unique_ptr<::i18n::addressinput::Source> GetAddressInputSource(
    net::URLRequestContextGetter* url_context_getter) {
  return std::unique_ptr<::i18n::addressinput::Source>(
      new autofill::ChromeMetadataSource(I18N_ADDRESS_VALIDATION_DATA_URL,
                                         url_context_getter));
}

std::unique_ptr<::i18n::addressinput::Storage> GetAddressInputStorage() {
  return autofill::ValidationRulesStorageFactory::CreateStorage();
}

}  // namespace

namespace payments {

PaymentRequest::PaymentRequest(
    const web::PaymentRequest& web_payment_request,
    ios::ChromeBrowserState* browser_state,
    web::WebState* web_state,
    autofill::PersonalDataManager* personal_data_manager,
    id<PaymentRequestUIDelegate> payment_request_ui_delegate)
    : web_payment_request_(web_payment_request),
      browser_state_(browser_state),
      web_state_(web_state),
      personal_data_manager_(personal_data_manager),
      payment_request_ui_delegate_(payment_request_ui_delegate),
      address_normalizer_(
          GetAddressInputSource(
              personal_data_manager_->GetURLRequestContextGetter()),
          GetAddressInputStorage()),
      address_normalization_manager_(
          &address_normalizer_,
          autofill::AutofillCountry::CountryCodeForLocale(
              GetApplicationContext()->GetApplicationLocale())),
      selected_shipping_profile_(nullptr),
      selected_contact_profile_(nullptr),
      selected_payment_method_(nullptr),
      selected_shipping_option_(nullptr),
      profile_comparator_(GetApplicationLocale(), *this),
      journey_logger_(IsIncognito(), GetLastCommittedURL(), GetUkmRecorder()) {
  PopulateAvailableShippingOptions();
  PopulateProfileCache();
  PopulateAvailableProfiles();
  PopulatePaymentMethodCache();
  PopulateAvailablePaymentMethods();

  SetSelectedShippingOption();

  if (request_shipping()) {
    // If the merchant provided a default shipping option, and the
    // highest-ranking shipping profile is usable, select it.
    if (selected_shipping_option_ && !shipping_profiles_.empty() &&
        profile_comparator_.IsShippingComplete(shipping_profiles_[0])) {
      selected_shipping_profile_ = shipping_profiles_[0];
    }
  }

  if (request_payer_name() || request_payer_email() || request_payer_phone()) {
    // If the highest-ranking contact profile is usable, select it. Otherwise,
    // select none.
    if (!contact_profiles_.empty() &&
        profile_comparator_.IsContactInfoComplete(contact_profiles_[0])) {
      selected_contact_profile_ = contact_profiles_[0];
    }
  }

  const auto first_complete_payment_method =
      std::find_if(payment_methods_.begin(), payment_methods_.end(),
                   [this](PaymentInstrument* payment_method) {
                     return payment_method->IsCompleteForPayment();
                   });
  if (first_complete_payment_method != payment_methods_.end())
    selected_payment_method_ = *first_complete_payment_method;

  // Kickoff the process of loading the rules (which is asynchronous) for each
  // profile's country, to get faster address normalization later.
  for (const autofill::AutofillProfile* profile :
       personal_data_manager_->GetProfilesToSuggest()) {
    std::string countryCode =
        base::UTF16ToUTF8(profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
    if (autofill::data_util::IsValidCountryCode(countryCode)) {
      address_normalizer_.LoadRulesForRegion(countryCode);
    }
  }
}

PaymentRequest::~PaymentRequest() {}

bool PaymentRequest::Compare::operator()(
    const std::unique_ptr<PaymentRequest>& lhs,
    const std::unique_ptr<PaymentRequest>& rhs) const {
  return lhs->web_payment_request().payment_request_id !=
         rhs->web_payment_request().payment_request_id;
}

autofill::PersonalDataManager* PaymentRequest::GetPersonalDataManager() {
  return personal_data_manager_;
}

const std::string& PaymentRequest::GetApplicationLocale() const {
  return GetApplicationContext()->GetApplicationLocale();
}

bool PaymentRequest::IsIncognito() const {
  return browser_state_->IsOffTheRecord();
}

bool PaymentRequest::IsSslCertificateValid() {
  NOTREACHED() << "Implementation is never used";
  return false;
}

const GURL& PaymentRequest::GetLastCommittedURL() const {
  return web_state_->GetLastCommittedURL();
}

void PaymentRequest::DoFullCardRequest(
    const autofill::CreditCard& credit_card,
    base::WeakPtr<autofill::payments::FullCardRequest::ResultDelegate>
        result_delegate) {
  [payment_request_ui_delegate_ requestFullCreditCard:credit_card
                                       resultDelegate:result_delegate];
}

AddressNormalizer* PaymentRequest::GetAddressNormalizer() {
  return &address_normalizer_;
}

autofill::RegionDataLoader* PaymentRequest::GetRegionDataLoader() {
  return new autofill::RegionDataLoaderImpl(
      GetAddressInputSource(
          personal_data_manager_->GetURLRequestContextGetter())
          .release(),
      GetAddressInputStorage().release(), GetApplicationLocale());
}

ukm::UkmRecorder* PaymentRequest::GetUkmRecorder() {
  return GetApplicationContext()->GetUkmRecorder();
}

std::string PaymentRequest::GetAuthenticatedEmail() const {
  const SigninManager* signin_manager =
      ios::SigninManagerFactory::GetForBrowserStateIfExists(browser_state_);
  if (signin_manager && signin_manager->IsAuthenticated())
    return signin_manager->GetAuthenticatedAccountInfo().email;
  else
    return std::string();
}

PrefService* PaymentRequest::GetPrefService() {
  return browser_state_->GetPrefs();
}

void PaymentRequest::UpdatePaymentDetails(const web::PaymentDetails& details) {
  web_payment_request_.details = details;
  PopulateAvailableShippingOptions();
  SetSelectedShippingOption();
}

bool PaymentRequest::request_shipping() const {
  return web_payment_request_.options.request_shipping;
}

bool PaymentRequest::request_payer_name() const {
  return web_payment_request_.options.request_payer_name;
}

bool PaymentRequest::request_payer_phone() const {
  return web_payment_request_.options.request_payer_phone;
}

bool PaymentRequest::request_payer_email() const {
  return web_payment_request_.options.request_payer_email;
}

PaymentShippingType PaymentRequest::shipping_type() const {
  return web_payment_request_.options.shipping_type;
}

CurrencyFormatter* PaymentRequest::GetOrCreateCurrencyFormatter() {
  if (!currency_formatter_) {
    currency_formatter_.reset(new CurrencyFormatter(
        base::UTF16ToASCII(web_payment_request_.details.total.amount.currency),
        base::UTF16ToASCII(
            web_payment_request_.details.total.amount.currency_system),
        GetApplicationLocale()));
  }
  return currency_formatter_.get();
}

AddressNormalizationManager* PaymentRequest::GetAddressNormalizationManager() {
  return &address_normalization_manager_;
}

autofill::AutofillProfile* PaymentRequest::AddAutofillProfile(
    const autofill::AutofillProfile& profile) {
  profile_cache_.push_back(
      base::MakeUnique<autofill::AutofillProfile>(profile));

  contact_profiles_.push_back(profile_cache_.back().get());
  shipping_profiles_.push_back(profile_cache_.back().get());

  return profile_cache_.back().get();
}

void PaymentRequest::PopulateProfileCache() {
  const std::vector<autofill::AutofillProfile*>& profiles_to_suggest =
      personal_data_manager_->GetProfilesToSuggest();
  // Return early if the user has no stored Autofill profiles.
  if (profiles_to_suggest.empty())
    return;

  profile_cache_.clear();
  profile_cache_.reserve(profiles_to_suggest.size());

  for (const auto* profile : profiles_to_suggest) {
    profile_cache_.push_back(
        base::MakeUnique<autofill::AutofillProfile>(*profile));
  }
}

void PaymentRequest::PopulateAvailableProfiles() {
  if (profile_cache_.empty())
    return;

  std::vector<autofill::AutofillProfile*> raw_profiles_for_filtering;
  raw_profiles_for_filtering.reserve(profile_cache_.size());

  for (auto const& profile : profile_cache_) {
    raw_profiles_for_filtering.push_back(profile.get());
  }

  // Contact profiles are deduped and ordered by completeness.
  contact_profiles_ =
      profile_comparator_.FilterProfilesForContact(raw_profiles_for_filtering);

  // Shipping profiles are ordered by completeness.
  shipping_profiles_ =
      profile_comparator_.FilterProfilesForShipping(raw_profiles_for_filtering);
}

AutofillPaymentInstrument* PaymentRequest::AddAutofillPaymentInstrument(
    const autofill::CreditCard& credit_card) {
  std::string basic_card_issuer_network =
      autofill::data_util::GetPaymentRequestData(credit_card.network())
          .basic_card_issuer_network;

  if (!base::ContainsValue(supported_card_networks_,
                           basic_card_issuer_network) ||
      !supported_card_types_set_.count(credit_card.card_type())) {
    return nullptr;
  }

  // If the merchant specified the card network as part of the "basic-card"
  // payment method, use "basic-card" as the method_name. Otherwise, use
  // the name of the network directly.
  std::string method_name = basic_card_issuer_network;
  if (basic_card_specified_networks_.count(basic_card_issuer_network)) {
    method_name = "basic_card";
  }

  // The total number of card types: credit, debit, prepaid, unknown.
  constexpr size_t kTotalNumberOfCardTypes = 4U;

  // Whether the card type (credit, debit, prepaid) matches the type that the
  // merchant has requested exactly. This should be false for unknown card
  // types, if the merchant cannot accept some card types.
  bool matches_merchant_card_type_exactly =
      credit_card.card_type() != autofill::CreditCard::CARD_TYPE_UNKNOWN ||
      supported_card_types_set_.size() == kTotalNumberOfCardTypes;

  // AutofillPaymentInstrument makes a copy of |credit_card| so it is
  // effectively owned by this object.
  payment_method_cache_.push_back(base::MakeUnique<AutofillPaymentInstrument>(
      method_name, credit_card, matches_merchant_card_type_exactly,
      billing_profiles(), GetApplicationLocale(), this));

  payment_methods_.push_back(payment_method_cache_.back().get());

  return static_cast<AutofillPaymentInstrument*>(
      payment_method_cache_.back().get());
}

PaymentsProfileComparator* PaymentRequest::profile_comparator() {
  return &profile_comparator_;
}

const PaymentsProfileComparator* PaymentRequest::profile_comparator() const {
  // Return a const version of what the non-const |profile_comparator| method
  // returns.
  return const_cast<PaymentRequest*>(this)->profile_comparator();
}

bool PaymentRequest::CanMakePayment() const {
  for (PaymentInstrument* payment_method : payment_methods_) {
    if (payment_method->IsValidForCanMakePayment()) {
      return true;
    }
  }
  return false;
}

void PaymentRequest::InvokePaymentApp(
    id<PaymentResponseHelperConsumer> consumer) {
  DCHECK(selected_payment_method());
  response_helper_ = base::MakeUnique<PaymentResponseHelper>(consumer, this);
  selected_payment_method()->InvokePaymentApp(response_helper_.get());
}

bool PaymentRequest::IsPaymentAppInvoked() const {
  return !!response_helper_;
}

void PaymentRequest::RecordUseStats() {
  if (request_shipping()) {
    DCHECK(selected_shipping_profile_);
    personal_data_manager_->RecordUseOf(*selected_shipping_profile_);
  }

  if (request_payer_name() || request_payer_email() || request_payer_phone()) {
    DCHECK(selected_contact_profile_);
    // If the same address was used for both contact and shipping, the stats
    // should be updated only once.
    if (!request_shipping() || (selected_shipping_profile_->guid() !=
                                selected_contact_profile_->guid())) {
      personal_data_manager_->RecordUseOf(*selected_contact_profile_);
    }
  }

  selected_payment_method_->RecordUse();
}

void PaymentRequest::PopulatePaymentMethodCache() {
  for (const PaymentMethodData& method_data_entry :
       web_payment_request_.method_data) {
    for (const std::string& method : method_data_entry.supported_methods) {
      stringified_method_data_[method].insert(method_data_entry.data);
    }
  }

  // TODO(crbug.com/709036): Validate method data.
  data_util::ParseSupportedMethods(
      web_payment_request_.method_data, &supported_card_networks_,
      &basic_card_specified_networks_, &url_payment_method_identifiers_);

  data_util::ParseSupportedCardTypes(web_payment_request_.method_data,
                                     &supported_card_types_set_);

  const std::vector<autofill::CreditCard*>& credit_cards_to_suggest =
      personal_data_manager_->GetCreditCardsToSuggest();
  // Return early if the user has no stored credit cards.
  if (credit_cards_to_suggest.empty())
    return;

  // TODO(crbug.com/602666): Determine the number of possible payment methods so
  // that we can reserve enough space in the following vector.

  payment_method_cache_.clear();
  payment_method_cache_.reserve(credit_cards_to_suggest.size());

  for (const auto* credit_card : credit_cards_to_suggest)
    AddAutofillPaymentInstrument(*credit_card);
}

void PaymentRequest::PopulateAvailablePaymentMethods() {
  if (payment_method_cache_.empty())
    return;

  payment_methods_.clear();
  payment_methods_.reserve(payment_method_cache_.size());

  for (auto const& payment_method : payment_method_cache_)
    payment_methods_.push_back(payment_method.get());
}

void PaymentRequest::PopulateAvailableShippingOptions() {
  shipping_options_.clear();
  selected_shipping_option_ = nullptr;
  if (web_payment_request_.details.shipping_options.empty())
    return;

  shipping_options_.reserve(
      web_payment_request_.details.shipping_options.size());
  std::transform(std::begin(web_payment_request_.details.shipping_options),
                 std::end(web_payment_request_.details.shipping_options),
                 std::back_inserter(shipping_options_),
                 [](web::PaymentShippingOption& option) { return &option; });
}

void PaymentRequest::SetSelectedShippingOption() {
  // If more than one option has |selected| set, the last one in the sequence
  // should be treated as the selected item.
  for (auto* shipping_option : base::Reversed(shipping_options_)) {
    if (shipping_option->selected) {
      selected_shipping_option_ = shipping_option;
      break;
    }
  }
}

}  // namespace payments
