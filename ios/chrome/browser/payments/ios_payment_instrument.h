// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_PAYMENTS_IOS_PAYMENT_INSTRUMENT_H_
#define IOS_CHROME_BROWSER_PAYMENTS_IOS_PAYMENT_INSTRUMENT_H_

#import <UIKit/UIKit.h>

#include <map>
#include <string>

#import "base/mac/scoped_nsobject.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "components/payments/core/payment_instrument.h"
#include "ios/chrome/browser/payments/payment_request.h"

@class PaymentRequestUIDelegate;

namespace payments {

// A map is maintained to enumerate scheme names corresponding with iOS
// payment apps. These scheme names are needed as a form of installation
// check. If canOpenURL of UIApplication succeeds on the scheme name then
// that's a guarantee that the app is installed on the user's device.
// These scheme names MUST be enumerated in LSApplicationQueriesSchemes
// in the plist file.
const std::map<std::string, std::string>& GetMethodNameToSchemeName();

// Represents an iOS Native App as a form of payment in Payment Request.
class IOSPaymentInstrument : public PaymentInstrument {
 public:
  // Initializes an IOSPaymentInstrument. |method_name| is the url payment
  // method identifier for this instrument. |universal_link| is the unique
  // link that is used to open the app from Chrome. |app_name| is the name
  // the iOS native payment app. The IOSPaymentInstrument takes ownership
  // of |icon_image| which is an icon that represents the app.
  // |payment_request_ui_delegate| is the UI class that manages opening
  // the native payment app from Chrome.
  IOSPaymentInstrument(
      const std::string& method_name,
      const std::string& universal_link,
      const std::string& app_name,
      UIImage* icon_image,
      id<PaymentRequestUIDelegate> payment_request_ui_delegate);

  ~IOSPaymentInstrument() override;

  // PaymentInstrument:
  void InvokePaymentApp(PaymentInstrument::Delegate* delegate) override;
  bool IsCompleteForPayment() const override;
  bool IsExactlyMatchingMerchantRequest() const override;
  base::string16 GetMissingInfoLabel() const override;
  bool IsValidForCanMakePayment() const override;
  void RecordUse() override;
  base::string16 GetLabel() const override;
  base::string16 GetSublabel() const override;
  bool IsValidForModifier(
      const std::vector<std::string>& supported_methods,
      const std::set<autofill::CreditCard::CardType>& supported_types,
      const std::vector<std::string>& supported_networks) const override;

  // Given that the icon for the iOS payment instrument can only be determined
  // at run-time, the icon is obtained using this UIImage object rather than
  // using a resource ID and Chrome's resource bundle.
  UIImage* icon_image() const { return icon_image_; }

 private:
  std::string method_name_;
  std::string universal_link_;
  std::string app_name_;
  base::scoped_nsobject<UIImage> icon_image_;

  id<PaymentRequestUIDelegate> payment_request_ui_delegate_;

  DISALLOW_COPY_AND_ASSIGN(IOSPaymentInstrument);
};

}  // namespace payments

#endif  // IOS_CHROME_BROWSER_PAYMENTS_IOS_PAYMENT_INSTRUMENT_H_
