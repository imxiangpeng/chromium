// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_action_test_util.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model_factory.h"
#include "components/crx_file/id_util.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/value_builder.h"

namespace extensions {
namespace extension_action_test_util {

namespace {

size_t GetPageActionCount(content::WebContents* web_contents,
                          bool only_count_visible) {
  DCHECK(web_contents);
  size_t count = 0u;
  int tab_id = SessionTabHelper::IdForTab(web_contents);
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  ToolbarActionsModel* toolbar_model = ToolbarActionsModel::Get(profile);
  const std::vector<ToolbarActionsModel::ToolbarItem>& toolbar_items =
      toolbar_model->toolbar_items();
  ExtensionActionManager* action_manager =
      ExtensionActionManager::Get(web_contents->GetBrowserContext());
  const ExtensionSet& enabled_extensions =
      ExtensionRegistry::Get(profile)->enabled_extensions();
  for (const ToolbarActionsModel::ToolbarItem& item : toolbar_items) {
    if (item.type == ToolbarActionsModel::EXTENSION_ACTION) {
      const Extension* extension = enabled_extensions.GetByID(item.id);
      ExtensionAction* extension_action =
          action_manager->GetPageAction(*extension);
      if (extension_action &&
          (!only_count_visible || extension_action->GetIsVisible(tab_id)))
        ++count;
    }
  }

  return count;
}

// Creates a new ToolbarActionsModel for the given |context|.
std::unique_ptr<KeyedService> BuildToolbarModel(
    content::BrowserContext* context) {
  return base::MakeUnique<ToolbarActionsModel>(
      Profile::FromBrowserContext(context),
      extensions::ExtensionPrefs::Get(context));
}

// Creates a new ToolbarActionsModel for the given profile, optionally
// triggering the extension system's ready signal.
ToolbarActionsModel* CreateToolbarModelImpl(Profile* profile,
                                            bool wait_for_ready) {
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
  if (model)
    return model;

  // No existing model means it's a new profile (since we, by default, don't
  // create the ToolbarModel in testing).
  ToolbarActionsModelFactory::GetInstance()->SetTestingFactory(
      profile, &BuildToolbarModel);
  model = ToolbarActionsModel::Get(profile);
  if (wait_for_ready) {
    // Fake the extension system ready signal.
    // HACK ALERT! In production, the ready task on ExtensionSystem (and most
    // everything else on it, too) is shared between incognito and normal
    // profiles, but a TestExtensionSystem doesn't have the concept of "shared".
    // Because of this, we have to set any new profile's TestExtensionSystem's
    // ready task, too.
    static_cast<TestExtensionSystem*>(ExtensionSystem::Get(profile))->
        SetReady();
    // Run tasks posted to TestExtensionSystem.
    base::RunLoop().RunUntilIdle();
  }

  return model;
}

}  // namespace

size_t GetVisiblePageActionCount(content::WebContents* web_contents) {
  return GetPageActionCount(web_contents, true);
}

size_t GetTotalPageActionCount(content::WebContents* web_contents) {
  return GetPageActionCount(web_contents, false);
}

scoped_refptr<const Extension> CreateActionExtension(
    const std::string& name,
    ActionType action_type,
    Manifest::Location location,
    std::unique_ptr<base::DictionaryValue> extra_keys) {
  DictionaryBuilder manifest;
  manifest.Set("name", name)
          .Set("description", "An extension")
          .Set("manifest_version", 2)
          .Set("version", "1.0.0");

  const char* action_key = nullptr;
  switch (action_type) {
    case NO_ACTION:
      break;
    case PAGE_ACTION:
      action_key = manifest_keys::kPageAction;
      break;
    case BROWSER_ACTION:
      action_key = manifest_keys::kBrowserAction;
      break;
  }

  if (action_key)
    manifest.Set(action_key, DictionaryBuilder().Build());

  ExtensionBuilder builder;
  builder.SetManifest(manifest.Build())
      .SetID(crx_file::id_util::GenerateId(name))
      .SetLocation(location);
  if (extra_keys)
    builder.MergeManifest(std::move(extra_keys));
  return builder.Build();
}

ToolbarActionsModel* CreateToolbarModelForProfile(Profile* profile) {
  return CreateToolbarModelImpl(profile, true);
}

ToolbarActionsModel* CreateToolbarModelForProfileWithoutWaitingForReady(
    Profile* profile) {
  return CreateToolbarModelImpl(profile, false);
}

}  // namespace extension_action_test_util
}  // namespace extensions
