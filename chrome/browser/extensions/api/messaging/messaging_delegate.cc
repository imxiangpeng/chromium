// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/messaging/messaging_delegate.h"

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/messaging/extension_message_port.h"
#include "chrome/browser/extensions/api/messaging/incognito_connectability.h"
#include "chrome/browser/extensions/api/messaging/native_message_port.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/api/messaging/native_message_host.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/api/messaging/port_id.h"
#include "extensions/common/extension.h"
#include "ui/gfx/native_widget_types.h"
#include "url/gurl.h"

namespace extensions {

// static
MessagingDelegate::PolicyPermission
MessagingDelegate::IsNativeMessagingHostAllowed(
    content::BrowserContext* browser_context,
    const std::string& native_host_name) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  PrefService* pref_service =
      Profile::FromBrowserContext(browser_context)->GetPrefs();

  PolicyPermission allow_result = PolicyPermission::ALLOW_ALL;
  if (pref_service->IsManagedPreference(
          pref_names::kNativeMessagingUserLevelHosts)) {
    if (!pref_service->GetBoolean(pref_names::kNativeMessagingUserLevelHosts))
      allow_result = PolicyPermission::ALLOW_SYSTEM_ONLY;
  }

  // All native messaging hosts are allowed if there is no blacklist.
  if (!pref_service->IsManagedPreference(pref_names::kNativeMessagingBlacklist))
    return allow_result;
  const base::ListValue* blacklist =
      pref_service->GetList(pref_names::kNativeMessagingBlacklist);
  if (!blacklist)
    return allow_result;

  // Check if the name or the wildcard is in the blacklist.
  base::Value name_value(native_host_name);
  base::Value wildcard_value("*");
  if (blacklist->Find(name_value) == blacklist->end() &&
      blacklist->Find(wildcard_value) == blacklist->end()) {
    return allow_result;
  }

  // The native messaging host is blacklisted. Check the whitelist.
  if (pref_service->IsManagedPreference(
          pref_names::kNativeMessagingWhitelist)) {
    const base::ListValue* whitelist =
        pref_service->GetList(pref_names::kNativeMessagingWhitelist);
    if (whitelist && whitelist->Find(name_value) != whitelist->end())
      return allow_result;
  }

  return PolicyPermission::DISALLOW;
}

// static
std::unique_ptr<base::DictionaryValue> MessagingDelegate::MaybeGetTabInfo(
    content::WebContents* web_contents) {
  // Add info about the opener's tab (if it was a tab).
  if (web_contents && ExtensionTabUtil::GetTabId(web_contents) >= 0) {
    // Only the tab id is useful to platform apps for internal use. The
    // unnecessary bits will be stripped out in
    // MessagingBindings::DispatchOnConnect().
    return ExtensionTabUtil::CreateTabObject(web_contents)->ToValue();
  }
  return nullptr;
}

// static
content::WebContents* MessagingDelegate::GetWebContentsByTabId(
    content::BrowserContext* browser_context,
    int tab_id) {
  content::WebContents* contents = nullptr;
  if (!ExtensionTabUtil::GetTabById(tab_id, browser_context,
                                    /*incognito_enabled=*/true, nullptr,
                                    nullptr, &contents, nullptr)) {
    return nullptr;
  }
  return contents;
}

// static
std::unique_ptr<MessagePort> MessagingDelegate::CreateReceiverForTab(
    base::WeakPtr<MessagePort::ChannelDelegate> channel_delegate,
    const std::string& extension_id,
    const PortId& receiver_port_id,
    content::WebContents* receiver_contents,
    int receiver_frame_id) {
  // Frame ID -1 is every frame in the tab.
  bool include_child_frames = receiver_frame_id == -1;
  content::RenderFrameHost* receiver_rfh =
      include_child_frames ? receiver_contents->GetMainFrame()
                           : ExtensionApiFrameIdMap::GetRenderFrameHostById(
                                 receiver_contents, receiver_frame_id);
  if (!receiver_rfh)
    return nullptr;

  return base::MakeUnique<ExtensionMessagePort>(
      channel_delegate, receiver_port_id, extension_id, receiver_rfh,
      include_child_frames);
}

// static
std::unique_ptr<MessagePort> MessagingDelegate::CreateReceiverForNativeApp(
    base::WeakPtr<MessagePort::ChannelDelegate> channel_delegate,
    content::RenderFrameHost* source,
    const std::string& extension_id,
    const PortId& receiver_port_id,
    const std::string& native_app_name,
    bool allow_user_level,
    std::string* error_out) {
  DCHECK(error_out);
  gfx::NativeView native_view = source ? source->GetNativeView() : nullptr;
  std::unique_ptr<NativeMessageHost> native_host = NativeMessageHost::Create(
      native_view, extension_id, native_app_name, allow_user_level, error_out);
  if (!native_host.get())
    return nullptr;
  return base::MakeUnique<NativeMessagePort>(channel_delegate, receiver_port_id,
                                             std::move(native_host));
}

// static
void MessagingDelegate::QueryIncognitoConnectability(
    content::BrowserContext* context,
    const Extension* target_extension,
    content::WebContents* source_contents,
    const GURL& source_url,
    const base::Callback<void(bool)>& callback) {
  DCHECK(context->IsOffTheRecord());
  IncognitoConnectability::Get(context)->Query(
      target_extension, source_contents, source_url, callback);
}

}  // namespace extensions
