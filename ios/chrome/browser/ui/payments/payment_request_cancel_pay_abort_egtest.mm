// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/strings/grit/components_strings.h"
#include "ios/chrome/browser/ui/autofill/card_unmask_prompt_view_bridge.h"
#import "ios/chrome/browser/ui/payments/payment_request_egtest_base.h"
#import "ios/chrome/browser/ui/payments/payment_request_error_view_controller.h"
#import "ios/chrome/browser/ui/payments/payment_request_view_controller.h"
#import "ios/chrome/test/app/chrome_test_util.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/web/public/test/http_server/http_server.h"
#import "ios/web/public/test/web_view_interaction_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
using chrome_test_util::ButtonWithAccessibilityLabel;
using chrome_test_util::ButtonWithAccessibilityLabelId;
using chrome_test_util::GetCurrentWebState;

// URLs of the test pages.
const char kAbortPage[] =
    "https://components/test/data/payments/payment_request_abort_test.html";
const char kNoShippingPage[] =
    "https://components/test/data/payments/"
    "payment_request_no_shipping_test.html";

}  // namepsace

// Tests for various scenarios in which Payment Request UI is displayed then
// closed (e.g., merchant cancellation, user cancellation, and completion).
@interface PaymentRequestOpenAndCloseEGTest : PaymentRequestEGTestBase

@end

@implementation PaymentRequestOpenAndCloseEGTest

#pragma mark - Tests

// Tests that navigating to a URL closes the Payment Request UI.
- (void)testOpenAndNavigateToURL {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kNoShippingPage)];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];
}

// Tests that reloading the page closes the Payment Request UI.
- (void)testOpenAndReload {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  [ChromeEarlGrey reload];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];
}

// Tests that navigating to the previous page closes the Payment Request UI.
- (void)testOpenAndNavigateBack {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  [ChromeEarlGrey goBack];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];
}

// Tests that tapping the cancel button closes the Payment Request UI.
- (void)testOpenAndCancel {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  // Tap the cancel button.
  [[EarlGrey selectElementWithMatcher:ButtonWithAccessibilityLabelId(
                                          IDS_ACCNAME_CANCEL)]
      performAction:grey_tap()];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];

  // TODO(crbug.com/602666): Check whether the Promise returned by
  // request.show() gets rejected with the appropriate error message.
}

// Tests that tapping the link to Chrome Settings closes the Payment Request UI
// and displays the Autofill Settings UI.
- (void)testOpenAndNavigateToSettings {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  // Tap the settings link.
  [[EarlGrey selectElementWithMatcher:ButtonWithAccessibilityLabel(@"Settings")]
      performAction:grey_tap()];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];

  // Confirm that the Autofill Settings UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          @"kAutofillCollectionViewId")]
      assertWithMatcher:grey_notNil()];

  // TODO(crbug.com/602666): Check whether the Promise returned by
  // request.show() gets rejected with the appropriate error message.
}

// Tests that tapping the pay button closes the Payment Request UI, accepts the
// Promise returned by request.show() with the response object, and accepts the
// Promise returned by response.complete() with an appropriate response message.
- (void)testOpenAndPay {
  autofill::AutofillProfile profile = autofill::test::GetFullProfile();
  [self addAutofillProfile:profile];
  autofill::CreditCard card = autofill::test::GetCreditCard();
  card.set_billing_address_id(profile.guid());
  [self addCreditCard:card];

  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kNoShippingPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  // Tap the Buy button.
  [[EarlGrey selectElementWithMatcher:ButtonWithAccessibilityLabelId(
                                          IDS_PAYMENTS_PAY_BUTTON)]
      performAction:grey_tap()];

  // Confirm that the Card Unmask Prompt is showing.
  [[EarlGrey
      selectElementWithMatcher:
          grey_accessibilityID(kCardUnmaskPromptCollectionViewAccessibilityID)]
      assertWithMatcher:grey_notNil()];

  // Type in the CVC.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(@"CVC_textField")]
      performAction:grey_typeText(@"111")];

  // Tap the Confirm button.
  [[EarlGrey
      selectElementWithMatcher:ButtonWithAccessibilityLabelId(IDS_ACCNAME_OK)]
      performAction:grey_tap()];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];

  // Confirm that the appropriate response message was sent. Note that this does
  // not test the structure of the response.
  [self
      waitForWebViewContainingTexts:
          {base::UTF16ToUTF8(card.GetRawInfo(autofill::CREDIT_CARD_NUMBER)),
           base::UTF16ToUTF8(card.GetRawInfo(autofill::CREDIT_CARD_NAME_FULL)),
           base::UTF16ToUTF8(card.GetRawInfo(autofill::CREDIT_CARD_EXP_MONTH)),
           base::UTF16ToUTF8(
               card.GetRawInfo(autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR)),
           "111"}];
  [self
      waitForWebViewContainingTexts:
          {base::UTF16ToUTF8(profile.GetRawInfo(autofill::NAME_FIRST)),
           base::UTF16ToUTF8(profile.GetRawInfo(autofill::NAME_LAST)),
           base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_LINE1)),
           base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_LINE2)),
           base::UTF16ToUTF8(
               profile.GetRawInfo(autofill::ADDRESS_HOME_COUNTRY)),
           base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_ZIP)),
           base::UTF16ToUTF8(profile.GetRawInfo(autofill::ADDRESS_HOME_CITY)),
           base::UTF16ToUTF8(
               profile.GetRawInfo(autofill::ADDRESS_HOME_STATE))}];
}

// Tests that calling request.abort() successfully aborts the Payment Request.
- (void)testAbort {
  [ChromeEarlGrey loadURL:web::test::HttpServer::MakeUrl(kAbortPage)];

  [ChromeEarlGrey tapWebViewElementWithID:@"buy"];

  // Confirm that the Payment Request UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  [ChromeEarlGrey tapWebViewElementWithID:@"abort"];

  // Confirm that the error confirmation UI is showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestErrorCollectionViewID)]
      assertWithMatcher:grey_notNil()];

  // Confirm the error.
  [[EarlGrey
      selectElementWithMatcher:ButtonWithAccessibilityLabelId(IDS_ACCNAME_OK)]
      performAction:grey_tap()];

  // Confirm that the Payment Request UI is not showing.
  [[EarlGrey selectElementWithMatcher:grey_accessibilityID(
                                          kPaymentRequestCollectionViewID)]
      assertWithMatcher:grey_nil()];

  [self waitForWebViewContainingTexts:{"Aborted"}];
}

@end
