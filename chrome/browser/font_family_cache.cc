// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/font_family_cache.h"

#include <stddef.h>

#include <map>

#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_font_webkit_names.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/notification_source.h"

// Identifies the user data on the profile.
const char kFontFamilyCacheKey[] = "FontFamilyCacheKey";

FontFamilyCache::FontFamilyCache(Profile* profile)
    : prefs_(profile->GetPrefs()) {
  profile_pref_registrar_.Init(profile->GetPrefs());
  notification_registrar_.Add(this,
                              chrome::NOTIFICATION_PROFILE_DESTROYED,
                              content::Source<Profile>(profile));
}

FontFamilyCache::~FontFamilyCache() {
}

void FontFamilyCache::FillFontFamilyMap(Profile* profile,
                                        const char* map_name,
                                        content::ScriptFontFamilyMap* map) {
  FontFamilyCache* cache =
      static_cast<FontFamilyCache*>(profile->GetUserData(&kFontFamilyCacheKey));
  if (!cache) {
    cache = new FontFamilyCache(profile);
    profile->SetUserData(&kFontFamilyCacheKey, base::WrapUnique(cache));
  }

  cache->FillFontFamilyMap(map_name, map);
}

void FontFamilyCache::FillFontFamilyMap(const char* map_name,
                                        content::ScriptFontFamilyMap* map) {
  // TODO(falken): Get rid of the brute-force scan over possible
  // (font family / script) combinations - see http://crbug.com/308095.
  for (size_t i = 0; i < prefs::kWebKitScriptsForFontFamilyMapsLength; ++i) {
    const char* script = prefs::kWebKitScriptsForFontFamilyMaps[i];
    base::string16 result = FetchAndCacheFont(script, map_name);
    if (!result.empty())
      (*map)[script] = result;
  }
}

base::string16 FontFamilyCache::FetchFont(const char* script,
                                          const char* map_name) {
  std::string pref_name = base::StringPrintf("%s.%s", map_name, script);
  std::string font = prefs_->GetString(pref_name.c_str());
  base::string16 font16 = base::UTF8ToUTF16(font);

  // Lazily constructs the map if it doesn't already exist.
  ScriptFontMap& map = font_family_map_[map_name];
  map[script] = font16;

  // Register for profile preference changes.
  profile_pref_registrar_.Add(
      pref_name.c_str(),
      base::Bind(&FontFamilyCache::OnPrefsChanged, base::Unretained(this)));
  return font16;
}

base::string16 FontFamilyCache::FetchAndCacheFont(const char* script,
                                                  const char* map_name) {
  FontFamilyMap::const_iterator it = font_family_map_.find(map_name);
  if (it != font_family_map_.end()) {
    ScriptFontMap::const_iterator it2 = it->second.find(script);
    if (it2 != it->second.end())
      return it2->second;
  }

  return FetchFont(script, map_name);
}

// There are ~1000 entries in the cache. Avoid unnecessary object construction,
// including std::string.
void FontFamilyCache::OnPrefsChanged(const std::string& pref_name) {
  const size_t delimiter_length = 1;
  const char delimiter = '.';
  for (auto& it : font_family_map_) {
    const char* map_name = it.first;
    size_t map_name_length = strlen(map_name);

    // If the map name doesn't match, move on.
    if (pref_name.compare(0, map_name_length, map_name) != 0)
      continue;

    ScriptFontMap& map = it.second;
    for (ScriptFontMap::iterator it2 = map.begin(); it2 != map.end(); ++it2) {
      const char* script = it2->first;
      size_t script_length = strlen(script);

      // If the length doesn't match, move on.
      if (pref_name.size() !=
          map_name_length + script_length + delimiter_length)
        continue;

      // If the script doesn't match, move on.
      if (pref_name.compare(
              map_name_length + delimiter_length, script_length, script) != 0)
        continue;

      // If the delimiter doesn't match, move on.
      if (pref_name[map_name_length] != delimiter)
        continue;

      // Clear the cache and the observer.
      map.erase(it2);
      profile_pref_registrar_.Remove(pref_name.c_str());
      break;
    }
  }
}

void FontFamilyCache::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_PROFILE_DESTROYED, type);
  profile_pref_registrar_.RemoveAll();
}
