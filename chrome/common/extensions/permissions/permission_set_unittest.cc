// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/json/json_file_value_serializer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_test_util.h"
#include "chrome/common/extensions/permissions/chrome_permission_message_provider.h"
#include "chrome/grit/generated_resources.h"
#include "components/version_info/version_info.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/features/feature_channel.h"
#include "extensions/common/permissions/permission_message_provider.h"
#include "extensions/common/permissions/permission_message_test_util.h"
#include "extensions/common/permissions/permission_message_util.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/permissions/permissions_info.h"
#include "extensions/common/permissions/socket_permission.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

using extension_test_util::LoadManifest;

namespace extensions {

namespace {

static void AddPattern(URLPatternSet* extent, const std::string& pattern) {
  int schemes = URLPattern::SCHEME_ALL;
  extent->AddPattern(URLPattern(schemes, pattern));
}

size_t IndexOf(const PermissionMessages& warnings, const std::string& warning) {
  size_t i = 0;
  for (const PermissionMessage& msg : warnings) {
    if (msg.message() == base::ASCIIToUTF16(warning))
      return i;
    ++i;
  }

  return warnings.size();
}

PermissionIDSet MakePermissionIDSet(APIPermission::ID id) {
  PermissionIDSet set;
  set.insert(id);
  return set;
}

PermissionIDSet MakePermissionIDSet(APIPermission::ID id1,
                                    APIPermission::ID id2) {
  PermissionIDSet set;
  set.insert(id1);
  set.insert(id2);
  return set;
}

PermissionIDSet MakePermissionIDSet(const APIPermissionSet& permissions) {
  PermissionIDSet set;
  for (const APIPermission* permission : permissions)
    set.insert(permission->id());
  return set;
}

std::string PermissionIDsToString(const PermissionIDSet& ids) {
  std::vector<std::string> strs;
  for (const PermissionID& id : ids)
    strs.push_back(base::IntToString(id.id()));
  return base::StringPrintf("[ %s ]", base::JoinString(strs, ", ").c_str());
}

std::string CoalescedPermissionIDsToString(const PermissionMessages& msgs) {
  std::vector<std::string> strs;
  for (const PermissionMessage& msg : msgs)
    strs.push_back(PermissionIDsToString(msg.permissions()));
  return base::JoinString(strs, " ");
}

// Check that the given |permissions| produce a single warning message,
// identified by the set of |expected_ids|.
testing::AssertionResult PermissionSetProducesMessage(
    const PermissionSet& permissions,
    Manifest::Type extension_type,
    const PermissionIDSet& expected_ids) {
  const PermissionMessageProvider* provider = PermissionMessageProvider::Get();

  PermissionMessages msgs = provider->GetPermissionMessages(
      provider->GetAllPermissionIDs(permissions, extension_type));
  if (msgs.size() != 1) {
    return testing::AssertionFailure()
           << "Expected single permission message with IDs "
           << PermissionIDsToString(expected_ids) << " but got " << msgs.size()
           << " messages: " << CoalescedPermissionIDsToString(msgs);
  }
  if (!msgs.front().permissions().Equals(expected_ids)) {
    return testing::AssertionFailure()
           << "Expected permission IDs " << PermissionIDsToString(expected_ids)
           << " but got " << PermissionIDsToString(msgs.front().permissions());
  }

  return testing::AssertionSuccess();
}

}  // namespace

// Tests GetByID.
TEST(PermissionsTest, GetByID) {
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  APIPermissionSet apis = info->GetAll();
  for (APIPermissionSet::const_iterator i = apis.begin();
       i != apis.end(); ++i) {
    EXPECT_EQ(i->id(), i->info()->id());
  }
}

// Tests that GetByName works with normal permission names and aliases.
TEST(PermissionsTest, GetByName) {
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  EXPECT_EQ(APIPermission::kTab, info->GetByName("tabs")->id());
  EXPECT_EQ(APIPermission::kManagement,
            info->GetByName("management")->id());
  EXPECT_FALSE(info->GetByName("alsdkfjasldkfj"));
}

TEST(PermissionsTest, GetAll) {
  size_t count = 0;
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  APIPermissionSet apis = info->GetAll();
  for (APIPermissionSet::const_iterator api = apis.begin();
       api != apis.end(); ++api) {
    // Make sure only the valid permission IDs get returned.
    EXPECT_NE(APIPermission::kInvalid, api->id());
    EXPECT_NE(APIPermission::kUnknown, api->id());
    count++;
  }
  EXPECT_EQ(count, info->get_permission_count());
}

TEST(PermissionsTest, GetAllByName) {
  std::set<std::string> names;
  names.insert("background");
  names.insert("management");

  // This is an alias of kTab
  names.insert("windows");

  // This unknown name should get dropped.
  names.insert("sdlkfjasdlkfj");

  APIPermissionSet expected;
  expected.insert(APIPermission::kBackground);
  expected.insert(APIPermission::kManagement);
  expected.insert(APIPermission::kTab);

  EXPECT_EQ(expected,
            PermissionsInfo::GetInstance()->GetAllByName(names));
}

// Tests that the aliases are properly mapped.
TEST(PermissionsTest, Aliases) {
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  // tabs: tabs, windows
  std::string tabs_name = "tabs";
  EXPECT_EQ(tabs_name, info->GetByID(APIPermission::kTab)->name());
  EXPECT_EQ(APIPermission::kTab, info->GetByName("tabs")->id());
  EXPECT_EQ(APIPermission::kTab, info->GetByName("windows")->id());

  // unlimitedStorage: unlimitedStorage, unlimited_storage
  std::string storage_name = "unlimitedStorage";
  EXPECT_EQ(storage_name, info->GetByID(
      APIPermission::kUnlimitedStorage)->name());
  EXPECT_EQ(APIPermission::kUnlimitedStorage,
            info->GetByName("unlimitedStorage")->id());
  EXPECT_EQ(APIPermission::kUnlimitedStorage,
            info->GetByName("unlimited_storage")->id());
}

TEST(PermissionsTest, EffectiveHostPermissions) {
  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "empty.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_EQ(0u, extension->permissions_data()
                      ->GetEffectiveHostPermissions()
                      .patterns()
                      .size());
    EXPECT_FALSE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "one_host.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_FALSE(
        permissions.HasEffectiveAccessToURL(GURL("https://www.google.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "one_host_wildcard.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("http://google.com")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://foo.google.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "two_hosts.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.reddit.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "https_not_considered.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("http://google.com")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("https://google.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "two_content_scripts.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("http://google.com")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.reddit.com")));
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(
        GURL("http://news.ycombinator.com")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "all_hosts.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("http://test/")));
    EXPECT_FALSE(permissions.HasEffectiveAccessToURL(GURL("https://test/")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_TRUE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "all_hosts2.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("http://test/")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_TRUE(permissions.HasEffectiveAccessToAllHosts());
  }

  {
    scoped_refptr<const Extension> extension =
        LoadManifest("effective_host_permissions", "all_hosts3.json");
    const PermissionSet& permissions =
        extension->permissions_data()->active_permissions();
    EXPECT_FALSE(permissions.HasEffectiveAccessToURL(GURL("http://test/")));
    EXPECT_TRUE(permissions.HasEffectiveAccessToURL(GURL("https://test/")));
    EXPECT_TRUE(
        permissions.HasEffectiveAccessToURL(GURL("http://www.google.com")));
    EXPECT_TRUE(permissions.HasEffectiveAccessToAllHosts());
  }
}

TEST(PermissionsTest, ExplicitAccessToOrigin) {
  APIPermissionSet apis;
  ManifestPermissionSet manifest_permissions;
  URLPatternSet explicit_hosts;
  URLPatternSet scriptable_hosts;

  AddPattern(&explicit_hosts, "http://*.google.com/*");
  // The explicit host paths should get set to /*.
  AddPattern(&explicit_hosts, "http://www.example.com/a/particular/path/*");

  PermissionSet perm_set(apis, manifest_permissions, explicit_hosts,
                         scriptable_hosts);
  ASSERT_TRUE(
      perm_set.HasExplicitAccessToOrigin(GURL("http://www.google.com/")));
  ASSERT_TRUE(
      perm_set.HasExplicitAccessToOrigin(GURL("http://test.google.com/")));
  ASSERT_TRUE(
      perm_set.HasExplicitAccessToOrigin(GURL("http://www.example.com")));
  ASSERT_TRUE(perm_set.HasEffectiveAccessToURL(GURL("http://www.example.com")));
  ASSERT_FALSE(
      perm_set.HasExplicitAccessToOrigin(GURL("http://test.example.com")));
}

TEST(PermissionsTest, CreateUnion) {
  APIPermission* permission = NULL;

  ManifestPermissionSet manifest_permissions;
  APIPermissionSet apis1;
  APIPermissionSet apis2;
  APIPermissionSet expected_apis;

  URLPatternSet explicit_hosts1;
  URLPatternSet explicit_hosts2;
  URLPatternSet expected_explicit_hosts;

  URLPatternSet scriptable_hosts1;
  URLPatternSet scriptable_hosts2;
  URLPatternSet expected_scriptable_hosts;

  URLPatternSet effective_hosts;

  std::unique_ptr<const PermissionSet> set1;
  std::unique_ptr<const PermissionSet> set2;
  std::unique_ptr<const PermissionSet> union_set;

  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kSocket);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }

  // Union with an empty set.
  apis1.insert(APIPermission::kTab);
  apis1.insert(APIPermission::kBackground);
  apis1.insert(permission->Clone());
  expected_apis.insert(APIPermission::kTab);
  expected_apis.insert(APIPermission::kBackground);
  expected_apis.insert(permission);

  AddPattern(&explicit_hosts1, "http://*.google.com/*");
  AddPattern(&expected_explicit_hosts, "http://*.google.com/*");
  AddPattern(&effective_hosts, "http://*.google.com/*");

  set1.reset(new PermissionSet(apis1, manifest_permissions, explicit_hosts1,
                               scriptable_hosts1));
  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  union_set = PermissionSet::CreateUnion(*set1, *set2);
  EXPECT_TRUE(set1->Contains(*set2));
  EXPECT_TRUE(set1->Contains(*union_set));
  EXPECT_FALSE(set2->Contains(*set1));
  EXPECT_FALSE(set2->Contains(*union_set));
  EXPECT_TRUE(union_set->Contains(*set1));
  EXPECT_TRUE(union_set->Contains(*set2));

  EXPECT_FALSE(union_set->HasEffectiveFullAccess());
  EXPECT_EQ(expected_apis, union_set->apis());
  EXPECT_EQ(expected_explicit_hosts, union_set->explicit_hosts());
  EXPECT_EQ(expected_scriptable_hosts, union_set->scriptable_hosts());
  EXPECT_EQ(expected_explicit_hosts, union_set->effective_hosts());

  // Now use a real second set.
  apis2.insert(APIPermission::kTab);
  apis2.insert(APIPermission::kProxy);
  apis2.insert(APIPermission::kClipboardWrite);
  apis2.insert(APIPermission::kPlugin);

  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-send-to::8899");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  apis2.insert(permission);

  expected_apis.insert(APIPermission::kTab);
  expected_apis.insert(APIPermission::kProxy);
  expected_apis.insert(APIPermission::kClipboardWrite);
  expected_apis.insert(APIPermission::kPlugin);

  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    value->AppendString("udp-send-to::8899");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  // Insert a new permission socket permisssion which will replace the old one.
  expected_apis.insert(permission);

  AddPattern(&explicit_hosts2, "http://*.example.com/*");
  AddPattern(&scriptable_hosts2, "http://*.google.com/*");
  AddPattern(&expected_explicit_hosts, "http://*.example.com/*");
  AddPattern(&expected_scriptable_hosts, "http://*.google.com/*");

  effective_hosts =
      URLPatternSet::CreateUnion(explicit_hosts2, scriptable_hosts2);

  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  union_set = PermissionSet::CreateUnion(*set1, *set2);

  EXPECT_FALSE(set1->Contains(*set2));
  EXPECT_FALSE(set1->Contains(*union_set));
  EXPECT_FALSE(set2->Contains(*set1));
  EXPECT_FALSE(set2->Contains(*union_set));
  EXPECT_TRUE(union_set->Contains(*set1));
  EXPECT_TRUE(union_set->Contains(*set2));

  EXPECT_TRUE(union_set->HasEffectiveFullAccess());
  EXPECT_TRUE(union_set->HasEffectiveAccessToAllHosts());
  EXPECT_EQ(expected_apis, union_set->apis());
  EXPECT_EQ(expected_explicit_hosts, union_set->explicit_hosts());
  EXPECT_EQ(expected_scriptable_hosts, union_set->scriptable_hosts());
  EXPECT_EQ(effective_hosts, union_set->effective_hosts());
}

TEST(PermissionsTest, CreateIntersection) {
  APIPermission* permission = NULL;

  ManifestPermissionSet manifest_permissions;
  APIPermissionSet apis1;
  APIPermissionSet apis2;
  APIPermissionSet expected_apis;

  URLPatternSet explicit_hosts1;
  URLPatternSet explicit_hosts2;
  URLPatternSet expected_explicit_hosts;

  URLPatternSet scriptable_hosts1;
  URLPatternSet scriptable_hosts2;
  URLPatternSet expected_scriptable_hosts;

  URLPatternSet effective_hosts;

  std::unique_ptr<const PermissionSet> set1;
  std::unique_ptr<const PermissionSet> set2;
  std::unique_ptr<const PermissionSet> new_set;

  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kSocket);

  // Intersection with an empty set.
  apis1.insert(APIPermission::kTab);
  apis1.insert(APIPermission::kBackground);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  apis1.insert(permission);

  AddPattern(&explicit_hosts1, "http://*.google.com/*");
  AddPattern(&scriptable_hosts1, "http://www.reddit.com/*");

  set1.reset(new PermissionSet(apis1, manifest_permissions, explicit_hosts1,
                               scriptable_hosts1));
  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  new_set = PermissionSet::CreateIntersection(*set1, *set2);
  EXPECT_TRUE(set1->Contains(*new_set));
  EXPECT_TRUE(set2->Contains(*new_set));
  EXPECT_TRUE(set1->Contains(*set2));
  EXPECT_FALSE(set2->Contains(*set1));
  EXPECT_FALSE(new_set->Contains(*set1));
  EXPECT_TRUE(new_set->Contains(*set2));

  EXPECT_TRUE(new_set->IsEmpty());
  EXPECT_FALSE(new_set->HasEffectiveFullAccess());
  EXPECT_EQ(expected_apis, new_set->apis());
  EXPECT_EQ(expected_explicit_hosts, new_set->explicit_hosts());
  EXPECT_EQ(expected_scriptable_hosts, new_set->scriptable_hosts());
  EXPECT_EQ(expected_explicit_hosts, new_set->effective_hosts());

  // Now use a real second set.
  apis2.insert(APIPermission::kTab);
  apis2.insert(APIPermission::kProxy);
  apis2.insert(APIPermission::kClipboardWrite);
  apis2.insert(APIPermission::kPlugin);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    value->AppendString("udp-send-to::8899");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  apis2.insert(permission);

  expected_apis.insert(APIPermission::kTab);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  expected_apis.insert(permission);

  AddPattern(&explicit_hosts2, "http://*.example.com/*");
  AddPattern(&explicit_hosts2, "http://*.google.com/*");
  AddPattern(&scriptable_hosts2, "http://*.google.com/*");
  AddPattern(&expected_explicit_hosts, "http://*.google.com/*");

  effective_hosts.ClearPatterns();
  AddPattern(&effective_hosts, "http://*.google.com/*");

  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  new_set = PermissionSet::CreateIntersection(*set1, *set2);

  EXPECT_TRUE(set1->Contains(*new_set));
  EXPECT_TRUE(set2->Contains(*new_set));
  EXPECT_FALSE(set1->Contains(*set2));
  EXPECT_FALSE(set2->Contains(*set1));
  EXPECT_FALSE(new_set->Contains(*set1));
  EXPECT_FALSE(new_set->Contains(*set2));

  EXPECT_FALSE(new_set->HasEffectiveFullAccess());
  EXPECT_FALSE(new_set->HasEffectiveAccessToAllHosts());
  EXPECT_EQ(expected_apis, new_set->apis());
  EXPECT_EQ(expected_explicit_hosts, new_set->explicit_hosts());
  EXPECT_EQ(expected_scriptable_hosts, new_set->scriptable_hosts());
  EXPECT_EQ(effective_hosts, new_set->effective_hosts());
}

TEST(PermissionsTest, CreateDifference) {
  APIPermission* permission = NULL;

  ManifestPermissionSet manifest_permissions;
  APIPermissionSet apis1;
  APIPermissionSet apis2;
  APIPermissionSet expected_apis;

  URLPatternSet explicit_hosts1;
  URLPatternSet explicit_hosts2;
  URLPatternSet expected_explicit_hosts;

  URLPatternSet scriptable_hosts1;
  URLPatternSet scriptable_hosts2;
  URLPatternSet expected_scriptable_hosts;

  URLPatternSet effective_hosts;

  std::unique_ptr<const PermissionSet> set1;
  std::unique_ptr<const PermissionSet> set2;
  std::unique_ptr<const PermissionSet> new_set;

  const APIPermissionInfo* permission_info =
    PermissionsInfo::GetInstance()->GetByID(APIPermission::kSocket);

  // Difference with an empty set.
  apis1.insert(APIPermission::kTab);
  apis1.insert(APIPermission::kBackground);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  apis1.insert(permission);

  AddPattern(&explicit_hosts1, "http://*.google.com/*");
  AddPattern(&scriptable_hosts1, "http://www.reddit.com/*");

  set1.reset(new PermissionSet(apis1, manifest_permissions, explicit_hosts1,
                               scriptable_hosts1));
  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  new_set = PermissionSet::CreateDifference(*set1, *set2);
  EXPECT_EQ(*set1, *new_set);

  // Now use a real second set.
  apis2.insert(APIPermission::kTab);
  apis2.insert(APIPermission::kProxy);
  apis2.insert(APIPermission::kClipboardWrite);
  apis2.insert(APIPermission::kPlugin);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("tcp-connect:*.example.com:80");
    value->AppendString("udp-send-to::8899");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  apis2.insert(permission);

  expected_apis.insert(APIPermission::kBackground);
  permission = permission_info->CreateAPIPermission();
  {
    std::unique_ptr<base::ListValue> value(new base::ListValue());
    value->AppendString("udp-bind::8080");
    value->AppendString("udp-send-to::8888");
    ASSERT_TRUE(permission->FromValue(value.get(), NULL, NULL));
  }
  expected_apis.insert(permission);

  AddPattern(&explicit_hosts2, "http://*.example.com/*");
  AddPattern(&explicit_hosts2, "http://*.google.com/*");
  AddPattern(&scriptable_hosts2, "http://*.google.com/*");
  AddPattern(&expected_scriptable_hosts, "http://www.reddit.com/*");

  effective_hosts.ClearPatterns();
  AddPattern(&effective_hosts, "http://www.reddit.com/*");

  set2.reset(new PermissionSet(apis2, manifest_permissions, explicit_hosts2,
                               scriptable_hosts2));
  new_set = PermissionSet::CreateDifference(*set1, *set2);

  EXPECT_TRUE(set1->Contains(*new_set));
  EXPECT_FALSE(set2->Contains(*new_set));

  EXPECT_FALSE(new_set->HasEffectiveFullAccess());
  EXPECT_FALSE(new_set->HasEffectiveAccessToAllHosts());
  EXPECT_EQ(expected_apis, new_set->apis());
  EXPECT_EQ(expected_explicit_hosts, new_set->explicit_hosts());
  EXPECT_EQ(expected_scriptable_hosts, new_set->scriptable_hosts());
  EXPECT_EQ(effective_hosts, new_set->effective_hosts());

  // |set3| = |set1| - |set2| --> |set3| intersect |set2| == empty_set
  set1 = PermissionSet::CreateIntersection(*new_set, *set2);
  EXPECT_TRUE(set1->IsEmpty());
}

TEST(PermissionsTest, IsPrivilegeIncrease) {
  const struct {
    const char* base_name;
    bool expect_increase;
  } kTests[] = {
    { "allhosts1", false },  // all -> all
    { "allhosts2", false },  // all -> one
    { "allhosts3", true },  // one -> all
    { "hosts1", false },  // http://a,http://b -> http://a,http://b
    { "hosts2", true },  // http://a,http://b -> https://a,http://*.b
    { "hosts3", false },  // http://a,http://b -> http://a
    { "hosts4", true },  // http://a -> http://a,http://b
    { "hosts5", false },  // http://a,b,c -> http://a,b,c + https://a,b,c
    { "hosts6", false },  // http://a.com -> http://a.com + http://a.co.uk
    { "permissions1", false },  // tabs -> tabs
    { "permissions2", true },  // tabs -> tabs,bookmarks
    // TODO(treib): This is wrong, kAllHosts implies kTabs. crbug.com/512344
    { "permissions3", true },  // http://*/* -> http://*/*,tabs
    { "permissions5", true },  // bookmarks -> bookmarks,history
    { "equivalent_warnings", false },  // tabs --> tabs, webNavigation
#if !defined(OS_CHROMEOS)  // plugins aren't allowed in ChromeOS
    { "permissions4", false },  // plugin -> plugin,tabs
    { "plugin1", false },  // plugin -> plugin
    { "plugin2", false },  // plugin -> none
    { "plugin3", true },  // none -> plugin
#endif
    { "storage", false },  // none -> storage
    { "notifications", true },  // none -> notifications
    { "platformapp1", false },  // host permissions for platform apps
    { "platformapp2", true },  // API permissions for platform apps
    { "media_galleries1", true },  // all -> read|all
    { "media_galleries2", true },  // read|all -> read|delete|copyTo|all
    { "media_galleries3", true },  // all -> read|delete|all
    { "media_galleries4", false },  // read|all -> all
    { "media_galleries5", false },  // read|copyTo|delete|all -> read|all
    { "media_galleries6", false },  // read|all -> read|all
    { "media_galleries7", true },  // read|delete|all -> read|copyTo|delete|all
    { "sockets1", true },  // none -> tcp:*:*
    { "sockets2", false },  // tcp:*:* -> tcp:*:*
    { "sockets3", true },  // tcp:a.com:80 -> tcp:*:*
  };

  for (size_t i = 0; i < arraysize(kTests); ++i) {
    scoped_refptr<Extension> old_extension(
        LoadManifest("allow_silent_upgrade",
                     std::string(kTests[i].base_name) + "_old.json"));
    scoped_refptr<Extension> new_extension(
        LoadManifest("allow_silent_upgrade",
                     std::string(kTests[i].base_name) + "_new.json"));

    EXPECT_TRUE(new_extension.get()) << kTests[i].base_name << "_new.json";
    if (!new_extension.get())
      continue;

    const PermissionSet& old_p =
        old_extension->permissions_data()->active_permissions();
    const PermissionSet& new_p =
        new_extension->permissions_data()->active_permissions();
    Manifest::Type extension_type = old_extension->GetType();

    bool increased = PermissionMessageProvider::Get()->IsPrivilegeIncrease(
        old_p, new_p, extension_type);
    EXPECT_EQ(kTests[i].expect_increase, increased) << kTests[i].base_name;
  }
}

TEST(PermissionsTest, PermissionMessages) {
  // Ensure that all permissions that needs to show install UI actually have
  // strings associated with them.
  APIPermissionSet skip;

  // These are considered "nuisance" or "trivial" permissions that don't need
  // a prompt.
  skip.insert(APIPermission::kActiveTab);
  skip.insert(APIPermission::kAlarms);
  skip.insert(APIPermission::kAlphaEnabled);
  skip.insert(APIPermission::kAlwaysOnTopWindows);
  skip.insert(APIPermission::kAppView);
  skip.insert(APIPermission::kAudio);
  skip.insert(APIPermission::kBrowsingData);
  skip.insert(APIPermission::kCastStreaming);
  skip.insert(APIPermission::kCommandsAccessibility);
  skip.insert(APIPermission::kContextMenus);
  skip.insert(APIPermission::kCryptotokenPrivate);
  skip.insert(APIPermission::kDesktopCapturePrivate);
  skip.insert(APIPermission::kDiagnostics);
  skip.insert(APIPermission::kDns);
  skip.insert(APIPermission::kDownloadsShelf);
  skip.insert(APIPermission::kEmbeddedExtensionOptions);
  skip.insert(APIPermission::kExtensionView);
  skip.insert(APIPermission::kFontSettings);
  skip.insert(APIPermission::kFullscreen);
  skip.insert(APIPermission::kGcm);
  skip.insert(APIPermission::kIdle);
  skip.insert(APIPermission::kImeWindowEnabled);
  skip.insert(APIPermission::kInlineInstallPrivate);
  skip.insert(APIPermission::kIdltest);
  skip.insert(APIPermission::kOverrideEscFullscreen);
  skip.insert(APIPermission::kPointerLock);
  skip.insert(APIPermission::kPower);
  skip.insert(APIPermission::kPrinterProvider);
  skip.insert(APIPermission::kSessions);
  skip.insert(APIPermission::kStorage);
  skip.insert(APIPermission::kSystemCpu);
  skip.insert(APIPermission::kSystemDisplay);
  skip.insert(APIPermission::kSystemMemory);
  skip.insert(APIPermission::kSystemNetwork);
  skip.insert(APIPermission::kSystemStorage);
  skip.insert(APIPermission::kTts);
  skip.insert(APIPermission::kUnlimitedStorage);
  skip.insert(APIPermission::kWebcamPrivate);
  skip.insert(APIPermission::kWebView);
  skip.insert(APIPermission::kWindowShape);

  // These permissions are restricted to extensions force-installed by policy
  // and don't require a prompt, i.e. they're restricted to location 'policy'.
  skip.insert(APIPermission::kEnterprisePlatformKeys);
  skip.insert(APIPermission::kEnterpriseDeviceAttributes);

  // TODO(erikkay) add a string for this permission.
  skip.insert(APIPermission::kBackground);

  skip.insert(APIPermission::kClipboard);

  // The cookie permission does nothing unless you have associated host
  // permissions.
  skip.insert(APIPermission::kCookie);

  // These are warned as part of host permission checks.
  skip.insert(APIPermission::kDataReductionProxy);
  skip.insert(APIPermission::kDeclarativeContent);
  skip.insert(APIPermission::kPageCapture);
  skip.insert(APIPermission::kProxy);
  skip.insert(APIPermission::kTabCapture);
  skip.insert(APIPermission::kWebRequest);
  skip.insert(APIPermission::kWebRequestBlocking);

  // This permission requires explicit user action (context menu handler)
  // so we won't prompt for it for now.
  skip.insert(APIPermission::kFileBrowserHandler);

  // These permissions require explicit user action (configuration dialog)
  // so we don't prompt for them at install time.
  skip.insert(APIPermission::kMediaGalleries);

  // If you've turned on the experimental command-line flag, we don't need
  // to warn you further.
  skip.insert(APIPermission::kExperimental);

  // The Identity API has its own server-driven permission prompts.
  skip.insert(APIPermission::kIdentity);

  // These are private.
  skip.insert(APIPermission::kAccessibilityPrivate);
  skip.insert(APIPermission::kAutoTestPrivate);
  skip.insert(APIPermission::kBookmarkManagerPrivate);
  skip.insert(APIPermission::kBrailleDisplayPrivate);
  skip.insert(APIPermission::kCast);
  skip.insert(APIPermission::kCastStreaming);
  skip.insert(APIPermission::kChromeosInfoPrivate);
  skip.insert(APIPermission::kCloudPrintPrivate);
  skip.insert(APIPermission::kCommandLinePrivate);
  skip.insert(APIPermission::kDeveloperPrivate);
  skip.insert(APIPermission::kDial);
  skip.insert(APIPermission::kDownloadsInternal);
  skip.insert(APIPermission::kEasyUnlockPrivate);
  skip.insert(APIPermission::kEchoPrivate);
  skip.insert(APIPermission::kEnterprisePlatformKeysPrivate);
  skip.insert(APIPermission::kFeedbackPrivate);
  skip.insert(APIPermission::kFileBrowserHandlerInternal);
  skip.insert(APIPermission::kFileManagerPrivate);
  skip.insert(APIPermission::kFirstRunPrivate);
  skip.insert(APIPermission::kHotwordPrivate);
  skip.insert(APIPermission::kIdentityPrivate);
  skip.insert(APIPermission::kInputMethodPrivate);
  skip.insert(APIPermission::kLanguageSettingsPrivate);
  skip.insert(APIPermission::kMediaPlayerPrivate);
  skip.insert(APIPermission::kMediaPerceptionPrivate);
  skip.insert(APIPermission::kMediaRouterPrivate);
  skip.insert(APIPermission::kMetricsPrivate);
  skip.insert(APIPermission::kNetworkingCastPrivate);
  skip.insert(APIPermission::kPreferencesPrivate);
  skip.insert(APIPermission::kImageWriterPrivate);
  skip.insert(APIPermission::kResourcesPrivate);
  skip.insert(APIPermission::kRtcPrivate);
  skip.insert(APIPermission::kStreamsPrivate);
  skip.insert(APIPermission::kSystemPrivate);
  skip.insert(APIPermission::kTabCaptureForTab);
  skip.insert(APIPermission::kTerminalPrivate);
  skip.insert(APIPermission::kVirtualKeyboardPrivate);
  skip.insert(APIPermission::kWallpaperPrivate);
  skip.insert(APIPermission::kWebrtcAudioPrivate);
  skip.insert(APIPermission::kWebrtcDesktopCapturePrivate);
  skip.insert(APIPermission::kWebrtcLoggingPrivate);
  skip.insert(APIPermission::kWebstorePrivate);
  skip.insert(APIPermission::kWebstoreWidgetPrivate);

  // Warned as part of host permissions.
  skip.insert(APIPermission::kDevtools);

  // Platform apps.
  skip.insert(APIPermission::kBrowser);
  skip.insert(APIPermission::kHid);
  skip.insert(APIPermission::kFileSystem);
  skip.insert(APIPermission::kFileSystemProvider);
  skip.insert(APIPermission::kFileSystemRequestFileSystem);
  skip.insert(APIPermission::kFileSystemRetainEntries);
  skip.insert(APIPermission::kFileSystemWrite);
  skip.insert(APIPermission::kSocket);
  skip.insert(APIPermission::kUsb);
  skip.insert(APIPermission::kVirtualKeyboard);
  skip.insert(APIPermission::kLauncherSearchProvider);

  // The lock screen apps are set by user through settings, no need to warn at
  // installation time.
  skip.insert(APIPermission::kLockScreen);

  // We already have a generic message for declaring externally_connectable.
  skip.insert(APIPermission::kExternallyConnectableAllUrls);

  // TODO(crbug.com/696822): Implement the permission model for Declarative Net
  // Request API.
  skip.insert(APIPermission::kDeclarativeNetRequest);

  const PermissionMessageProvider* provider = PermissionMessageProvider::Get();
  PermissionsInfo* info = PermissionsInfo::GetInstance();
  APIPermissionSet permissions = info->GetAll();
  for (APIPermissionSet::const_iterator i = permissions.begin();
       i != permissions.end(); ++i) {
    const APIPermissionInfo* permission_info = i->info();
    EXPECT_TRUE(permission_info != NULL);

    PermissionIDSet id;
    id.insert(permission_info->id());
    bool has_message = !provider->GetPermissionMessages(id).empty();
    bool should_have_message = !skip.count(i->id());
    EXPECT_EQ(should_have_message, has_message) << permission_info->name();
  }
}

TEST(PermissionsTest, FileSystemPermissionMessages) {
  APIPermissionSet api_permissions;
  api_permissions.insert(APIPermission::kFileSystemWrite);
  api_permissions.insert(APIPermission::kFileSystemDirectory);
  PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                            URLPatternSet(), URLPatternSet());
  EXPECT_TRUE(
      PermissionSetProducesMessage(permissions, Manifest::TYPE_PLATFORM_APP,
                                   MakePermissionIDSet(api_permissions)));
}

TEST(PermissionsTest, HiddenFileSystemPermissionMessages) {
  APIPermissionSet api_permissions;
  api_permissions.insert(APIPermission::kFileSystemWrite);
  api_permissions.insert(APIPermission::kFileSystemDirectory);
  PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                            URLPatternSet(), URLPatternSet());
  EXPECT_TRUE(
      PermissionSetProducesMessage(permissions, Manifest::TYPE_PLATFORM_APP,
                                   MakePermissionIDSet(api_permissions)));
}

TEST(PermissionsTest, SuppressedPermissionMessages) {
  {
    // Tabs warning suppresses favicon warning.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kTab);
    URLPatternSet hosts;
    hosts.AddPattern(URLPattern(URLPattern::SCHEME_CHROMEUI,
                                "chrome://favicon/"));
    PermissionSet permissions(api_permissions, ManifestPermissionSet(), hosts,
                              URLPatternSet());
    EXPECT_TRUE(PermissionSetProducesMessage(
        permissions, Manifest::TYPE_EXTENSION,
        MakePermissionIDSet(APIPermission::kTab, APIPermission::kFavicon)));
  }
  {
    // History warning suppresses favicon warning.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kHistory);
    URLPatternSet hosts;
    hosts.AddPattern(URLPattern(URLPattern::SCHEME_CHROMEUI,
                                "chrome://favicon/"));
    PermissionSet permissions(api_permissions, ManifestPermissionSet(), hosts,
                              URLPatternSet());
    EXPECT_TRUE(PermissionSetProducesMessage(
        permissions, Manifest::TYPE_EXTENSION,
        MakePermissionIDSet(APIPermission::kHistory, APIPermission::kFavicon)));
  }
  {
    // All sites warning suppresses tabs warning.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kTab);
    URLPatternSet hosts;
    hosts.AddPattern(URLPattern(URLPattern::SCHEME_CHROMEUI, "*://*/*"));
    PermissionSet permissions(api_permissions, ManifestPermissionSet(), hosts,
                              URLPatternSet());
    EXPECT_TRUE(PermissionSetProducesMessage(
        permissions, Manifest::TYPE_EXTENSION,
        MakePermissionIDSet(APIPermission::kHostsAll, APIPermission::kTab)));
  }
  {
    // All sites warning suppresses topSites warning.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kTopSites);
    URLPatternSet hosts;
    hosts.AddPattern(URLPattern(URLPattern::SCHEME_CHROMEUI, "*://*/*"));
    PermissionSet permissions(api_permissions, ManifestPermissionSet(), hosts,
                              URLPatternSet());
    EXPECT_TRUE(PermissionSetProducesMessage(
        permissions, Manifest::TYPE_EXTENSION,
        MakePermissionIDSet(APIPermission::kHostsAll,
                            APIPermission::kTopSites)));
  }
  {
    // All sites warning suppresses declarativeWebRequest warning.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kDeclarativeWebRequest);
    URLPatternSet hosts;
    hosts.AddPattern(URLPattern(URLPattern::SCHEME_CHROMEUI, "*://*/*"));
    PermissionSet permissions(api_permissions, ManifestPermissionSet(), hosts,
                              URLPatternSet());
    EXPECT_TRUE(PermissionSetProducesMessage(
        permissions, Manifest::TYPE_EXTENSION,
        MakePermissionIDSet(APIPermission::kHostsAll)));
  }
  {
    // BrowsingHistory warning suppresses all history read/write warnings.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kHistory);
    api_permissions.insert(APIPermission::kTab);
    api_permissions.insert(APIPermission::kTopSites);
    api_permissions.insert(APIPermission::kProcesses);
    api_permissions.insert(APIPermission::kWebNavigation);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    EXPECT_TRUE(
        PermissionSetProducesMessage(permissions, Manifest::TYPE_EXTENSION,
                                     MakePermissionIDSet(api_permissions)));
  }
  {
    // Tabs warning suppresses all read-only history warnings.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kTab);
    api_permissions.insert(APIPermission::kTopSites);
    api_permissions.insert(APIPermission::kProcesses);
    api_permissions.insert(APIPermission::kWebNavigation);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    EXPECT_TRUE(
        PermissionSetProducesMessage(permissions, Manifest::TYPE_EXTENSION,
                                     MakePermissionIDSet(api_permissions)));
  }
}

TEST(PermissionsTest, AccessToDevicesMessages) {
  {
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kSerial);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    VerifyOnePermissionMessage(
        permissions, Manifest::TYPE_EXTENSION,
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_SERIAL));
  }
  {
    // Testing that multiple permissions will show the one message.
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kSerial);
    api_permissions.insert(APIPermission::kSerial);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    VerifyOnePermissionMessage(
        permissions, Manifest::TYPE_EXTENSION,
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_SERIAL));
  }
  {
    scoped_refptr<Extension> extension =
        LoadManifest("permissions", "access_to_devices_bluetooth.json");
    PermissionSet& set = const_cast<PermissionSet&>(
        extension->permissions_data()->active_permissions());
    VerifyOnePermissionMessage(
        set, extension->GetType(),
        l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_BLUETOOTH));

    // Test Bluetooth and Serial
    set.apis_.insert(APIPermission::kSerial);
    VerifyOnePermissionMessage(
        set, extension->GetType(),
        l10n_util::GetStringUTF16(
            IDS_EXTENSION_PROMPT_WARNING_BLUETOOTH_SERIAL));
  }
}

TEST(PermissionsTest, MergedFileSystemPermissionComparison) {
  APIPermissionSet write_api_permissions;
  write_api_permissions.insert(APIPermission::kFileSystemWrite);
  PermissionSet write_permissions(write_api_permissions,
                                  ManifestPermissionSet(), URLPatternSet(),
                                  URLPatternSet());

  APIPermissionSet directory_api_permissions;
  directory_api_permissions.insert(APIPermission::kFileSystemDirectory);
  PermissionSet directory_permissions(directory_api_permissions,
                                      ManifestPermissionSet(), URLPatternSet(),
                                      URLPatternSet());

  APIPermissionSet write_directory_api_permissions;
  write_directory_api_permissions.insert(APIPermission::kFileSystemWrite);
  write_directory_api_permissions.insert(APIPermission::kFileSystemDirectory);
  PermissionSet write_directory_permissions(write_directory_api_permissions,
                                            ManifestPermissionSet(),
                                            URLPatternSet(), URLPatternSet());

  const PermissionMessageProvider* provider = PermissionMessageProvider::Get();
  EXPECT_FALSE(provider->IsPrivilegeIncrease(write_directory_permissions,
                                             write_permissions,
                                             Manifest::TYPE_PLATFORM_APP));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(write_directory_permissions,
                                             directory_permissions,
                                             Manifest::TYPE_PLATFORM_APP));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(
      write_permissions, directory_permissions, Manifest::TYPE_PLATFORM_APP));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(write_permissions,
                                            write_directory_permissions,
                                            Manifest::TYPE_PLATFORM_APP));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(
      directory_permissions, write_permissions, Manifest::TYPE_PLATFORM_APP));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(directory_permissions,
                                            write_directory_permissions,
                                            Manifest::TYPE_PLATFORM_APP));
}

TEST(PermissionsTest, GetWarningMessages_ManyHosts) {
  scoped_refptr<Extension> extension;
  extension = LoadManifest("permissions", "many-hosts.json");
  EXPECT_TRUE(VerifyOnePermissionMessage(
      extension->permissions_data(),
      "Read and change your data on encrypted.google.com and www.google.com"));
}

TEST(PermissionsTest, GetWarningMessages_Plugins) {
  scoped_refptr<Extension> extension;
  extension = LoadManifest("permissions", "plugins.json");
// We don't parse the plugins key on Chrome OS, so it should not ask for any
// permissions.
#if defined(OS_CHROMEOS)
  EXPECT_TRUE(VerifyNoPermissionMessages(extension->permissions_data()));
#else
  EXPECT_TRUE(VerifyOnePermissionMessage(
      extension->permissions_data(),
      "Read and change all your data on your computer and the websites you "
      "visit"));
#endif
}

TEST(PermissionsTest, GetWarningMessages_AudioVideo) {
  const std::string kAudio("Use your microphone");
  const std::string kVideo("Use your camera");
  const std::string kBoth("Use your microphone and camera");

  // Both audio and video present.
  scoped_refptr<Extension> extension =
      LoadManifest("permissions", "audio-video.json");
  const PermissionMessageProvider* provider = PermissionMessageProvider::Get();
  PermissionSet& set = const_cast<PermissionSet&>(
      extension->permissions_data()->active_permissions());
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kAudio));
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kVideo));
  EXPECT_TRUE(VerifyHasPermissionMessage(set, extension->GetType(), kBoth));
  PermissionMessages warnings = provider->GetPermissionMessages(
      provider->GetAllPermissionIDs(set, extension->GetType()));
  size_t combined_index = IndexOf(warnings, kBoth);
  size_t combined_size = warnings.size();

  // Just audio present.
  set.apis_.erase(APIPermission::kVideoCapture);
  EXPECT_TRUE(VerifyHasPermissionMessage(set, extension->GetType(), kAudio));
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kVideo));
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kBoth));
  PermissionMessages warnings2 = provider->GetPermissionMessages(
      provider->GetAllPermissionIDs(set, extension->GetType()));
  EXPECT_EQ(combined_size, warnings2.size());
  EXPECT_EQ(combined_index, IndexOf(warnings2, kAudio));

  // Just video present.
  set.apis_.erase(APIPermission::kAudioCapture);
  set.apis_.insert(APIPermission::kVideoCapture);
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kAudio));
  EXPECT_TRUE(VerifyHasPermissionMessage(set, extension->GetType(), kVideo));
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(), kBoth));
  PermissionMessages warnings3 = provider->GetPermissionMessages(
      provider->GetAllPermissionIDs(set, extension->GetType()));
  EXPECT_EQ(combined_size, warnings3.size());
  EXPECT_EQ(combined_index, IndexOf(warnings3, kVideo));
}

TEST(PermissionsTest, GetWarningMessages_CombinedSessions) {
  {
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kTab);
    api_permissions.insert(APIPermission::kTopSites);
    api_permissions.insert(APIPermission::kProcesses);
    api_permissions.insert(APIPermission::kWebNavigation);
    api_permissions.insert(APIPermission::kSessions);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    EXPECT_TRUE(VerifyOnePermissionMessage(
        permissions, Manifest::TYPE_EXTENSION,
        l10n_util::GetStringUTF16(
            IDS_EXTENSION_PROMPT_WARNING_HISTORY_READ_AND_SESSIONS)));
  }
  {
    APIPermissionSet api_permissions;
    api_permissions.insert(APIPermission::kHistory);
    api_permissions.insert(APIPermission::kTab);
    api_permissions.insert(APIPermission::kTopSites);
    api_permissions.insert(APIPermission::kProcesses);
    api_permissions.insert(APIPermission::kWebNavigation);
    api_permissions.insert(APIPermission::kSessions);
    PermissionSet permissions(api_permissions, ManifestPermissionSet(),
                              URLPatternSet(), URLPatternSet());
    EXPECT_TRUE(VerifyOnePermissionMessage(
        permissions, Manifest::TYPE_EXTENSION,
        l10n_util::GetStringUTF16(
            IDS_EXTENSION_PROMPT_WARNING_HISTORY_WRITE_AND_SESSIONS)));
  }
}

TEST(PermissionsTest, GetWarningMessages_DeclarativeWebRequest) {
  // Test that if the declarativeWebRequest permission is present
  // in combination with all hosts permission, then only the warning
  // for host permissions is shown, because that covers the use of
  // declarativeWebRequest.

  // Until Declarative Web Request is in stable, let's make sure it is enabled
  // on the current channel.
  ScopedCurrentChannel sc(version_info::Channel::CANARY);

  {
    // First verify that declarativeWebRequest produces a message when host
    // permissions do not cover all hosts.
    scoped_refptr<const Extension> extension = LoadManifest(
        "permissions", "web_request_not_all_host_permissions.json");
    const PermissionSet& set =
        extension->permissions_data()->active_permissions();
    EXPECT_TRUE(VerifyHasPermissionMessage(set, extension->GetType(),
                                           "Block parts of web pages"));
    EXPECT_FALSE(VerifyHasPermissionMessage(
        set, extension->GetType(),
        "Read and change all your data on the websites you visit"));
  }

  {
  // Now verify that declarativeWebRequest does not produce a message when host
  // permissions do cover all hosts.
  scoped_refptr<const Extension> extension =
      LoadManifest("permissions", "web_request_all_host_permissions.json");
  const PermissionSet& set =
      extension->permissions_data()->active_permissions();
  EXPECT_FALSE(VerifyHasPermissionMessage(set, extension->GetType(),
                                          "Block parts of web pages"));
  EXPECT_TRUE(VerifyHasPermissionMessage(
      set, extension->GetType(),
      "Read and change all your data on the websites you visit"));
  }
}

TEST(PermissionsTest, GetWarningMessages_Serial) {
  scoped_refptr<Extension> extension =
      LoadManifest("permissions", "serial.json");

  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(
      extension->permissions_data()->HasAPIPermission(APIPermission::kSerial));
  EXPECT_TRUE(VerifyOnePermissionMessage(extension->permissions_data(),
                                         "Access your serial devices"));
}

TEST(PermissionsTest, GetWarningMessages_Socket_AnyHost) {
  ScopedCurrentChannel channel(version_info::Channel::DEV);

  scoped_refptr<Extension> extension =
      LoadManifest("permissions", "socket_any_host.json");
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(
      extension->permissions_data()->HasAPIPermission(APIPermission::kSocket));
  EXPECT_TRUE(VerifyOnePermissionMessage(
      extension->permissions_data(),
      "Exchange data with any device on the local network or internet"));
}

TEST(PermissionsTest, GetWarningMessages_Socket_OneDomainTwoHostnames) {
  ScopedCurrentChannel channel(version_info::Channel::DEV);

  scoped_refptr<Extension> extension =
      LoadManifest("permissions", "socket_one_domain_two_hostnames.json");
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(
      extension->permissions_data()->HasAPIPermission(APIPermission::kSocket));

  // Verify the warnings, including support for unicode characters, the fact
  // that domain host warnings come before specific host warnings, and the fact
  // that domains and hostnames are in alphabetical order regardless of the
  // order in the manifest file.
  EXPECT_TRUE(VerifyTwoPermissionMessages(
      extension->permissions_data(),
      "Exchange data with any device in the domain example.org",
      "Exchange data with the devices named: "
      "b\xC3\xA5r.example.com foo.example.com",
      // "\xC3\xA5" = UTF-8 for lowercase A with ring above
      true));
}

TEST(PermissionsTest, GetWarningMessages_Socket_TwoDomainsOneHostname) {
  ScopedCurrentChannel channel(version_info::Channel::DEV);

  scoped_refptr<Extension> extension =
      LoadManifest("permissions", "socket_two_domains_one_hostname.json");
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(
      extension->permissions_data()->HasAPIPermission(APIPermission::kSocket));

  // Verify the warnings, including the fact that domain host warnings come
  // before specific host warnings and the fact that domains and hostnames are
  // in alphabetical order regardless of the order in the manifest file.
  EXPECT_TRUE(VerifyTwoPermissionMessages(
      extension->permissions_data(),
      "Exchange data with any device in the domains: "
      "example.com foo.example.org",
      "Exchange data with the device named bar.example.org", true));
}

// Since platform apps always use isolated storage, they can't (silently)
// access user data on other domains, so there's no need to prompt about host
// permissions. See crbug.com/255229.
TEST(PermissionsTest, GetWarningMessages_PlatformAppHosts) {
  scoped_refptr<Extension> extension;

  extension = LoadManifest("permissions", "platform_app_hosts.json");
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(VerifyNoPermissionMessages(extension->permissions_data()));

  extension = LoadManifest("permissions", "platform_app_all_urls.json");
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(VerifyNoPermissionMessages(extension->permissions_data()));
}

testing::AssertionResult ShowsAllHostsWarning(const std::string& pattern) {
  scoped_refptr<Extension> extension =
      ExtensionBuilder()
          .SetManifest(
              DictionaryBuilder()
                  .Set("name", "TLDWildCardTest")
                  .Set("version", "0.1.0")
                  .Set("permissions", ListBuilder().Append(pattern).Build())
                  .Build())
          .Build();

  return VerifyHasPermissionMessage(
      extension->permissions_data(),
      l10n_util::GetStringUTF16(IDS_EXTENSION_PROMPT_WARNING_ALL_HOSTS));
}

TEST(PermissionsTest, GetWarningMessages_TLDWildcardTreatedAsAllHosts) {
  EXPECT_TRUE(ShowsAllHostsWarning("http://*.com/*"));    // most popular.
  EXPECT_TRUE(ShowsAllHostsWarning("http://*.org/*"));    // sanity check.
  EXPECT_TRUE(ShowsAllHostsWarning("http://*.co.uk/*"));  // eTLD.
  EXPECT_TRUE(ShowsAllHostsWarning("http://*.de/*"));  // foreign country tld.

  // We should still show the normal permissions (i.e., "Can access your data on
  // *.rdcronin.com") for things that are not TLDs.
  EXPECT_FALSE(ShowsAllHostsWarning("http://*.rdcronin.com/*"));

  // Pseudo-TLDs, like appspot.com, should not show all hosts.
  EXPECT_FALSE(ShowsAllHostsWarning("http://*.appspot.com/*"));

  // Non-TLDs should be likewise exempt.
  EXPECT_FALSE(ShowsAllHostsWarning("http://*.notarealtld/*"));

  // Our internal checks use "foo", so let's make sure we're not messing
  // something up with it.
  EXPECT_FALSE(ShowsAllHostsWarning("http://*.foo.com"));
  EXPECT_FALSE(ShowsAllHostsWarning("http://foo.com"));
  // This will fail if foo becomes a recognized TLD. Which could be soon.
  // Update as needed.
  EXPECT_FALSE(ShowsAllHostsWarning("http://*.foo"));
}

TEST(PermissionsTest, GetDistinctHosts) {
  URLPatternSet explicit_hosts;
  std::set<std::string> expected;
  expected.insert("www.foo.com");
  expected.insert("www.bar.com");
  expected.insert("www.baz.com");

  {
    SCOPED_TRACE("no dupes");

    // Simple list with no dupes.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.com/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.bar.com/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.baz.com/path"));
    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("two dupes");

    // Add some dupes.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.com/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.baz.com/path"));
    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("schemes differ");

    // Add a pattern that differs only by scheme. This should be filtered out.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTPS, "https://www.bar.com/path"));
    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("paths differ");

    // Add some dupes by path.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.bar.com/pathypath"));
    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("subdomains differ");

    // We don't do anything special for subdomains.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://monkey.www.bar.com/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://bar.com/path"));

    expected.insert("monkey.www.bar.com");
    expected.insert("bar.com");

    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("RCDs differ");

    // Now test for RCD uniquing.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.com/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.co.uk/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.de/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.ca.us/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.net/path"));
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.com.my/path"));

    // This is an unknown RCD, which shouldn't be uniqued out.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.xyzzy/path"));
    // But it should only occur once.
    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.xyzzy/path"));

    expected.insert("www.foo.xyzzy");

    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("wildcards");

    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://*.google.com/*"));

    expected.insert("*.google.com");

    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }

  {
    SCOPED_TRACE("scriptable hosts");

    APIPermissionSet empty_perms;
    explicit_hosts.ClearPatterns();
    URLPatternSet scriptable_hosts;
    expected.clear();

    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://*.google.com/*"));
    scriptable_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_HTTP, "http://*.example.com/*"));

    expected.insert("*.google.com");
    expected.insert("*.example.com");

    PermissionSet perm_set(empty_perms, ManifestPermissionSet(), explicit_hosts,
                           scriptable_hosts);
    EXPECT_EQ(expected, permission_message_util::GetDistinctHosts(
                            perm_set.effective_hosts(), true, true));
  }

  {
    // We don't display warnings for file URLs because they are off by default.
    SCOPED_TRACE("file urls");

    explicit_hosts.ClearPatterns();
    expected.clear();

    explicit_hosts.AddPattern(
        URLPattern(URLPattern::SCHEME_FILE, "file:///*"));

    EXPECT_EQ(expected,
              permission_message_util::GetDistinctHosts(
                  explicit_hosts, true, true));
  }
}

TEST(PermissionsTest, GetDistinctHosts_ComIsBestRcd) {
  URLPatternSet explicit_hosts;
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.ca/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.org/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.co.uk/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.net/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.jp/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.com/path"));

  std::set<std::string> expected;
  expected.insert("www.foo.com");
  EXPECT_EQ(expected,
            permission_message_util::GetDistinctHosts(
                explicit_hosts, true, true));
}

TEST(PermissionsTest, GetDistinctHosts_NetIs2ndBestRcd) {
  URLPatternSet explicit_hosts;
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.ca/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.org/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.co.uk/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.net/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.jp/path"));
  // No http://www.foo.com/path

  std::set<std::string> expected;
  expected.insert("www.foo.net");
  EXPECT_EQ(expected,
            permission_message_util::GetDistinctHosts(
                explicit_hosts, true, true));
}

TEST(PermissionsTest, GetDistinctHosts_OrgIs3rdBestRcd) {
  URLPatternSet explicit_hosts;
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.ca/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.org/path"));
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.co.uk/path"));
  // No http://www.foo.net/path
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.jp/path"));
  // No http://www.foo.com/path

  std::set<std::string> expected;
  expected.insert("www.foo.org");
  EXPECT_EQ(expected,
            permission_message_util::GetDistinctHosts(
                explicit_hosts, true, true));
}

TEST(PermissionsTest, GetDistinctHosts_FirstInListIs4thBestRcd) {
  URLPatternSet explicit_hosts;
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.ca/path"));
  // No http://www.foo.org/path
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.co.uk/path"));
  // No http://www.foo.net/path
  explicit_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.foo.jp/path"));
  // No http://www.foo.com/path

  std::set<std::string> expected;
  expected.insert("www.foo.ca");
  EXPECT_EQ(expected,
            permission_message_util::GetDistinctHosts(
                explicit_hosts, true, true));
}

TEST(PermissionsTest, IsHostPrivilegeIncrease) {
  Manifest::Type type = Manifest::TYPE_EXTENSION;
  const PermissionMessageProvider* provider = PermissionMessageProvider::Get();
  ManifestPermissionSet empty_manifest_permissions;
  URLPatternSet elist1;
  URLPatternSet elist2;
  URLPatternSet slist1;
  URLPatternSet slist2;
  std::unique_ptr<const PermissionSet> set1;
  std::unique_ptr<const PermissionSet> set2;
  APIPermissionSet empty_perms;
  elist1.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com.hk/path"));
  elist1.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com/path"));

  // Test that the host order does not matter.
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com/path"));
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com.hk/path"));

  set1.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist1,
                               slist1));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));

  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set2, *set1, type));

  // Test that paths are ignored.
  elist2.ClearPatterns();
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com/*"));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set2, *set1, type));

  // Test that RCDs are ignored.
  elist2.ClearPatterns();
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com.hk/*"));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set2, *set1, type));

  // Test that subdomain wildcards are handled properly.
  elist2.ClearPatterns();
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://*.google.com.hk/*"));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  // TODO(jstritar): Does not match subdomains properly. http://crbug.com/65337
  // EXPECT_FALSE(provider->IsPrivilegeIncrease(set2, set1, type));

  // Test that different domains count as different hosts.
  elist2.ClearPatterns();
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.google.com/path"));
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://www.example.org/path"));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set2, *set1, type));

  // Test that different subdomains count as different hosts.
  elist2.ClearPatterns();
  elist2.AddPattern(
      URLPattern(URLPattern::SCHEME_HTTP, "http://mail.google.com/*"));
  set2.reset(new PermissionSet(empty_perms, empty_manifest_permissions, elist2,
                               slist2));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_TRUE(provider->IsPrivilegeIncrease(*set2, *set1, type));

  // Test that platform apps do not have host permissions increases.
  type = Manifest::TYPE_PLATFORM_APP;
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set1, *set2, type));
  EXPECT_FALSE(provider->IsPrivilegeIncrease(*set2, *set1, type));
}

TEST(PermissionsTest, GetAPIsAsStrings) {
  APIPermissionSet apis;
  URLPatternSet empty_set;

  apis.insert(APIPermission::kProxy);
  apis.insert(APIPermission::kBackground);
  apis.insert(APIPermission::kNotifications);
  apis.insert(APIPermission::kTab);

  PermissionSet perm_set(apis, ManifestPermissionSet(), empty_set, empty_set);
  std::set<std::string> api_names = perm_set.GetAPIsAsStrings();

  // The result is correct if it has the same number of elements
  // and we can convert it back to the id set.
  EXPECT_EQ(4u, api_names.size());
  EXPECT_EQ(apis,
            PermissionsInfo::GetInstance()->GetAllByName(api_names));
}

TEST(PermissionsTest, IsEmpty) {
  APIPermissionSet empty_apis;
  URLPatternSet empty_extent;

  std::unique_ptr<const PermissionSet> empty(new PermissionSet());
  EXPECT_TRUE(empty->IsEmpty());
  std::unique_ptr<const PermissionSet> perm_set;

  perm_set.reset(new PermissionSet(empty_apis, ManifestPermissionSet(),
                                   empty_extent, empty_extent));
  EXPECT_TRUE(perm_set->IsEmpty());

  APIPermissionSet non_empty_apis;
  non_empty_apis.insert(APIPermission::kBackground);
  perm_set.reset(new PermissionSet(non_empty_apis, ManifestPermissionSet(),
                                   empty_extent, empty_extent));
  EXPECT_FALSE(perm_set->IsEmpty());

  // Try non standard host
  URLPatternSet non_empty_extent;
  AddPattern(&non_empty_extent, "http://www.google.com/*");

  perm_set.reset(new PermissionSet(empty_apis, ManifestPermissionSet(),
                                   non_empty_extent, empty_extent));
  EXPECT_FALSE(perm_set->IsEmpty());

  perm_set.reset(new PermissionSet(empty_apis, ManifestPermissionSet(),
                                   empty_extent, non_empty_extent));
  EXPECT_FALSE(perm_set->IsEmpty());
}

TEST(PermissionsTest, ImpliedPermissions) {
  URLPatternSet empty_extent;
  APIPermissionSet apis;
  apis.insert(APIPermission::kFileBrowserHandler);
  EXPECT_EQ(1U, apis.size());

  PermissionSet perm_set(apis, ManifestPermissionSet(), empty_extent,
                         empty_extent);
  EXPECT_EQ(2U, perm_set.apis().size());
}

TEST(PermissionsTest, SyncFileSystemPermission) {
  scoped_refptr<Extension> extension = LoadManifest(
      "permissions", "sync_file_system.json");
  APIPermissionSet apis;
  apis.insert(APIPermission::kSyncFileSystem);
  EXPECT_TRUE(extension->is_platform_app());
  EXPECT_TRUE(extension->permissions_data()->HasAPIPermission(
      APIPermission::kSyncFileSystem));
  EXPECT_TRUE(
      VerifyOnePermissionMessage(extension->permissions_data(),
                                 "Store data in your Google Drive account"));
}

// Make sure that we don't crash when we're trying to show the permissions
// even though chrome://thumb (and everything that's not chrome://favicon with
// a chrome:// scheme) is not a valid permission.
// More details here: crbug/246314.
TEST(PermissionsTest, ChromeURLs) {
  URLPatternSet allowed_hosts;
  allowed_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_ALL, "http://www.google.com/"));
  allowed_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_ALL, "chrome://favicon/"));
  allowed_hosts.AddPattern(
      URLPattern(URLPattern::SCHEME_ALL, "chrome://thumb/"));
  PermissionSet permissions(APIPermissionSet(), ManifestPermissionSet(),
                            allowed_hosts, URLPatternSet());
  PermissionMessageProvider::Get()->GetPermissionMessages(
      PermissionMessageProvider::Get()->GetAllPermissionIDs(
          permissions, Manifest::TYPE_EXTENSION));
}

TEST(PermissionsTest, IsPrivilegeIncrease_DeclarativeWebRequest) {
  scoped_refptr<Extension> extension(
      LoadManifest("permissions", "permissions_all_urls.json"));
  const PermissionSet& permissions =
      extension->permissions_data()->active_permissions();

  scoped_refptr<Extension> extension_dwr(
      LoadManifest("permissions", "web_request_all_host_permissions.json"));
  const PermissionSet& permissions_dwr =
      extension_dwr->permissions_data()->active_permissions();

  EXPECT_FALSE(PermissionMessageProvider::Get()->IsPrivilegeIncrease(
      permissions, permissions_dwr, extension->GetType()));
}

}  // namespace extensions
