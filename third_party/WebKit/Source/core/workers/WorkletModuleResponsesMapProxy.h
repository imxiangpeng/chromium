// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WorkletModuleResponsesMapProxy_h
#define WorkletModuleResponsesMapProxy_h

#include "core/CoreExport.h"
#include "core/workers/WorkletModuleResponsesMap.h"
#include "platform/WebTaskRunner.h"
#include "platform/heap/Heap.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/Optional.h"

namespace blink {

class ModuleScriptCreationParams;

// WorkletModuleResponsesMapProxy serves as a proxy to talk to
// WorkletModuleResponsesMap on the main thread (outside_settings) from
// WorkletGlobalScope on the worklet context thread (inside_settings). The
// constructor and all public functions must be called on the worklet context
// thread.
class CORE_EXPORT WorkletModuleResponsesMapProxy
    : public GarbageCollectedFinalized<WorkletModuleResponsesMapProxy> {
 public:
  using Client = WorkletModuleResponsesMap::Client;

  static WorkletModuleResponsesMapProxy* Create(
      WorkletModuleResponsesMap*,
      RefPtr<WebTaskRunner> outside_settings_task_runner,
      RefPtr<WebTaskRunner> inside_settings_task_runner);

  void ReadEntry(const KURL&, Client*);
  void UpdateEntry(const KURL&, const ModuleScriptCreationParams&);
  void InvalidateEntry(const KURL&);

  DECLARE_TRACE();

 private:
  WorkletModuleResponsesMapProxy(
      WorkletModuleResponsesMap*,
      RefPtr<WebTaskRunner> outside_settings_task_runner,
      RefPtr<WebTaskRunner> inside_settings_task_runner);

  void ReadEntryOnMainThread(const KURL&, Client*);

  CrossThreadPersistent<WorkletModuleResponsesMap> module_responses_map_;
  RefPtr<WebTaskRunner> outside_settings_task_runner_;
  RefPtr<WebTaskRunner> inside_settings_task_runner_;
};

}  // namespace blink

#endif  // WorkletModuleResponsesMapProxy_h
