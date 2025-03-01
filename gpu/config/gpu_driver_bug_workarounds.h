// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_CONFIG_GPU_DRIVER_BUG_WORKAROUNDS_H_
#define GPU_CONFIG_GPU_DRIVER_BUG_WORKAROUNDS_H_

#include <vector>

#include "base/macros.h"
#include "build/build_config.h"
#include "gpu/config/gpu_driver_bug_workaround_type.h"
#include "gpu/gpu_export.h"

// Forwardly declare a few GL types to avoid including GL header files.
typedef int GLint;

namespace base {
class CommandLine;
}

namespace gpu {

class GPU_EXPORT GpuDriverBugWorkarounds {
 public:
  GpuDriverBugWorkarounds();
  explicit GpuDriverBugWorkarounds(const std::vector<int>&);
  explicit GpuDriverBugWorkarounds(const base::CommandLine* command_line);

  GpuDriverBugWorkarounds(const GpuDriverBugWorkarounds& other);

  ~GpuDriverBugWorkarounds();

#define GPU_OP(type, name) bool name = false;
  GPU_DRIVER_BUG_WORKAROUNDS(GPU_OP)
#undef GPU_OP

  // Note: 0 here means use driver limit.
  GLint max_texture_size = 0;
  GLint max_fragment_uniform_vectors = 0;
  GLint max_varying_vectors = 0;
  GLint max_vertex_uniform_vectors = 0;
  GLint max_copy_texture_chromium_size = 0;
};

}  // namespace gpu

#endif  // GPU_CONFIG_GPU_DRIVER_BUG_WORKAROUNDS_H_
