// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/proxy_overridden_bubble_delegate.h"

#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/settings_api_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "ui/base/l10n/l10n_util.h"

namespace extensions {

namespace {

// The minimum time to wait (since the extension was installed) before notifying
// the user about it.
const int kDaysSinceInstallMin = 7;

// Whether the user has been notified about extension overriding the proxy.
const char kProxyBubbleAcknowledged[] = "ack_proxy_bubble";

}  // namespace

ProxyOverriddenBubbleDelegate::ProxyOverriddenBubbleDelegate(
    Profile* profile)
    : ExtensionMessageBubbleController::Delegate(profile) {
  set_acknowledged_flag_pref_name(kProxyBubbleAcknowledged);
}

ProxyOverriddenBubbleDelegate::~ProxyOverriddenBubbleDelegate() {}

bool ProxyOverriddenBubbleDelegate::ShouldIncludeExtension(
    const Extension* extension) {
  if (!extension_id_.empty() && extension_id_ != extension->id())
    return false;  // Only one extension can be controlling the proxy at a time.

  const Extension* overriding = GetExtensionOverridingProxy(profile());
  if (!overriding || overriding != extension)
    return false;

  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  base::TimeDelta since_install =
      base::Time::Now() - prefs->GetInstallTime(extension->id());
  if (since_install.InDays() < kDaysSinceInstallMin)
    return false;

  if (HasBubbleInfoBeenAcknowledged(extension->id()))
    return false;

  // Found the only extension; restrict to this one.
  extension_id_ = extension->id();

  return true;
}

void ProxyOverriddenBubbleDelegate::AcknowledgeExtension(
    const std::string& extension_id,
    ExtensionMessageBubbleController::BubbleAction user_action) {
  if (user_action != ExtensionMessageBubbleController::ACTION_EXECUTE)
    SetBubbleInfoBeenAcknowledged(extension_id, true);
}

void ProxyOverriddenBubbleDelegate::PerformAction(const ExtensionIdList& list) {
  for (size_t i = 0; i < list.size(); ++i)
    service()->DisableExtension(list[i], Extension::DISABLE_USER_ACTION);
}

base::string16 ProxyOverriddenBubbleDelegate::GetTitle() const {
  return l10n_util::GetStringUTF16(
      IDS_EXTENSIONS_PROXY_CONTROLLED_TITLE_HOME_PAGE_BUBBLE);
}

base::string16 ProxyOverriddenBubbleDelegate::GetMessageBody(
    bool anchored_to_browser_action,
    int extension_count) const {
  if (anchored_to_browser_action) {
    return l10n_util::GetStringUTF16(
        IDS_EXTENSIONS_PROXY_CONTROLLED_FIRST_LINE_EXTENSION_SPECIFIC);
  } else {
    const Extension* extension = registry()->GetExtensionById(
            extension_id_, ExtensionRegistry::EVERYTHING);
    // If the bubble is about to show, the extension should certainly exist.
    CHECK(extension);
    return l10n_util::GetStringFUTF16(
        IDS_EXTENSIONS_PROXY_CONTROLLED_FIRST_LINE,
        base::UTF8ToUTF16(extension->name()));
  }
}

base::string16 ProxyOverriddenBubbleDelegate::GetOverflowText(
    const base::string16& overflow_count) const {
  // Does not have more than one extension in the list at a time.
  NOTREACHED();
  return base::string16();
}

GURL ProxyOverriddenBubbleDelegate::GetLearnMoreUrl() const {
  return GURL(chrome::kExtensionControlledSettingLearnMoreURL);
}

base::string16 ProxyOverriddenBubbleDelegate::GetActionButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_EXTENSION_CONTROLLED_RESTORE_SETTINGS);
}

base::string16 ProxyOverriddenBubbleDelegate::GetDismissButtonLabel() const {
  return l10n_util::GetStringUTF16(IDS_EXTENSION_CONTROLLED_KEEP_CHANGES);
}

bool ProxyOverriddenBubbleDelegate::ShouldCloseOnDeactivate() const {
  return false;
}

bool ProxyOverriddenBubbleDelegate::ShouldAcknowledgeOnDeactivate() const {
  return false;
}

bool ProxyOverriddenBubbleDelegate::ShouldShowExtensionList() const {
  return false;
}

bool ProxyOverriddenBubbleDelegate::ShouldHighlightExtensions() const {
  return true;
}

bool ProxyOverriddenBubbleDelegate::ShouldLimitToEnabledExtensions() const {
  return true;
}

void ProxyOverriddenBubbleDelegate::LogExtensionCount(size_t count) {
}

void ProxyOverriddenBubbleDelegate::LogAction(
    ExtensionMessageBubbleController::BubbleAction action) {
  UMA_HISTOGRAM_ENUMERATION("ProxyOverriddenBubble.UserSelection",
                            action,
                            ExtensionMessageBubbleController::ACTION_BOUNDARY);
}

const char* ProxyOverriddenBubbleDelegate::GetKey() {
  return "ProxyOverriddenBubbleDelegate";
}

bool ProxyOverriddenBubbleDelegate::SupportsPolicyIndicator() {
  return true;
}

}  // namespace extensions
