// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SSL_SSL_BLOCKING_PAGE_H_
#define CHROME_BROWSER_SSL_SSL_BLOCKING_PAGE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/ssl_cert_reporter.h"
#include "components/certificate_reporting/error_report.h"
#include "components/security_interstitials/content/security_interstitial_page.h"
#include "content/public/browser/certificate_request_result_type.h"
#include "extensions/features/features.h"
#include "net/ssl/ssl_info.h"
#include "url/gurl.h"

namespace policy {
class PolicyTest_SSLErrorOverridingDisallowed_Test;
}

namespace security_interstitials {
class SSLErrorUI;
}

class CertReportHelper;
class SSLUITest;
class ChromeMetricsHelper;

// This class is responsible for showing/hiding the interstitial page that is
// shown when a certificate error happens.
// It deletes itself when the interstitial page is closed.
class SSLBlockingPage
    : public security_interstitials::SecurityInterstitialPage {
 public:
  // Interstitial type, used in tests.
  static const InterstitialPageDelegate::TypeID kTypeForTesting;

  ~SSLBlockingPage() override;

  // Creates an SSL blocking page. If the blocking page isn't shown, the caller
  // is responsible for cleaning up the blocking page, otherwise the
  // interstitial takes ownership when shown. |options_mask| must be a bitwise
  // mask of SSLErrorUI::SSLErrorOptionsMask values.
  // This is static because the constructor uses expensive to compute parameters
  // more than once (e.g. overrideable).
  static SSLBlockingPage* Create(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      int options_mask,
      const base::Time& time_triggered,
      std::unique_ptr<SSLCertReporter> ssl_cert_reporter,
      bool is_superfish,
      const base::Callback<void(content::CertificateRequestResultType)>&
          callback);

  // InterstitialPageDelegate method:
  InterstitialPageDelegate::TypeID GetTypeForTesting() const override;

  // Returns true if |options_mask| refers to a soft-overridable SSL error and
  // if SSL error overriding is allowed by policy.
  static bool IsOverridable(int options_mask, const Profile* const profile);

  void SetSSLCertReporterForTesting(
      std::unique_ptr<SSLCertReporter> ssl_cert_reporter);

 protected:
  friend class policy::PolicyTest_SSLErrorOverridingDisallowed_Test;
  friend class SSLUITest;

  // InterstitialPageDelegate implementation.
  void CommandReceived(const std::string& command) override;
  void OverrideEntry(content::NavigationEntry* entry) override;
  void OverrideRendererPrefs(content::RendererPreferences* prefs) override;
  void OnProceed() override;
  void OnDontProceed() override;

  // SecurityInterstitialPage implementation:
  bool ShouldCreateNewNavigation() const override;
  void PopulateInterstitialStrings(
      base::DictionaryValue* load_time_data) override;

 private:
  SSLBlockingPage(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      int options_mask,
      const base::Time& time_triggered,
      std::unique_ptr<SSLCertReporter> ssl_cert_reporter,
      bool overrideable,
      std::unique_ptr<ChromeMetricsHelper> metrics_helper,
      bool is_superfish,
      const base::Callback<void(content::CertificateRequestResultType)>&
          callback);

  void NotifyDenyCertificate();

  base::Callback<void(content::CertificateRequestResultType)> callback_;
  const net::SSLInfo ssl_info_;
  const bool overridable_;  // The UI allows the user to override the error.

  // The user previously allowed a bad certificate, but the decision has now
  // expired.
  const bool expired_but_previously_allowed_;

  const std::unique_ptr<CertReportHelper> cert_report_helper_;
  const std::unique_ptr<security_interstitials::SSLErrorUI> ssl_error_ui_;

  DISALLOW_COPY_AND_ASSIGN(SSLBlockingPage);
};

#endif  // CHROME_BROWSER_SSL_SSL_BLOCKING_PAGE_H_
