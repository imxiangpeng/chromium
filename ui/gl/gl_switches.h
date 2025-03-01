// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GL_GL_SWITCHES_H_
#define UI_GL_GL_SWITCHES_H_

// Defines all the command-line switches used by ui/gl.

#include "base/feature_list.h"
#include "ui/gl/gl_export.h"

namespace gl {

// The GL implementation names that can be passed to --use-gl.
GL_EXPORT extern const char kGLImplementationDesktopName[];
GL_EXPORT extern const char kGLImplementationOSMesaName[];
GL_EXPORT extern const char kGLImplementationAppleName[];
GL_EXPORT extern const char kGLImplementationEGLName[];
GL_EXPORT extern const char kGLImplementationANGLEName[];
GL_EXPORT extern const char kGLImplementationSwiftShaderName[];
GL_EXPORT extern const char kGLImplementationSwiftShaderForWebGLName[];
extern const char kGLImplementationMockName[];
extern const char kGLImplementationStubName[];

GL_EXPORT extern const char kANGLEImplementationDefaultName[];
GL_EXPORT extern const char kANGLEImplementationD3D9Name[];
GL_EXPORT extern const char kANGLEImplementationD3D11Name[];
GL_EXPORT extern const char kANGLEImplementationOpenGLName[];
GL_EXPORT extern const char kANGLEImplementationOpenGLESName[];
GL_EXPORT extern const char kANGLEImplementationNullName[];

}  // namespace gl

namespace switches {

GL_EXPORT extern const char kCreateDefaultGLContext[];
GL_EXPORT extern const char kDisableD3D11[];
GL_EXPORT extern const char kDisableES3GLContext[];
GL_EXPORT extern const char kDisableGpuVsync[];
GL_EXPORT extern const char kEnableGPUServiceLogging[];
GL_EXPORT extern const char kEnableGPUServiceTracing[];
GL_EXPORT extern const char kGpuNoContextLost[];
GL_EXPORT extern const char kDisableDirectComposition[];

GL_EXPORT extern const char kSupportsDualGpus[];

GL_EXPORT extern const char kUseANGLE[];
GL_EXPORT extern const char kUseGL[];
GL_EXPORT extern const char kTestGLLib[];
GL_EXPORT extern const char kUseGpuInTests[];
GL_EXPORT extern const char kEnableES3APIs[];
GL_EXPORT extern const char kDisableES3APIs[];
GL_EXPORT extern const char kEnableSgiVideoSync[];
GL_EXPORT extern const char kDisableGLExtensions[];
GL_EXPORT extern const char kEnableSwapBuffersWithBounds[];
GL_EXPORT extern const char kEnableDirectCompositionLayers[];
GL_EXPORT extern const char kDisableDirectCompositionLayers[];

GL_EXPORT extern const char kUsePassthroughCmdDecoder[];

// These flags are used by the test harness code, not passed in by users.
GL_EXPORT extern const char kDisableGLDrawingForTests[];
GL_EXPORT extern const char kOverrideUseSoftwareGLForTests[];

GL_EXPORT extern const char* const kGLSwitchesCopiedFromGpuProcessHost[];
GL_EXPORT extern const int kGLSwitchesCopiedFromGpuProcessHostNumSwitches;

}  // namespace switches

namespace features {

#if defined(OS_WIN)
GL_EXPORT extern const base::Feature kD3DVsync;
#endif  // defined(OS_WIN)

GL_EXPORT extern const base::Feature kDirectCompositionUnderlays;
GL_EXPORT extern const base::Feature kDirectCompositionComplexOverlays;

}  // namespace features

#endif  // UI_GL_GL_SWITCHES_H_
