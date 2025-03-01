// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/config/gpu_switches.h"

namespace switches {

// Disable workarounds for various GPU driver bugs.
const char kDisableGpuDriverBugWorkarounds[] =
    "disable-gpu-driver-bug-workarounds";

// Disable GPU rasterization, i.e. rasterize on the CPU only.
// Overrides the kEnableGpuRasterization and kForceGpuRasterization flags.
const char kDisableGpuRasterization[] = "disable-gpu-rasterization";

// Allow heuristics to determine when a layer tile should be drawn with the
// Skia GPU backend. Only valid with GPU accelerated compositing +
// impl-side painting.
const char kEnableGpuRasterization[] = "enable-gpu-rasterization";

// Passes active gpu vendor id from browser process to GPU process.
const char kGpuActiveVendorID[] = "gpu-active-vendor-id";

// Passes active gpu device id from browser process to GPU process.
const char kGpuActiveDeviceID[] = "gpu-active-device-id";

// Passes gpu device_id from browser process to GPU process.
const char kGpuDeviceID[] = "gpu-device-id";

// Pass a set of GpuDriverBugWorkaroundType ids, seperated by ','.
const char kGpuDriverBugWorkarounds[] = "gpu-driver-bug-workarounds";

// Passes gpu driver_vendor from browser process to GPU process.
const char kGpuDriverVendor[] = "gpu-driver-vendor";

// Passes gpu driver_version from browser process to GPU process.
const char kGpuDriverVersion[] = "gpu-driver-version";

// Passes gpu driver_date from browser process to GPU process.
const char kGpuDriverDate[] = "gpu-driver-date";

// Passes secondary gpu vendor ids from browser process to GPU process.
const char kGpuSecondaryVendorIDs[] = "gpu-secondary-vendor-ids";

// Passes secondary gpu device ids from browser process to GPU process.
const char kGpuSecondaryDeviceIDs[] = "gpu-secondary-device-ids";

// Testing switch to not launch the gpu process for full gpu info collection.
const char kGpuTestingNoCompleteInfoCollection[] =
    "gpu-no-complete-info-collection";

// Override os version from GpuControlList::MakeDecision.
const char kGpuTestingOsVersion[] = "gpu-testing-os-version";

// Override gpu vendor id from the GpuInfoCollector.
const char kGpuTestingVendorId[] = "gpu-testing-vendor-id";

// Override gpu device id from the GpuInfoCollector.
const char kGpuTestingDeviceId[] = "gpu-testing-device-id";

// Override secondary gpu vendor ids from the GpuInfoCollector.
const char kGpuTestingSecondaryVendorIDs[] = "gpu-testing-secondary-vendor-ids";

// Override secondary gpu device ids from the GpuInfoCollector.
const char kGpuTestingSecondaryDeviceIDs[] = "gpu-testing-secondary-device-ids";

// Override gpu driver date from the GpuInfoCollector.
const char kGpuTestingDriverDate[] = "gpu-testing-driver-date";

// Override gl vendor from the GpuInfoCollector.
const char kGpuTestingGLVendor[] = "gpu-testing-gl-vendor";

// Override gl renderer from the GpuInfoCollector.
const char kGpuTestingGLRenderer[] = "gpu-testing-gl-renderer";

// Override gl version from the GpuInfoCollector.
const char kGpuTestingGLVersion[] = "gpu-testing-gl-version";

// Passes gpu vendor_id from browser process to GPU process.
const char kGpuVendorID[] = "gpu-vendor-id";

// Ignores GPU blacklist.
const char kIgnoreGpuBlacklist[] = "ignore-gpu-blacklist";

}  // namespace switches
