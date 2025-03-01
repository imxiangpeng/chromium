// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_DICE_HEADER_HELPER_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_DICE_HEADER_HELPER_H_

#include <string>

#include "base/macros.h"
#include "components/signin/core/browser/signin_header_helper.h"

class GURL;

namespace signin {

// SigninHeaderHelper implementation managing the Dice header.
class DiceHeaderHelper : public SigninHeaderHelper {
 public:
  explicit DiceHeaderHelper(bool signed_in_with_auth_error);
  ~DiceHeaderHelper() override {}

  // Returns the parameters contained in the X-Chrome-ID-Consistency-Response
  // response header.
  static DiceResponseParams BuildDiceSigninResponseParams(
      const std::string& header_value);

  // Returns the parameters contained in the Google-Accounts-SignOut response
  // header.
  static DiceResponseParams BuildDiceSignoutResponseParams(
      const std::string& header_value);

  // Returns the header value for Dice requests. Returns the empty string when
  // the header must not be added.
  std::string BuildRequestHeader(const std::string& account_id,
                                 bool sync_enabled);

 private:
  // SigninHeaderHelper implementation:
  bool IsUrlEligibleForRequestHeader(const GURL& url) override;

  bool signed_in_with_auth_error_;

  DISALLOW_COPY_AND_ASSIGN(DiceHeaderHelper);
};

}  // namespace signin

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_DICE_HEADER_HELPER_H_
