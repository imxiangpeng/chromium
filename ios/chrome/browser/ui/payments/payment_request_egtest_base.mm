// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/payments/payment_request_egtest_base.h"

#include <algorithm>

#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "ios/chrome/browser/autofill/personal_data_manager_factory.h"
#include "ios/chrome/browser/payments/ios_payment_request_cache_factory.h"
#import "ios/chrome/test/app/chrome_test_util.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/testing/wait_util.h"
#import "ios/web/public/test/web_view_interaction_test_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Timeout in seconds while waiting for a profile or a credit to be added to the
// PersonalDataManager.
const NSTimeInterval kPDMMaxDelaySeconds = 10.0;
}

@interface PaymentRequestEGTestBase ()

// Returns the PersonalDataManager instance for the current browser state.
- (autofill::PersonalDataManager*)personalDataManager;

@end

@implementation PaymentRequestEGTestBase

// Profiles added to the PersonalDataManager.
std::vector<autofill::AutofillProfile> _profiles;

// Credit Cards added to the PersonalDataManager.
std::vector<autofill::CreditCard> _cards;

#pragma mark - XCTestCase

- (void)tearDown {
  for (const auto& profile : _profiles) {
    [self personalDataManager]->RemoveByGUID(profile.guid());
  }
  _profiles.clear();

  for (const auto& card : _cards) {
    [self personalDataManager]->RemoveByGUID(card.guid());
  }
  _cards.clear();

  [super tearDown];
}

#pragma mark - Public methods

- (void)addAutofillProfile:(const autofill::AutofillProfile&)profile {
  _profiles.push_back(profile);
  size_t profile_count = [self personalDataManager]->GetProfiles().size();
  [self personalDataManager]->AddProfile(profile);
  GREYAssert(testing::WaitUntilConditionOrTimeout(
                 kPDMMaxDelaySeconds,
                 ^bool() {
                   return profile_count <
                          [self personalDataManager]->GetProfiles().size();
                 }),
             @"Failed to add profile.");
}

- (void)addCreditCard:(const autofill::CreditCard&)card {
  _cards.push_back(card);
  size_t card_count = [self personalDataManager]->GetCreditCards().size();
  [self personalDataManager]->AddCreditCard(card);
  GREYAssert(testing::WaitUntilConditionOrTimeout(
                 kPDMMaxDelaySeconds,
                 ^bool() {
                   return card_count <
                          [self personalDataManager]->GetCreditCards().size();
                 }),
             @"Failed to add credit card.");
}

- (payments::PaymentRequestCache::PaymentRequestSet&)paymentRequestsForWebState:
    (web::WebState*)webState {
  return payments::IOSPaymentRequestCacheFactory::GetForBrowserState(
             chrome_test_util::GetOriginalBrowserState())
      ->GetPaymentRequests(webState);
}

- (void)waitForWebViewContainingTexts:(const std::vector<std::string>&)texts {
  for (const std::string& text : texts)
    [ChromeEarlGrey waitForWebViewContainingText:text];
}

#pragma mark - Helper methods

- (autofill::PersonalDataManager*)personalDataManager {
  return autofill::PersonalDataManagerFactory::GetForBrowserState(
      chrome_test_util::GetOriginalBrowserState());
}

@end
