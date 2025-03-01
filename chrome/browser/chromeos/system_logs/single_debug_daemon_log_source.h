// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_SINGLE_DEBUG_DAEMON_LOG_SOURCE_H_
#define CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_SINGLE_DEBUG_DAEMON_LOG_SOURCE_H_

#include <stddef.h>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/feedback/system_logs/system_logs_source.h"

namespace system_logs {

// Gathers log data from a single debugd log source, via DebugDaemonClient.
class SingleDebugDaemonLogSource : public SystemLogsSource {
 public:
  enum class SupportedSource {
    // For "modetest" command.
    kModetest,

    // For "lsusb" command.
    kLsusb,
  };

  explicit SingleDebugDaemonLogSource(SupportedSource source);
  ~SingleDebugDaemonLogSource() override;

  // system_logs::SystemLogsSource:
  void Fetch(const SysLogsSourceCallback& callback) override;

 private:
  // Callback for handling response from DebugDaemonClient.
  void OnFetchComplete(const std::string& log_name,
                       const SysLogsSourceCallback& callback,
                       bool success,
                       const std::string& result) const;

  base::WeakPtrFactory<SingleDebugDaemonLogSource> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SingleDebugDaemonLogSource);
};

}  // namespace system_logs

#endif  // CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_SINGLE_DEBUG_DAEMON_LOG_SOURCE_H_
