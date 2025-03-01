// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/common/password_manager_features.h"

#include "build/build_config.h"

namespace password_manager {

namespace features {

// Enable affiliation based matching, so that credentials stored for an Android
// application will also be considered matches for, and be filled into
// corresponding Web applications.
const base::Feature kAffiliationBasedMatching = {
    "affiliation-based-matching", base::FEATURE_ENABLED_BY_DEFAULT};

// Drop the sync credential if captured for saving, do not offer it for saving.
const base::Feature kDropSyncCredential = {"drop-sync-credential",
                                           base::FEATURE_ENABLED_BY_DEFAULT};

// Enable additional elements in the form popup UI, which will allow the user to
// trigger generation or view all saved passwords.
const base::Feature kEnableManualFallbacksFilling = {
    "EnableManualFallbacksFilling", base::FEATURE_DISABLED_BY_DEFAULT};

// Enable a context menu item in the password field that allows the user
// to manually enforce saving of their password.
const base::Feature kEnablePasswordForceSaving = {
    "enable-password-force-saving", base::FEATURE_DISABLED_BY_DEFAULT};

// Enable the user to trigger password generation manually.
extern const base::Feature kEnableManualPasswordGeneration = {
    "enable-manual-password-generation", base::FEATURE_DISABLED_BY_DEFAULT};

// Enables username correction while saving username and password details.
extern const base::Feature kEnableUsernameCorrection{
    "EnableUsernameCorrection", base::FEATURE_DISABLED_BY_DEFAULT};

// Disallow autofilling of the sync credential.
const base::Feature kProtectSyncCredential = {
    "protect-sync-credential", base::FEATURE_DISABLED_BY_DEFAULT};

// Disallow autofilling of the sync credential only for transactional reauth
// pages.
const base::Feature kProtectSyncCredentialOnReauth = {
    "protect-sync-credential-on-reauth", base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kPasswordImportExport = {"password-import-export",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

// Control whether users can view and copy passwords. This is only used for
// mobile, the desktop version of Chrome always allows users to view
// passwords.
const base::Feature kViewPasswords = {"view-passwords",
// TODO(crbug.com/739404): Remove this ifdef once the feature spends a couple
// of releases in stable on iOS or once it also gets enabled on Android.
#if defined(OS_IOS)
                                      base::FEATURE_ENABLED_BY_DEFAULT};
#else
                                      base::FEATURE_DISABLED_BY_DEFAULT};
#endif

// Enables the experiment for the password manager to only fill on account
// selection, rather than autofilling on page load, with highlighting of fields.
const base::Feature kFillOnAccountSelect = {"fill-on-account-select",
                                            base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace features

}  // namespace password_manager
