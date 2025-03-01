// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BLINK_WEBMEDIAPLAYER_IMPL_H_
#define MEDIA_BLINK_WEBMEDIAPLAYER_IMPL_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/cancelable_callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/threading/thread.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "media/base/media_observer.h"
#include "media/base/media_tracks.h"
#include "media/base/overlay_info.h"
#include "media/base/pipeline_impl.h"
#include "media/base/renderer_factory_selector.h"
#include "media/base/surface_manager.h"
#include "media/base/text_track.h"
#include "media/blink/buffered_data_source_host_impl.h"
#include "media/blink/media_blink_export.h"
#include "media/blink/multibuffer_data_source.h"
#include "media/blink/video_frame_compositor.h"
#include "media/blink/webmediaplayer_delegate.h"
#include "media/blink/webmediaplayer_params.h"
#include "media/blink/webmediaplayer_util.h"
#include "media/filters/pipeline_controller.h"
#include "media/renderers/skcanvas_video_renderer.h"
#include "third_party/WebKit/public/platform/WebAudioSourceProvider.h"
#include "third_party/WebKit/public/platform/WebContentDecryptionModuleResult.h"
#include "third_party/WebKit/public/platform/WebMediaPlayer.h"
#include "third_party/WebKit/public/platform/WebSurfaceLayerBridge.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)  // WMPI_CAST
// Delete this file when WMPI_CAST is no longer needed.
#include "media/blink/webmediaplayer_cast_android.h"
#endif

namespace blink {
class WebLocalFrame;
class WebMediaPlayerClient;
class WebMediaPlayerEncryptedMediaClient;
}

namespace base {
class SingleThreadTaskRunner;
class TaskRunner;
}

namespace cc_blink {
class WebLayerImpl;
}

namespace gpu {
namespace gles2 {
class GLES2Interface;
}
}

namespace media {
class ChunkDemuxer;
class ContentDecryptionModule;
class MediaLog;
class UrlIndex;
class VideoFrameCompositor;
class WatchTimeReporter;
class WebAudioSourceProviderImpl;
class WebMediaPlayerDelegate;

// The canonical implementation of blink::WebMediaPlayer that's backed by
// Pipeline. Handles normal resource loading, Media Source, and
// Encrypted Media.
class MEDIA_BLINK_EXPORT WebMediaPlayerImpl
    : public NON_EXPORTED_BASE(blink::WebMediaPlayer),
      public NON_EXPORTED_BASE(WebMediaPlayerDelegate::Observer),
      public NON_EXPORTED_BASE(Pipeline::Client),
      public MediaObserverClient,
      public base::SupportsWeakPtr<WebMediaPlayerImpl> {
 public:
  // Constructs a WebMediaPlayer implementation using Chromium's media stack.
  // |delegate| and |renderer_factory_selector| must not be null.
  WebMediaPlayerImpl(
      blink::WebLocalFrame* frame,
      blink::WebMediaPlayerClient* client,
      blink::WebMediaPlayerEncryptedMediaClient* encrypted_client,
      WebMediaPlayerDelegate* delegate,
      std::unique_ptr<RendererFactorySelector> renderer_factory_selector,
      UrlIndex* url_index,
      std::unique_ptr<WebMediaPlayerParams> params);
  ~WebMediaPlayerImpl() override;

  void Load(LoadType load_type,
            const blink::WebMediaPlayerSource& source,
            CORSMode cors_mode) override;

  // Playback controls.
  void Play() override;
  void Pause() override;
  bool SupportsSave() const override;
  void Seek(double seconds) override;
  void SetRate(double rate) override;
  void SetVolume(double volume) override;
  void SetSinkId(const blink::WebString& sink_id,
                 const blink::WebSecurityOrigin& security_origin,
                 blink::WebSetSinkIdCallbacks* web_callback) override;
  void SetPreload(blink::WebMediaPlayer::Preload preload) override;
  blink::WebTimeRanges Buffered() const override;
  blink::WebTimeRanges Seekable() const override;

  // paint() the current video frame into |canvas|. This is used to support
  // various APIs and functionalities, including but not limited to: <canvas>,
  // WebGL texImage2D, ImageBitmap, printing and capturing capabilities.
  void Paint(blink::WebCanvas* canvas,
             const blink::WebRect& rect,
             cc::PaintFlags& flags) override;

  // True if the loaded media has a playable video/audio track.
  bool HasVideo() const override;
  bool HasAudio() const override;

  void EnabledAudioTracksChanged(
      const blink::WebVector<blink::WebMediaPlayer::TrackId>& enabledTrackIds)
      override;
  void SelectedVideoTrackChanged(
      blink::WebMediaPlayer::TrackId* selectedTrackId) override;

  bool GetLastUploadedFrameInfo(unsigned* width,
                                unsigned* height,
                                double* timestamp) override;

  // Dimensions of the video.
  blink::WebSize NaturalSize() const override;

  blink::WebSize VisibleRect() const override;

  // Getters of playback state.
  bool Paused() const override;
  bool Seeking() const override;
  double Duration() const override;
  virtual double timelineOffset() const;
  double CurrentTime() const override;

  // Internal states of loading and network.
  // TODO(hclam): Ask the pipeline about the state rather than having reading
  // them from members which would cause race conditions.
  blink::WebMediaPlayer::NetworkState GetNetworkState() const override;
  blink::WebMediaPlayer::ReadyState GetReadyState() const override;

  blink::WebString GetErrorMessage() const override;
  bool DidLoadingProgress() override;

  bool HasSingleSecurityOrigin() const override;
  bool DidPassCORSAccessCheck() const override;

  double MediaTimeForTimeValue(double timeValue) const override;

  unsigned DecodedFrameCount() const override;
  unsigned DroppedFrameCount() const override;
  size_t AudioDecodedByteCount() const override;
  size_t VideoDecodedByteCount() const override;

  bool CopyVideoTextureToPlatformTexture(gpu::gles2::GLES2Interface* gl,
                                         unsigned int target,
                                         unsigned int texture,
                                         unsigned internal_format,
                                         unsigned format,
                                         unsigned type,
                                         int level,
                                         bool premultiply_alpha,
                                         bool flip_y) override;

  blink::WebAudioSourceProvider* GetAudioSourceProvider() override;

  void SetContentDecryptionModule(
      blink::WebContentDecryptionModule* cdm,
      blink::WebContentDecryptionModuleResult result) override;

  bool SupportsOverlayFullscreenVideo() override;
  void EnteredFullscreen() override;
  void ExitedFullscreen() override;
  void BecameDominantVisibleContent(bool isDominant) override;
  void SetIsEffectivelyFullscreen(bool isEffectivelyFullscreen) override;
  void OnHasNativeControlsChanged(bool) override;
  void OnDisplayTypeChanged(WebMediaPlayer::DisplayType) override;

  // WebMediaPlayerDelegate::Observer implementation.
  void OnFrameHidden() override;
  void OnFrameClosed() override;
  void OnFrameShown() override;
  void OnIdleTimeout() override;
  void OnPlay() override;
  void OnPause() override;
  void OnVolumeMultiplierUpdate(double multiplier) override;
  void OnBecamePersistentVideo(bool value) override;

  void RequestRemotePlaybackDisabled(bool disabled) override;
#if defined(OS_ANDROID)  // WMPI_CAST
  bool IsRemote() const override;
  void RequestRemotePlayback() override;
  void RequestRemotePlaybackControl() override;
  void RequestRemotePlaybackStop() override;

  void SetMediaPlayerManager(
      RendererMediaPlayerManagerInterface* media_player_manager);
  void OnRemotePlaybackEnded();
  void OnDisconnectedFromRemoteDevice(double t);
  void SuspendForRemote();
  void DisplayCastFrameAfterSuspend(const scoped_refptr<VideoFrame>& new_frame,
                                    PipelineStatus status);
  gfx::Size GetCanvasSize() const;
  void SetDeviceScaleFactor(float scale_factor);
  void SetUseFallbackPath(bool use_fallback_path);
  void SetPoster(const blink::WebURL& poster) override;
#endif

  // MediaObserverClient implementation.
  void SwitchRenderer(bool is_rendered_remotely) override;
  void ActivateViewportIntersectionMonitoring(bool activate) override;
  void UpdateRemotePlaybackCompatibility(bool is_compatible) override;

  // Called from WebMediaPlayerCast.
  // TODO(hubbe): WMPI_CAST make private.
  void OnPipelineSeeked(bool time_updated);

  // Distinct states that |delegate_| can be in. (Public for testing.)
  enum class DelegateState {
    GONE,
    PLAYING,
    PAUSED,
  };

  // Playback state variables computed together in UpdatePlayState().
  // (Public for testing.)
  struct PlayState {
    DelegateState delegate_state;
    bool is_idle;
    bool is_memory_reporting_enabled;
    bool is_suspended;
  };

 private:
  friend class WebMediaPlayerImplTest;
  friend class WebMediaPlayerImplBackgroundBehaviorTest;

  void EnableOverlay();
  void DisableOverlay();

  // Do we have overlay information?  For CVV, this is a surface id.  For
  // AndroidOverlay, this is the routing token.
  bool HaveOverlayInfo();

  // Send OverlayInfo to the decoder.
  //
  // If we've requested but not yet received the surface id or routing token, or
  // if there's no decoder-provided callback to send the overlay info, then this
  // call will do nothing.
  void MaybeSendOverlayInfoToDecoder();

  void OnPipelineSuspended();
  void OnBeforePipelineResume();
  void OnPipelineResumed();
  void OnDemuxerOpened();

  // Pipeline::Client overrides.
  void OnError(PipelineStatus status) override;
  void OnEnded() override;
  void OnMetadata(PipelineMetadata metadata) override;
  void OnBufferingStateChange(BufferingState state) override;
  void OnDurationChange() override;
  void OnAddTextTrack(const TextTrackConfig& config,
                      const AddTextTrackDoneCB& done_cb) override;
  void OnWaitingForDecryptionKey() override;
  void OnAudioConfigChange(const AudioDecoderConfig& config) override;
  void OnVideoConfigChange(const VideoDecoderConfig& config) override;
  void OnVideoNaturalSizeChange(const gfx::Size& size) override;
  void OnVideoOpacityChange(bool opaque) override;
  void OnVideoAverageKeyframeDistanceUpdate() override;

  // Actually seek. Avoids causing |should_notify_time_changed_| to be set when
  // |time_updated| is false.
  void DoSeek(base::TimeDelta time, bool time_updated);

  // Called after |defer_load_cb_| has decided to allow the load. If
  // |defer_load_cb_| is null this is called immediately.
  void DoLoad(LoadType load_type,
              const blink::WebURL& url,
              CORSMode cors_mode);

  // Called after asynchronous initialization of a data source completed.
  void DataSourceInitialized(bool success);

  // Called when the data source is downloading or paused.
  void NotifyDownloading(bool is_downloading);

  // Called by SurfaceManager when a surface is created.
  void OnSurfaceCreated(int surface_id);

  // Called by RenderFrameImpl with the overlay routing token, if we request it.
  void OnOverlayRoutingToken(const base::UnguessableToken& token);

  // Called by GpuVideoDecoder on Android to request a surface to render to (if
  // necessary).
  void OnOverlayInfoRequested(
      bool decoder_requires_restart_for_overlay,
      const ProvideOverlayInfoCB& provide_overlay_info_cb);

  // Creates a Renderer via the |renderer_factory_selector_|.
  std::unique_ptr<Renderer> CreateRenderer();

  // Finishes starting the pipeline due to a call to load().
  void StartPipeline();

  // Restart the player/pipeline as soon as possible. This will destroy the
  // current renderer, if any, and create a new one via the RendererFactory; and
  // then seek to resume playback at the current position.
  void ScheduleRestart();

  // Helpers that set the network/ready state and notifies the client if
  // they've changed.
  void SetNetworkState(blink::WebMediaPlayer::NetworkState state);
  void SetReadyState(blink::WebMediaPlayer::ReadyState state);

  // Returns the current video frame from |compositor_|. Blocks until the
  // compositor can return the frame.
  scoped_refptr<VideoFrame> GetCurrentFrameFromCompositor() const;

  // Called when the demuxer encounters encrypted streams.
  void OnEncryptedMediaInitData(EmeInitDataType init_data_type,
                                const std::vector<uint8_t>& init_data);

  // Called when the FFmpegDemuxer encounters new media tracks. This is only
  // invoked when using FFmpegDemuxer, since MSE/ChunkDemuxer handle media
  // tracks separately in WebSourceBufferImpl.
  void OnFFmpegMediaTracksUpdated(std::unique_ptr<MediaTracks> tracks);

  // Sets CdmContext from |cdm| on the pipeline and calls OnCdmAttached()
  // when done.
  void SetCdm(blink::WebContentDecryptionModule* cdm);

  // Called when a CDM has been attached to the |pipeline_|.
  void OnCdmAttached(bool success);

  // Inspects the current playback state and:
  //   - notifies |delegate_|,
  //   - toggles the memory usage reporting timer, and
  //   - toggles suspend/resume as necessary.
  //
  // This method should be called any time its dependent values change. These
  // are:
  //   - isRemote(),
  //   - hasVideo(),
  //   - delegate_->IsHidden(),
  //   - network_state_, ready_state_,
  //   - is_idle_, must_suspend_,
  //   - paused_, ended_,
  //   - pending_suspend_resume_cycle_,
  void UpdatePlayState();

  // Methods internal to UpdatePlayState().
  PlayState UpdatePlayState_ComputePlayState(bool is_remote,
                                             bool can_auto_suspend,
                                             bool is_suspended,
                                             bool is_backgrounded);
  void SetDelegateState(DelegateState new_state, bool is_idle);
  void SetMemoryReportingState(bool is_memory_reporting_enabled);
  void SetSuspendState(bool is_suspended);

  // Called at low frequency to tell external observers how much memory we're
  // using for video playback.  Called by |memory_usage_reporting_timer_|.
  // Memory usage reporting is done in two steps, because |demuxer_| must be
  // accessed on the media thread.
  void ReportMemoryUsage();
  void FinishMemoryUsageReport(int64_t demuxer_memory_usage);

  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

  // Called during OnHidden() when we want a suspended player to enter the
  // paused state after some idle timeout.
  void ScheduleIdlePauseTimer();

  // Returns |true| before HaveFutureData whenever there has been loading
  // progress and we have not been resumed for at least kLoadingToIdleTimeout
  // since then.
  //
  // This is used to delay suspension long enough for preroll to complete, which
  // is necessay because play() will not be called before HaveFutureData (and
  // thus we think we are idle forever).
  bool IsPrerollAttemptNeeded();

  void CreateWatchTimeReporter();

  // Returns true if the player is hidden.
  bool IsHidden() const;

  // Returns true if the player's source is streaming.
  bool IsStreaming() const;

  // Return whether |pipeline_metadata_| is compatible with an overlay. This
  // is intended for android.
  bool DoesOverlaySupportMetadata() const;

  // Whether the video should be paused when hidden. Uses metadata so has
  // meaning only after the pipeline has started, otherwise returns false.
  // Doesn't check if the video can actually be paused depending on the
  // pipeline's state.
  bool ShouldPauseVideoWhenHidden() const;

  // Whether the video track should be disabled when hidden. Uses metadata so
  // has meaning only after the pipeline has started, otherwise returns false.
  // Doesn't check if the video track can actually be disabled depending on the
  // pipeline's state.
  bool ShouldDisableVideoWhenHidden() const;

  // Whether the video is suitable for background playback optimizations (either
  // pausing it or disabling the video track). Uses metadata so has meaning only
  // after the pipeline has started, otherwise returns false.
  // The logical OR between the two methods above that is also used as their
  // common implementation.
  bool IsBackgroundOptimizationCandidate() const;

  // If enabling or disabling background video optimization has been delayed,
  // because of the pipeline not running, seeking or resuming, this method
  // needs to be called to update the optimization state.
  void UpdateBackgroundVideoOptimizationState();

  // Pauses a hidden video only player to save power if possible.
  // Must be called when either of the following happens:
  // - right after the video was hidden,
  // - right ater the pipeline has resumed if the video is hidden.
  void PauseVideoIfNeeded();

  // Disables the video track to save power if possible.
  // Must be called when either of the following happens:
  // - right after the video was hidden,
  // - right after the pipeline has started (|seeking_| is used to detect the
  //   when pipeline started) if the video is hidden,
  // - right ater the pipeline has resumed if the video is hidden.
  void DisableVideoTrackIfNeeded();

  // Enables the video track if it was disabled before to save power.
  // Must be called when either of the following happens:
  // - right after the video was shown,
  // - right before the pipeline is requested to resume
  //   (see https://crbug.com/678374),
  // - right after the pipeline has resumed if the video is not hidden.
  void EnableVideoTrackIfNeeded();

  // Overrides the pipeline statistics returned by GetPiplineStatistics() for
  // tests.
  void SetPipelineStatisticsForTest(const PipelineStatistics& stats);

  // Returns the pipeline statistics or the value overridden by tests.
  PipelineStatistics GetPipelineStatistics() const;

  // Overrides the pipeline media duration returned by
  // GetPipelineMediaDuration() for tests.
  void SetPipelineMediaDurationForTest(base::TimeDelta duration);

  // Return the pipeline media duration or the value overridden by tests.
  base::TimeDelta GetPipelineMediaDuration() const;

  void ReportTimeFromForegroundToFirstFrame(base::TimeTicks foreground_time,
                                            base::TimeTicks new_frame_time);

  // Records |duration| to the appropriate metric based on whether we're
  // handling a src= or MSE based playback.
  void RecordUnderflowDuration(base::TimeDelta duration);

  // Called by the data source when loading progresses.
  // Can be called quite often.
  void OnProgress();

  // Returns true when we estimate that we can play the rest of the media
  // without buffering.
  bool CanPlayThrough();

  // Records |natural_size| to MediaLog and video height to UMA.
  void RecordVideoNaturalSize(const gfx::Size& natural_size);

  // Takes ownership of |tick_clock|
  void SetTickClockForTest(base::TickClock* tick_clock);

  blink::WebLocalFrame* frame_;

  // The playback state last reported to |delegate_|, to avoid setting duplicate
  // states.
  // TODO(sandersd): The delegate should be implementing deduplication.
  DelegateState delegate_state_;
  bool delegate_has_audio_;

  blink::WebMediaPlayer::NetworkState network_state_;
  blink::WebMediaPlayer::ReadyState ready_state_;
  blink::WebMediaPlayer::ReadyState highest_ready_state_;

  // Preload state for when |data_source_| is created after setPreload().
  MultibufferDataSource::Preload preload_;

  // Task runner for posting tasks on Chrome's main thread. Also used
  // for DCHECKs so methods calls won't execute in the wrong thread.
  const scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;

  scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
  scoped_refptr<base::TaskRunner> worker_task_runner_;
  std::unique_ptr<MediaLog> media_log_;

  // |pipeline_controller_| owns an instance of Pipeline.
  PipelineController pipeline_controller_;

  // The LoadType passed in the |load_type| parameter of the load() call.
  LoadType load_type_;

  // Cache of metadata for answering hasAudio(), hasVideo(), and naturalSize().
  PipelineMetadata pipeline_metadata_;

  // Whether the video is known to be opaque or not.
  bool opaque_;

  // Playback state.
  //
  // TODO(scherkus): we have these because Pipeline favours the simplicity of a
  // single "playback rate" over worrying about paused/stopped etc...  It forces
  // all clients to manage the pause+playback rate externally, but is that
  // really a bad thing?
  //
  // TODO(scherkus): since SetPlaybackRate(0) is asynchronous and we don't want
  // to hang the render thread during pause(), we record the time at the same
  // time we pause and then return that value in currentTime().  Otherwise our
  // clock can creep forward a little bit while the asynchronous
  // SetPlaybackRate(0) is being executed.
  double playback_rate_;

  // Set while paused. |paused_time_| is only valid when |paused_| is true.
  bool paused_;
  base::TimeDelta paused_time_;

  // Set if paused automatically when hidden and need to resume when visible.
  // Reset if paused for any other reason.
  bool paused_when_hidden_;

  // Set when starting, seeking, and resuming (all of which require a Pipeline
  // seek). |seek_time_| is only valid when |seeking_| is true.
  bool seeking_;
  base::TimeDelta seek_time_;

  // Set when doing a restart (a suspend and resume in sequence) of the pipeline
  // in order to destruct and reinitialize the decoders. This is separate from
  // |pending_resume_| and |pending_suspend_| because they can be elided in
  // certain cases, whereas for a restart they must happen.
  // TODO(sandersd,watk): Create a simpler interface for a pipeline restart.
  bool pending_suspend_resume_cycle_;

  // TODO(scherkus): Replace with an explicit ended signal to HTMLMediaElement,
  // see http://crbug.com/409280
  bool ended_;

  // Tracks whether to issue time changed notifications during buffering state
  // changes.
  bool should_notify_time_changed_;

  bool overlay_enabled_;

  // Whether the current decoder requires a restart on overlay transitions.
  bool decoder_requires_restart_for_overlay_;

  blink::WebMediaPlayerClient* client_;
  blink::WebMediaPlayerEncryptedMediaClient* encrypted_client_;

  // WebMediaPlayer notifies the |delegate_| of playback state changes using
  // |delegate_id_|; an id provided after registering with the delegate.  The
  // WebMediaPlayer may also receive directives (play, pause) from the delegate
  // via the WebMediaPlayerDelegate::Observer interface after registration.
  //
  // NOTE: HTMLMediaElement is a Blink::SuspendableObject, and will receive a
  // call to contextDestroyed() when Blink::Document::shutdown() is called.
  // Document::shutdown() is called before the frame detaches (and before the
  // frame is destroyed). RenderFrameImpl owns |delegate_| and is guaranteed
  // to outlive |this|; thus it is safe to store |delegate_| as a raw pointer.
  media::WebMediaPlayerDelegate* delegate_;
  int delegate_id_;

  WebMediaPlayerParams::DeferLoadCB defer_load_cb_;
  WebMediaPlayerParams::Context3DCB context_3d_cb_;

  // Members for notifying upstream clients about internal memory usage.  The
  // |adjust_allocated_memory_cb_| must only be called on |main_task_runner_|.
  base::RepeatingTimer memory_usage_reporting_timer_;
  WebMediaPlayerParams::AdjustAllocatedMemoryCB adjust_allocated_memory_cb_;
  int64_t last_reported_memory_usage_;

  // Routes audio playback to either AudioRendererSink or WebAudio.
  scoped_refptr<WebAudioSourceProviderImpl> audio_source_provider_;

  bool supports_save_;

  // These two are mutually exclusive:
  //   |data_source_| is used for regular resource loads.
  //   |chunk_demuxer_| is used for Media Source resource loads.
  //
  // |demuxer_| will contain the appropriate demuxer based on which resource
  // load strategy we're using.
  std::unique_ptr<MultibufferDataSource> data_source_;
  std::unique_ptr<Demuxer> demuxer_;
  ChunkDemuxer* chunk_demuxer_;

  std::unique_ptr<base::MemoryPressureListener> memory_pressure_listener_;

  std::unique_ptr<base::TickClock> tick_clock_;

  BufferedDataSourceHostImpl buffered_data_source_host_;
  UrlIndex* url_index_;

  // Video rendering members.
  scoped_refptr<base::SingleThreadTaskRunner> compositor_task_runner_;
  VideoFrameCompositor* compositor_;  // Deleted on |compositor_task_runner_|.
  SkCanvasVideoRenderer skcanvas_video_renderer_;

  // The compositor layer for displaying the video content when using composited
  // playback.
  std::unique_ptr<cc_blink::WebLayerImpl> video_weblayer_;

  std::unique_ptr<blink::WebContentDecryptionModuleResult> set_cdm_result_;

  // If a CDM is attached keep a reference to it, so that it is not destroyed
  // until after the pipeline is done with it.
  scoped_refptr<ContentDecryptionModule> cdm_;

  // Keep track of the CDM while it is in the process of attaching to the
  // pipeline.
  scoped_refptr<ContentDecryptionModule> pending_cdm_;

#if defined(OS_ANDROID)  // WMPI_CAST
  WebMediaPlayerCast cast_impl_;
#endif

  // The last volume received by setVolume() and the last volume multiplier from
  // OnVolumeMultiplierUpdate().  The multiplier is typical 1.0, but may be less
  // if the WebMediaPlayerDelegate has requested a volume reduction (ducking)
  // for a transient sound.  Playout volume is derived by volume * multiplier.
  double volume_;
  double volume_multiplier_;

  std::unique_ptr<RendererFactorySelector> renderer_factory_selector_;

  // For requesting surfaces on behalf of the Android H/W decoder in fullscreen.
  // This will be null everywhere but Android.
  SurfaceManager* surface_manager_;

  // For canceling ongoing surface creation requests when exiting fullscreen.
  base::CancelableCallback<void(int)> surface_created_cb_;

  // The current overlay surface id. Populated, possibly with kNoSurfaceID if
  // we're not supposed to use an overlay, unless we have an outstanding surface
  // request to the SurfaceManager.
  base::Optional<int> overlay_surface_id_;

  // For canceling AndroidOverlay routing token requests.
  base::CancelableCallback<void(const base::UnguessableToken&)>
      token_available_cb_;

  // If overlay info is requested before we have it, then the request is saved
  // and satisfied once the overlay info is available. If the decoder does not
  // require restart to change surfaces, this is callback is kept until cleared
  // by the decoder.
  ProvideOverlayInfoCB provide_overlay_info_cb_;

  // On Android an overlay surface means using
  // SurfaceView instead of SurfaceTexture.

  // Use overlays for all video.
  bool force_video_overlays_;

  // Suppresses calls to OnPipelineError() after destruction / shutdown has been
  // started; prevents us from spuriously logging errors that are transient or
  // unimportant.
  bool suppress_destruction_errors_;

  // Used for HLS playback and in certain fallback paths (e.g. on older devices
  // that can't support the unified media pipeline).
  GURL loaded_url_;

  // NOTE: |using_media_player_renderer_| is set based on the usage of a
  // MediaResource::Type::URL in StartPipeline(). This currently works because
  // the MediaPlayerRendererClient factory is the only factory that returns that
  // Type, but this may no longer be accurate when we remove |cast_impl_| and
  // WebMediaPlayerCast. This flag should be renamed/updated accordingly when
  // removing |cast_impl_|.
  bool using_media_player_renderer_ = false;

  // Called sometime after the media is suspended in a playing state in
  // OnFrameHidden(), causing the state to change to paused.
  base::OneShotTimer background_pause_timer_;

  // Monitors the watch time of the played content.
  std::unique_ptr<WatchTimeReporter> watch_time_reporter_;
  bool is_encrypted_;

  // Elapsed time since we've last reached BUFFERING_HAVE_NOTHING.
  std::unique_ptr<base::ElapsedTimer> underflow_timer_;

  // Used to track loading progress, used by IsPrerollAttemptNeeded().
  // |preroll_attempt_pending_| indicates that the clock has been reset
  // (awaiting a resume to start), while |preroll_attempt_start_time_| tracks
  // when a preroll attempt began.
  bool preroll_attempt_pending_;
  base::TimeTicks preroll_attempt_start_time_;

  // Monitors the player events.
  base::WeakPtr<MediaObserver> observer_;

  // Owns the weblayer and obtains/maintains SurfaceIds for
  // kUseSurfaceLayerForVideo feature.
  std::unique_ptr<blink::WebSurfaceLayerBridge> bridge_;

  // The maximum video keyframe distance that allows triggering background
  // playback optimizations (non-MSE).
  base::TimeDelta max_keyframe_distance_to_disable_background_video_;

  // The maximum video keyframe distance that allows triggering background
  // playback optimizations (MSE).
  base::TimeDelta max_keyframe_distance_to_disable_background_video_mse_;

  // When MSE memory pressure based garbage collection is enabled, the
  // |enable_instant_source_buffer_gc| controls whether the GC is done
  // immediately on memory pressure notification or during the next SourceBuffer
  // append (slower, but MSE spec compliant).
  bool enable_instant_source_buffer_gc_ = false;

  // Whether disabled the video track as an optimization.
  bool video_track_disabled_ = false;

  // Whether the pipeline is being resumed at the moment.
  bool is_pipeline_resuming_ = false;

  // When this is true, pipeline will not be auto suspended.
  bool disable_pipeline_auto_suspend_ = false;

  // Pipeline statistics overridden by tests.
  base::Optional<PipelineStatistics> pipeline_statistics_for_test_;

  // Pipeline media duration overridden by tests.
  base::Optional<base::TimeDelta> pipeline_media_duration_for_test_;

  // Whether the video requires a user gesture to resume after it was paused in
  // the background. Affects the value of ShouldPauseVideoWhenHidden().
  bool video_locked_when_paused_when_hidden_ = false;

  // Whether embedded media experience is currently enabled.
  bool embedded_media_experience_enabled_ = false;

  // Whether the use of a surface layer instead of a video layer is enabled.
  bool surface_layer_for_video_enabled_ = false;

  mutable gfx::Size last_uploaded_frame_size_;
  mutable base::TimeDelta last_uploaded_frame_timestamp_;

  base::CancelableCallback<void(base::TimeTicks)> frame_time_report_cb_;

  bool initial_video_height_recorded_ = false;

  enum class OverlayMode {
    // All overlays are turned off.
    kNoOverlays,

    // Use ContentVideoView for overlays.
    kUseContentVideoView,

    // Use AndroidOverlay for overlays.
    kUseAndroidOverlay,
  };

  OverlayMode overlay_mode_ = OverlayMode::kNoOverlays;

  // Optional callback to request the routing token for AndroidOverlay.
  RequestRoutingTokenCallback request_routing_token_cb_;

  // If |overlay_routing_token_is_pending_| is false, then
  // |overlay_routing_token_| contains the routing token we should send, if any.
  // Otherwise, |overlay_routing_token_| is undefined.  We set the flag while
  // we have a request for the token that hasn't been answered yet; i.e., it
  // means that we don't know what, if any, token we should be using.
  bool overlay_routing_token_is_pending_ = false;
  OverlayInfo::RoutingToken overlay_routing_token_;

  OverlayInfo overlay_info_;

  DISALLOW_COPY_AND_ASSIGN(WebMediaPlayerImpl);
};

}  // namespace media

#endif  // MEDIA_BLINK_WEBMEDIAPLAYER_IMPL_H_
