// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines all the "media" command-line switches.

#ifndef MEDIA_BASE_MEDIA_SWITCHES_H_
#define MEDIA_BASE_MEDIA_SWITCHES_H_

#include "base/feature_list.h"
#include "build/build_config.h"
#include "media/base/media_export.h"
#include "media/media_features.h"
#include "ppapi/features/features.h"

namespace base {
class CommandLine;
}

namespace switches {

MEDIA_EXPORT extern const char kAudioBufferSize[];

MEDIA_EXPORT extern const char kAutoplayPolicy[];

MEDIA_EXPORT extern const char kVideoThreads[];

MEDIA_EXPORT extern const char kEnableMediaSuspend[];
MEDIA_EXPORT extern const char kDisableMediaSuspend[];

MEDIA_EXPORT extern const char kReportVp9AsAnUnsupportedMimeType[];

#if defined(OS_LINUX) || defined(OS_FREEBSD) || defined(OS_SOLARIS)
MEDIA_EXPORT extern const char kAlsaInputDevice[];
MEDIA_EXPORT extern const char kAlsaOutputDevice[];
#endif

#if defined(OS_WIN)
MEDIA_EXPORT extern const char kEnableExclusiveAudio[];
MEDIA_EXPORT extern const char kForceMediaFoundationVideoCapture[];
MEDIA_EXPORT extern const char kForceWaveAudio[];
MEDIA_EXPORT extern const char kTrySupportedChannelLayouts[];
MEDIA_EXPORT extern const char kWaveOutBuffers[];
#endif

#if defined(USE_CRAS)
MEDIA_EXPORT extern const char kUseCras[];
#endif

MEDIA_EXPORT extern const char
    kUnsafelyAllowProtectedMediaIdentifierForDomain[];

#if !defined(OS_ANDROID) || BUILDFLAG(ENABLE_PLUGINS)
MEDIA_EXPORT extern const char kEnableAudioFocus[];
#endif  // !defined(OS_ANDROID) || BUILDFLAG(ENABLE_PLUGINS)

#if BUILDFLAG(ENABLE_PLUGINS)
MEDIA_EXPORT extern const char kEnableAudioFocusDuckFlash[];
#endif  // BUILDFLAG(ENABLE_PLUGINS)

#if BUILDFLAG(ENABLE_RUNTIME_MEDIA_RENDERER_SELECTION)
MEDIA_EXPORT extern const char kDisableMojoRenderer[];
#endif  // BUILDFLAG(ENABLE_RUNTIME_MEDIA_RENDERER_SELECTION)

MEDIA_EXPORT extern const char kUseFakeDeviceForMediaStream[];
MEDIA_EXPORT extern const char kUseFileForFakeVideoCapture[];
MEDIA_EXPORT extern const char kUseFileForFakeAudioCapture[];
MEDIA_EXPORT extern const char kUseFakeJpegDecodeAccelerator[];

MEDIA_EXPORT extern const char kEnableInbandTextTracks[];

MEDIA_EXPORT extern const char kRequireAudioHardwareForTesting[];

MEDIA_EXPORT extern const char kVideoUnderflowThresholdMs[];

MEDIA_EXPORT extern const char kDisableRTCSmoothnessAlgorithm[];

MEDIA_EXPORT extern const char kForceVideoOverlays[];

MEDIA_EXPORT extern const char kMSEAudioBufferSizeLimit[];
MEDIA_EXPORT extern const char kMSEVideoBufferSizeLimit[];

MEDIA_EXPORT extern const char kIgnoreAutoplayRestrictionsForTests[];

#if !defined(OS_ANDROID)
MEDIA_EXPORT extern const char kEnableInternalMediaSession[];
#endif  // !defined(OS_ANDROID)

namespace autoplay {

MEDIA_EXPORT extern const char kDocumentUserActivationRequiredPolicy[];
MEDIA_EXPORT extern const char kNoUserGestureRequiredPolicy[];
MEDIA_EXPORT extern const char kUserGestureRequiredPolicy[];
MEDIA_EXPORT extern const char kUserGestureRequiredForCrossOriginPolicy[];

}  // namespace autoplay

}  // namespace switches

namespace media {

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.

MEDIA_EXPORT extern const base::Feature kBackgroundVideoPauseOptimization;
MEDIA_EXPORT extern const base::Feature kBackgroundVideoTrackOptimization;
MEDIA_EXPORT extern const base::Feature kComplexityBasedVideoBuffering;
MEDIA_EXPORT extern const base::Feature kExternalClearKeyForTesting;
MEDIA_EXPORT extern const base::Feature kLowDelayVideoRenderingOnLiveStream;
MEDIA_EXPORT extern const base::Feature kMediaCastOverlayButton;
MEDIA_EXPORT extern const base::Feature kRecordMediaEngagementScores;
MEDIA_EXPORT extern const base::Feature kMediaEngagementBypassAutoplayPolicies;
MEDIA_EXPORT extern const base::Feature kMemoryPressureBasedSourceBufferGC;
MEDIA_EXPORT extern const base::Feature kMojoCdm;
MEDIA_EXPORT extern const base::Feature kNewAudioRenderingMixingStrategy;
MEDIA_EXPORT extern const base::Feature kNewRemotePlaybackPipeline;
MEDIA_EXPORT extern const base::Feature kOverlayFullscreenVideo;
MEDIA_EXPORT extern const base::Feature kResumeBackgroundVideo;
MEDIA_EXPORT extern const base::Feature kSpecCompliantCanPlayThrough;
MEDIA_EXPORT extern const base::Feature kSupportExperimentalCdmInterface;
MEDIA_EXPORT extern const base::Feature kUseAndroidOverlay;
MEDIA_EXPORT extern const base::Feature kUseNewMediaCache;
MEDIA_EXPORT extern const base::Feature kVideoBlitColorAccuracy;
MEDIA_EXPORT extern const base::Feature kVideoColorManagement;
MEDIA_EXPORT extern const base::Feature kUseSurfaceLayerForVideo;

#if defined(OS_ANDROID)
MEDIA_EXPORT extern const base::Feature kVideoFullscreenOrientationLock;
MEDIA_EXPORT extern const base::Feature kVideoRotateToFullscreen;
MEDIA_EXPORT extern const base::Feature kMediaDrmPersistentLicense;
#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)
MEDIA_EXPORT extern const base::Feature kD3D11VideoDecoding;
MEDIA_EXPORT extern const base::Feature kDelayCopyNV12Textures;
MEDIA_EXPORT extern const base::Feature kMediaFoundationH264Encoding;
#endif  // defined(OS_WIN)

// Based on a |command_line| and the current platform, returns the effective
// autoplay policy. In other words, it will take into account the default policy
// if none is specified via the command line and options passed for testing.
// Returns one of the possible autoplay policy switches from the
// switches::autoplay namespace.
MEDIA_EXPORT std::string GetEffectiveAutoplayPolicy(
    const base::CommandLine& command_line);

}  // namespace media

#endif  // MEDIA_BASE_MEDIA_SWITCHES_H_
