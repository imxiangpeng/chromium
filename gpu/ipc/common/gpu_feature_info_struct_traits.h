// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_COMMON_GPU_FEATURE_INFO_STRUCT_TRAITS_H_
#define GPU_IPC_COMMON_GPU_FEATURE_INFO_STRUCT_TRAITS_H_

#include "gpu/config/gpu_feature_info.h"

namespace mojo {

template <>
struct EnumTraits<gpu::mojom::GpuFeatureStatus, gpu::GpuFeatureStatus> {
  static gpu::mojom::GpuFeatureStatus ToMojom(gpu::GpuFeatureStatus status) {
    switch (status) {
      case gpu::kGpuFeatureStatusEnabled:
        return gpu::mojom::GpuFeatureStatus::Enabled;
      case gpu::kGpuFeatureStatusBlacklisted:
        return gpu::mojom::GpuFeatureStatus::Blacklisted;
      case gpu::kGpuFeatureStatusDisabled:
        return gpu::mojom::GpuFeatureStatus::Disabled;
      case gpu::kGpuFeatureStatusUndefined:
        return gpu::mojom::GpuFeatureStatus::Undefined;
      case gpu::kGpuFeatureStatusMax:
        return gpu::mojom::GpuFeatureStatus::Max;
    }
    NOTREACHED();
    return gpu::mojom::GpuFeatureStatus::Max;
  }

  static bool FromMojom(gpu::mojom::GpuFeatureStatus input,
                        gpu::GpuFeatureStatus* out) {
    switch (input) {
      case gpu::mojom::GpuFeatureStatus::Enabled:
        *out = gpu::kGpuFeatureStatusEnabled;
        return true;
      case gpu::mojom::GpuFeatureStatus::Blacklisted:
        *out = gpu::kGpuFeatureStatusBlacklisted;
        return true;
      case gpu::mojom::GpuFeatureStatus::Disabled:
        *out = gpu::kGpuFeatureStatusDisabled;
        return true;
      case gpu::mojom::GpuFeatureStatus::Undefined:
        *out = gpu::kGpuFeatureStatusUndefined;
        return true;
      case gpu::mojom::GpuFeatureStatus::Max:
        *out = gpu::kGpuFeatureStatusMax;
        return true;
    }
    return false;
  }
};

template <>
struct StructTraits<gpu::mojom::GpuFeatureInfoDataView, gpu::GpuFeatureInfo> {
  static bool Read(gpu::mojom::GpuFeatureInfoDataView data,
                   gpu::GpuFeatureInfo* out) {
    std::vector<gpu::GpuFeatureStatus> info_status;
    if (!data.ReadStatusValues(&info_status))
      return false;
    if (info_status.size() != gpu::NUMBER_OF_GPU_FEATURE_TYPES)
      return false;
    std::copy(info_status.begin(), info_status.end(), out->status_values);
    return data.ReadEnabledGpuDriverBugWorkarounds(
               &out->enabled_gpu_driver_bug_workarounds) &&
           data.ReadDisabledExtensions(&out->disabled_extensions);
  }

  static std::vector<gpu::GpuFeatureStatus> status_values(
      const gpu::GpuFeatureInfo& info) {
    return std::vector<gpu::GpuFeatureStatus>(info.status_values,
                                              std::end(info.status_values));
  }

  static const std::vector<int>& enabled_gpu_driver_bug_workarounds(
      const gpu::GpuFeatureInfo& info) {
    return info.enabled_gpu_driver_bug_workarounds;
  }

  static const std::string& disabled_extensions(
      const gpu::GpuFeatureInfo& info) {
    return info.disabled_extensions;
  }
};

}  // namespace mojo

#endif  // GPU_IPC_COMMON_GPU_FEATURE_INFO_STRUCT_TRAITS_H_
