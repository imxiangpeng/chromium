// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/remoting/renderer_controller.h"

#include "base/callback.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/cdm_config.h"
#include "media/base/limits.h"
#include "media/base/media_util.h"
#include "media/base/test_helpers.h"
#include "media/base/video_decoder_config.h"
#include "media/remoting/fake_remoter.h"
#include "media/remoting/remoting_cdm.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace remoting {

namespace {

PipelineMetadata DefaultMetadata(VideoCodec codec) {
  PipelineMetadata data;
  data.has_audio = true;
  data.has_video = true;
  data.video_decoder_config = TestVideoConfig::Normal(codec);
  data.audio_decoder_config = TestAudioConfig::Normal();
  return data;
}

PipelineMetadata EncryptedMetadata() {
  PipelineMetadata data;
  data.has_audio = true;
  data.has_video = true;
  data.video_decoder_config = TestVideoConfig::NormalEncrypted();
  return data;
}

mojom::RemotingSinkMetadata GetDefaultSinkMetadata(bool enable) {
  mojom::RemotingSinkMetadata metadata;
  if (enable) {
    metadata.features.push_back(mojom::RemotingSinkFeature::RENDERING);
    metadata.features.push_back(mojom::RemotingSinkFeature::CONTENT_DECRYPTION);
  } else {
    metadata.features.clear();
  }
  metadata.video_capabilities.push_back(
      mojom::RemotingSinkVideoCapability::CODEC_VP8);
  metadata.audio_capabilities.push_back(
      mojom::RemotingSinkAudioCapability::CODEC_BASELINE_SET);
  return metadata;
}

}  // namespace

class RendererControllerTest : public ::testing::Test,
                               public MediaObserverClient {
 public:
  RendererControllerTest() {}
  ~RendererControllerTest() override {}

  void TearDown() final { RunUntilIdle(); }

  static void RunUntilIdle() { base::RunLoop().RunUntilIdle(); }

  // MediaObserverClient implementation.
  void SwitchRenderer(bool disable_pipeline_auto_suspend) override {
    is_rendering_remotely_ = disable_pipeline_auto_suspend;
    disable_pipeline_suspend_ = disable_pipeline_auto_suspend;
  }

  void ActivateViewportIntersectionMonitoring(bool activate) override {
    activate_viewport_intersection_monitoring_ = activate;
  }

  void UpdateRemotePlaybackCompatibility(bool is_compatibe) override {}

  void CreateCdm(bool is_remoting) { is_remoting_cdm_ = is_remoting; }

  void InitializeControllerAndBecomeDominant(
      const scoped_refptr<SharedSession> shared_session,
      const PipelineMetadata& pipeline_metadata,
      const mojom::RemotingSinkMetadata& sink_metadata) {
    EXPECT_FALSE(is_rendering_remotely_);
    controller_ = base::MakeUnique<RendererController>(shared_session);
    controller_->SetClient(this);
    RunUntilIdle();
    EXPECT_FALSE(is_rendering_remotely_);
    EXPECT_FALSE(activate_viewport_intersection_monitoring_);
    EXPECT_FALSE(disable_pipeline_suspend_);
    shared_session->OnSinkAvailable(sink_metadata.Clone());
    RunUntilIdle();
    EXPECT_FALSE(is_rendering_remotely_);
    EXPECT_FALSE(disable_pipeline_suspend_);
    controller_->OnRemotePlaybackDisabled(false);
    RunUntilIdle();
    EXPECT_FALSE(is_rendering_remotely_);
    EXPECT_FALSE(disable_pipeline_suspend_);
    controller_->OnMetadataChanged(pipeline_metadata);
    RunUntilIdle();
    EXPECT_FALSE(is_rendering_remotely_);
    EXPECT_FALSE(disable_pipeline_suspend_);
    controller_->OnBecameDominantVisibleContent(true);
    RunUntilIdle();
    EXPECT_FALSE(is_rendering_remotely_);
    EXPECT_FALSE(disable_pipeline_suspend_);
    controller_->OnPlaying();
    RunUntilIdle();
  }

  bool IsInDelayedStart() {
    return controller_->delayed_start_stability_timer_.IsRunning();
  }

  void DelayedStartEnds() {
    EXPECT_TRUE(IsInDelayedStart());
    const base::Closure callback =
        controller_->delayed_start_stability_timer_.user_task();
    callback.Run();
    controller_->delayed_start_stability_timer_.Stop();
  }

  base::MessageLoop message_loop_;

 protected:
  std::unique_ptr<RendererController> controller_;
  bool is_rendering_remotely_ = false;
  bool is_remoting_cdm_ = false;
  bool activate_viewport_intersection_monitoring_ = false;
  bool disable_pipeline_suspend_ = false;

 private:
  DISALLOW_COPY_AND_ASSIGN(RendererControllerTest);
};

TEST_F(RendererControllerTest, ToggleRendererOnDominantChange) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecVP8),
                                        GetDefaultSinkMetadata(true));
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(disable_pipeline_suspend_);

  // Leaving fullscreen should shut down remoting.
  controller_->OnBecameDominantVisibleContent(false);
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
}

TEST_F(RendererControllerTest, ToggleRendererOnSinkCapabilities) {
  EXPECT_FALSE(is_rendering_remotely_);
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecVP8),
                                        GetDefaultSinkMetadata(false));
  // An available sink that does not support remote rendering should not cause
  // the controller to toggle remote rendering on.
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  shared_session->OnSinkGone();  // Bye-bye useless sink!
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  // A sink that *does* support remote rendering *does* cause the controller to
  // toggle remote rendering on.
  shared_session->OnSinkAvailable(GetDefaultSinkMetadata(true).Clone());
  RunUntilIdle();
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(is_rendering_remotely_);
  controller_->OnBecameDominantVisibleContent(true);
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);
  EXPECT_TRUE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, ToggleRendererOnDisableChange) {
  EXPECT_FALSE(is_rendering_remotely_);
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecVP8),
                                        GetDefaultSinkMetadata(true));
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(disable_pipeline_suspend_);

  // If the page disables remote playback (e.g., by setting the
  // disableRemotePlayback attribute), this should shut down remoting.
  controller_->OnRemotePlaybackDisabled(true);
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, WithVP9VideoCodec) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecVP9),
                                        GetDefaultSinkMetadata(true));
  // An available sink that does not support VP9 video codec should not cause
  // the controller to toggle remote rendering on.
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);

  shared_session->OnSinkGone();  // Bye-bye useless sink!
  mojom::RemotingSinkMetadata sink_metadata = GetDefaultSinkMetadata(true);
  sink_metadata.video_capabilities.push_back(
      mojom::RemotingSinkVideoCapability::CODEC_VP9);
  // A sink that *does* support VP9 video codec *does* cause the controller to
  // toggle remote rendering on.
  shared_session->OnSinkAvailable(sink_metadata.Clone());
  RunUntilIdle();
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_TRUE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, WithHEVCVideoCodec) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecHEVC),
                                        GetDefaultSinkMetadata(true));
  // An available sink that does not support HEVC video codec should not cause
  // the controller to toggle remote rendering on.
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(disable_pipeline_suspend_);

  shared_session->OnSinkGone();  // Bye-bye useless sink!
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  mojom::RemotingSinkMetadata sink_metadata = GetDefaultSinkMetadata(true);
  sink_metadata.video_capabilities.push_back(
      mojom::RemotingSinkVideoCapability::CODEC_HEVC);
  // A sink that *does* support HEVC video codec *does* cause the controller to
  // toggle remote rendering on.
  shared_session->OnSinkAvailable(sink_metadata.Clone());
  RunUntilIdle();
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_TRUE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, WithAACAudioCodec) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  const AudioDecoderConfig audio_config = AudioDecoderConfig(
      AudioCodec::kCodecAAC, kSampleFormatPlanarF32, CHANNEL_LAYOUT_STEREO,
      44100, EmptyExtraData(), Unencrypted());
  PipelineMetadata pipeline_metadata = DefaultMetadata(VideoCodec::kCodecVP8);
  pipeline_metadata.audio_decoder_config = audio_config;
  InitializeControllerAndBecomeDominant(shared_session, pipeline_metadata,
                                        GetDefaultSinkMetadata(true));
  // An available sink that does not support AAC audio codec should not cause
  // the controller to toggle remote rendering on.
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);

  shared_session->OnSinkGone();  // Bye-bye useless sink!
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
  mojom::RemotingSinkMetadata sink_metadata = GetDefaultSinkMetadata(true);
  sink_metadata.audio_capabilities.push_back(
      mojom::RemotingSinkAudioCapability::CODEC_AAC);
  // A sink that *does* support AAC audio codec *does* cause the controller to
  // toggle remote rendering on.
  shared_session->OnSinkAvailable(sink_metadata.Clone());
  RunUntilIdle();
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_TRUE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, WithOpusAudioCodec) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  const AudioDecoderConfig audio_config = AudioDecoderConfig(
      AudioCodec::kCodecOpus, kSampleFormatPlanarF32, CHANNEL_LAYOUT_STEREO,
      44100, EmptyExtraData(), Unencrypted());
  PipelineMetadata pipeline_metadata = DefaultMetadata(VideoCodec::kCodecVP8);
  pipeline_metadata.audio_decoder_config = audio_config;
  InitializeControllerAndBecomeDominant(shared_session, pipeline_metadata,
                                        GetDefaultSinkMetadata(true));
  // An available sink that does not support Opus audio codec should not cause
  // the controller to toggle remote rendering on.
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(activate_viewport_intersection_monitoring_);
  EXPECT_FALSE(disable_pipeline_suspend_);

  shared_session->OnSinkGone();  // Bye-bye useless sink!
  RunUntilIdle();
  mojom::RemotingSinkMetadata sink_metadata = GetDefaultSinkMetadata(true);
  sink_metadata.audio_capabilities.push_back(
      mojom::RemotingSinkAudioCapability::CODEC_OPUS);
  // A sink that *does* support Opus audio codec *does* cause the controller to
  // toggle remote rendering on.
  shared_session->OnSinkAvailable(sink_metadata.Clone());
  RunUntilIdle();
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);  // All requirements now satisfied.
  EXPECT_TRUE(activate_viewport_intersection_monitoring_);
  EXPECT_TRUE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, StartFailed) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(true);
  InitializeControllerAndBecomeDominant(shared_session,
                                        DefaultMetadata(VideoCodec::kCodecVP8),
                                        GetDefaultSinkMetadata(true));
  RunUntilIdle();
  EXPECT_TRUE(IsInDelayedStart());
  DelayedStartEnds();
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(disable_pipeline_suspend_);
}

TEST_F(RendererControllerTest, EncryptedWithRemotingCdm) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session, EncryptedMetadata(),
                                        GetDefaultSinkMetadata(true));
  EXPECT_FALSE(is_rendering_remotely_);

  const scoped_refptr<SharedSession> cdm_shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  std::unique_ptr<RemotingCdmController> cdm_controller =
      base::MakeUnique<RemotingCdmController>(cdm_shared_session);
  cdm_shared_session->OnSinkAvailable(GetDefaultSinkMetadata(true).Clone());
  cdm_controller->ShouldCreateRemotingCdm(
      base::Bind(&RendererControllerTest::CreateCdm, base::Unretained(this)));
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_TRUE(is_remoting_cdm_);

  // Create a RemotingCdm with |cdm_controller|.
  const scoped_refptr<RemotingCdm> remoting_cdm = new RemotingCdm(
      std::string(), GURL(), CdmConfig(), SessionMessageCB(), SessionClosedCB(),
      SessionKeysChangeCB(), SessionExpirationUpdateCB(), CdmCreatedCB(),
      std::move(cdm_controller));
  std::unique_ptr<RemotingCdmContext> remoting_cdm_context =
      base::MakeUnique<RemotingCdmContext>(remoting_cdm.get());
  controller_->OnSetCdm(remoting_cdm_context.get());
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);

  // For encrypted contents, becoming/exiting dominant has no effect.
  controller_->OnBecameDominantVisibleContent(true);
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);
  EXPECT_FALSE(IsInDelayedStart());
  controller_->OnBecameDominantVisibleContent(false);
  RunUntilIdle();
  EXPECT_TRUE(is_rendering_remotely_);
  EXPECT_FALSE(IsInDelayedStart());

  EXPECT_NE(SharedSession::SESSION_PERMANENTLY_STOPPED,
            controller_->session()->state());
  cdm_shared_session->OnSinkGone();
  RunUntilIdle();
  EXPECT_EQ(SharedSession::SESSION_PERMANENTLY_STOPPED,
            controller_->session()->state());
  // Don't switch renderer in this case. Still using the remoting renderer to
  // show the failure interstitial.
  EXPECT_TRUE(is_rendering_remotely_);
}

TEST_F(RendererControllerTest, EncryptedWithLocalCdm) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session, EncryptedMetadata(),
                                        GetDefaultSinkMetadata(true));
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(IsInDelayedStart());

  const scoped_refptr<SharedSession> cdm_shared_session =
      FakeRemoterFactory::CreateSharedSession(true);
  std::unique_ptr<RemotingCdmController> cdm_controller =
      base::MakeUnique<RemotingCdmController>(cdm_shared_session);
  cdm_shared_session->OnSinkAvailable(GetDefaultSinkMetadata(true).Clone());
  cdm_controller->ShouldCreateRemotingCdm(
      base::Bind(&RendererControllerTest::CreateCdm, base::Unretained(this)));
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(is_remoting_cdm_);
  EXPECT_FALSE(IsInDelayedStart());
}

TEST_F(RendererControllerTest, EncryptedWithFailedRemotingCdm) {
  const scoped_refptr<SharedSession> shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  InitializeControllerAndBecomeDominant(shared_session, EncryptedMetadata(),
                                        GetDefaultSinkMetadata(true));
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_FALSE(IsInDelayedStart());

  const scoped_refptr<SharedSession> cdm_shared_session =
      FakeRemoterFactory::CreateSharedSession(false);
  std::unique_ptr<RemotingCdmController> cdm_controller =
      base::MakeUnique<RemotingCdmController>(cdm_shared_session);
  cdm_shared_session->OnSinkAvailable(GetDefaultSinkMetadata(true).Clone());
  cdm_controller->ShouldCreateRemotingCdm(
      base::Bind(&RendererControllerTest::CreateCdm, base::Unretained(this)));
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_TRUE(is_remoting_cdm_);
  EXPECT_FALSE(IsInDelayedStart());

  cdm_shared_session->OnSinkGone();
  RunUntilIdle();
  EXPECT_FALSE(is_rendering_remotely_);
  EXPECT_NE(SharedSession::SESSION_PERMANENTLY_STOPPED,
            controller_->session()->state());

  const scoped_refptr<RemotingCdm> remoting_cdm = new RemotingCdm(
      std::string(), GURL(), CdmConfig(), SessionMessageCB(), SessionClosedCB(),
      SessionKeysChangeCB(), SessionExpirationUpdateCB(), CdmCreatedCB(),
      std::move(cdm_controller));
  std::unique_ptr<RemotingCdmContext> remoting_cdm_context =
      base::MakeUnique<RemotingCdmContext>(remoting_cdm.get());
  controller_->OnSetCdm(remoting_cdm_context.get());
  RunUntilIdle();
  // Switch to using the remoting renderer, even when the remoting CDM session
  // was already terminated, to show the failure interstitial.
  EXPECT_TRUE(is_rendering_remotely_);
  EXPECT_FALSE(IsInDelayedStart());
  EXPECT_EQ(SharedSession::SESSION_PERMANENTLY_STOPPED,
            controller_->session()->state());
}

}  // namespace remoting
}  // namespace media
