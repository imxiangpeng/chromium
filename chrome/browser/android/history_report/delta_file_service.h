// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_HISTORY_REPORT_DELTA_FILE_SERVICE_H_
#define CHROME_BROWSER_ANDROID_HISTORY_REPORT_DELTA_FILE_SERVICE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/threading/sequenced_worker_pool.h"

class GURL;

namespace base {
class FilePath;
}  // namespace base

namespace history_report {

class DeltaFileEntryWithData;
class DeltaFileBackend;

// Delta file for gmscore integration and history reporting.
class DeltaFileService {
 public:
  explicit DeltaFileService(const base::FilePath& dir);
  ~DeltaFileService();

  // Adds new addtion entry to delta file.
  virtual void PageAdded(const GURL& url);
  // Adds new deletion entry to delta file.
  void PageDeleted(const GURL& url);
  // Removes all delta file entries with seqno <= lower_bound.
  // Returns max seqno in delta file.
  int64_t Trim(int64_t lower_bound);
  // Removes all data from delta file and populates it with new addition
  // entries for given urls.
  bool Recreate(const std::vector<std::string>& urls);
  // Provides up to limit delta file entries with seqno > last_seq_no.
  std::unique_ptr<std::vector<DeltaFileEntryWithData>> Query(
      int64_t last_seq_no,
      int32_t limit);
  // Removes all entries from delta file.
  void Clear();

  // Dumps internal state to string.
  std::string Dump();

 private:
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::unique_ptr<DeltaFileBackend> delta_file_backend_;

  DISALLOW_COPY_AND_ASSIGN(DeltaFileService);
};

}  // namespace history_report

#endif  // CHROME_BROWSER_ANDROID_HISTORY_REPORT_DELTA_FILE_SERVICE_H_
