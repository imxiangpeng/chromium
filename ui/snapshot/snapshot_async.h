// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_SNAPSHOT_SNAPSHOT_ASYNC_H_
#define UI_SNAPSHOT_SNAPSHOT_ASYNC_H_

#include <memory>

#include "base/macros.h"
#include "components/viz/common/quads/copy_output_result.h"
#include "ui/snapshot/snapshot.h"

namespace base {
class TaskRunner;
}

namespace gfx {
class Size;
}

namespace ui {

// Helper methods for async snapshots to convert a viz::CopyOutputResult into a
// ui::GrabWindowSnapshot callback.
class SnapshotAsync {
 public:
  static void ScaleCopyOutputResult(
      const GrabWindowSnapshotAsyncCallback& callback,
      const gfx::Size& target_size,
      scoped_refptr<base::TaskRunner> background_task_runner,
      std::unique_ptr<viz::CopyOutputResult> result);

  static void RunCallbackWithCopyOutputResult(
      const GrabWindowSnapshotAsyncCallback& callback,
      std::unique_ptr<viz::CopyOutputResult> result);

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SnapshotAsync);
};

}  // namespace ui

#endif  // UI_SNAPSHOT_SNAPSHOT_ASYNC_H_
