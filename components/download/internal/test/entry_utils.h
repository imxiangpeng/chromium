// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DOWNLOAD_INTERNAL_TEST_ENTRY_UTILS_H_
#define COMPONENTS_DOWNLOAD_INTERNAL_TEST_ENTRY_UTILS_H_

#include <string>
#include <vector>

#include "components/download/internal/entry.h"

namespace download {
namespace test {

bool CompareEntry(const Entry* const& expected, const Entry* const& actual);

bool CompareEntryList(const std::vector<Entry*>& a,
                      const std::vector<Entry*>& b);

bool CompareEntryList(const std::vector<Entry>& list1,
                      const std::vector<Entry>& list2);

Entry BuildBasicEntry();

Entry BuildBasicEntry(Entry::State state);

Entry BuildEntry(DownloadClient client, const std::string& guid);

Entry BuildEntry(DownloadClient client,
                 const std::string& guid,
                 base::Time cancel_time,
                 SchedulingParams::NetworkRequirements network_requirements,
                 SchedulingParams::BatteryRequirements battery_requirements,
                 SchedulingParams::Priority priority,
                 const GURL& url,
                 const std::string& request_method,
                 Entry::State state,
                 const base::FilePath& file_path,
                 base::Time create_time,
                 base::Time completion_time,
                 int attempt_count);

}  // namespace test
}  // namespace download

#endif  // COMPONENTS_DOWNLOAD_INTERNAL_TEST_ENTRY_UTILS_H_
