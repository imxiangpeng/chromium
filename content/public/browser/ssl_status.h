// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_SSL_STATUS_H_
#define CONTENT_PUBLIC_BROWSER_SSL_STATUS_H_

#include <stdint.h>

#include <vector>

#include "content/common/content_export.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/sct_status_flags.h"
#include "net/cert/x509_certificate.h"

namespace net {
class SSLInfo;
}

namespace content {

// Collects the SSL information for this NavigationEntry.
struct CONTENT_EXPORT SSLStatus {
  // SSLStatus consumers can attach instances of derived UserData classes to an
  // SSLStatus. This allows an embedder to attach data to the NavigationEntry
  // without SSLStatus having to know about it. Derived UserData classes have to
  // be cloneable since NavigationEntrys are cloned during navigations.
  class UserData {
   public:
    UserData() {}
    virtual ~UserData() = default;
    virtual std::unique_ptr<UserData> Clone() = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(UserData);
  };

  // Flags used for the page security content status.
  enum ContentStatusFlags {
    // HTTP page, or HTTPS page with no insecure content.
    NORMAL_CONTENT = 0,

    // HTTPS page containing "displayed" HTTP resources (e.g. images, CSS).
    DISPLAYED_INSECURE_CONTENT = 1 << 0,

    // HTTPS page containing "executed" HTTP resources (i.e. script).
    RAN_INSECURE_CONTENT = 1 << 1,

    // HTTPS page containing "displayed" HTTPS resources (e.g. images,
    // CSS) loaded with certificate errors.
    DISPLAYED_CONTENT_WITH_CERT_ERRORS = 1 << 2,

    // HTTPS page containing "executed" HTTPS resources (i.e. script)
    // loaded with certificate errors.
    RAN_CONTENT_WITH_CERT_ERRORS = 1 << 3,

    // HTTP page containing a password input. Embedders may use this to
    // adjust UI on nonsecure pages that collect sensitive data.
    DISPLAYED_PASSWORD_FIELD_ON_HTTP = 1 << 4,

    // HTTP page containing a credit card input. Embedders may use this to
    // adjust UI on nonsecure pages that collect sensitive data.
    DISPLAYED_CREDIT_CARD_FIELD_ON_HTTP = 1 << 5,

    // HTTPS page containing a form targeting an insecure action url.
    DISPLAYED_FORM_WITH_INSECURE_ACTION = 1 << 6,
  };

  SSLStatus();
  SSLStatus(const net::SSLInfo& ssl_info);
  SSLStatus(const SSLStatus& other);
  SSLStatus& operator=(SSLStatus other);
  ~SSLStatus();

  bool initialized;
  scoped_refptr<net::X509Certificate> certificate;
  net::CertStatus cert_status;
  int security_bits;
  uint16_t key_exchange_group;
  int connection_status;
  // A combination of the ContentStatusFlags above.
  int content_status;
  // True if PKP was bypassed due to a local trust anchor.
  bool pkp_bypassed;

  std::unique_ptr<UserData> user_data;

  // If you add new fields here, be sure to add them in the copy constructor and
  // copy assignment operator definitions in ssl_status.cc.
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_SSL_STATUS_H_
