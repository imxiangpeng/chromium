// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_
#define COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "url/gurl.h"

class PrefService;

namespace security_interstitials {

class MetricsHelper;

// Constants used to communicate with the JavaScript.
extern const char kBoxChecked[];
extern const char kDisplayCheckBox[];
extern const char kOptInLink[];
extern const char kPrivacyLinkHtml[];

// These represent the commands sent from the interstitial JavaScript.
// DO NOT reorder or change these without also changing the JavaScript!
// See components/security_interstitials/core/browser/resources/
enum SecurityInterstitialCommands {
  // Used by tests
  CMD_ERROR = -3,
  CMD_TEXT_FOUND = -2,
  CMD_TEXT_NOT_FOUND = -1,
  // Decisions
  CMD_DONT_PROCEED = 0,
  CMD_PROCEED = 1,
  // Ways for user to get more information
  CMD_SHOW_MORE_SECTION = 2,
  CMD_OPEN_HELP_CENTER = 3,
  CMD_OPEN_DIAGNOSTIC = 4,
  // Primary button actions
  CMD_RELOAD = 5,
  CMD_OPEN_DATE_SETTINGS = 6,
  CMD_OPEN_LOGIN = 7,
  // Safe Browsing Extended Reporting
  CMD_DO_REPORT = 8,
  CMD_DONT_REPORT = 9,
  CMD_OPEN_REPORTING_PRIVACY = 10,
  CMD_OPEN_WHITEPAPER = 11,
  // Report a phishing error
  CMD_REPORT_PHISHING_ERROR = 12,
};

// Provides methods for handling commands from the user, which requires some
// embedder-specific abstraction. This class should handle all commands sent
// by the JavaScript error page.
class ControllerClient {
 public:
  explicit ControllerClient(std::unique_ptr<MetricsHelper> metrics_helper);
  virtual ~ControllerClient();

  // Handle the user's reporting preferences.
  void SetReportingPreference(bool report);
  void OpenExtendedReportingPrivacyPolicy();
  void OpenExtendedReportingWhitepaper();

  // If available, open the operating system's date/time settings.
  virtual bool CanLaunchDateAndTimeSettings() = 0;
  virtual void LaunchDateAndTimeSettings() = 0;

  // Close the error and go back to the previous page. This applies to
  // situations where navigation is blocked before committing.
  virtual void GoBack() = 0;
  // Whether it is possible to go 'Back to safety'.
  virtual bool CanGoBack() = 0;

  // If the offending entry has committed, go back or to a safe page without
  // closing the error page. This error page will be closed when the new page
  // commits.
  virtual void GoBackAfterNavigationCommitted() = 0;

  // Close the error and proceed to the blocked page.
  virtual void Proceed() = 0;

  // Reload the blocked page to see if it succeeds now.
  virtual void Reload() = 0;

  MetricsHelper* metrics_helper() const;

  virtual void OpenUrlInCurrentTab(const GURL& url) = 0;

  virtual void OpenUrlInNewForegroundTab(const GURL& url) = 0;

  virtual PrefService* GetPrefService() = 0;

  virtual const std::string& GetApplicationLocale() const = 0;

  GURL GetBaseHelpCenterUrl() const;

  void SetBaseHelpCenterUrlForTesting(const GURL& test_url);

 protected:
  virtual const std::string GetExtendedReportingPrefName() const = 0;

 private:
  std::unique_ptr<MetricsHelper> metrics_helper_;
  // Link to the help center.
  GURL help_center_url_;

  DISALLOW_COPY_AND_ASSIGN(ControllerClient);
};

}  // namespace security_interstitials

#endif  // COMPONENTS_SECURITY_INTERSTITIALS_CORE_CONTROLLER_CLIENT_H_
