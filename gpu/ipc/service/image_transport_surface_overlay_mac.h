// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_
#define GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_

#include <list>
#include <memory>
#include <vector>

#import "base/mac/scoped_nsobject.h"
#include "base/timer/timer.h"
#include "gpu/ipc/service/gpu_command_buffer_stub.h"
#include "gpu/ipc/service/image_transport_surface.h"
#include "ui/base/cocoa/remote_layer_api.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gpu_switching_observer.h"
#include "ui/latency/latency_info.h"

@class CAContext;
@class CALayer;

namespace ui {
class CALayerTreeCoordinator;
struct CARendererLayerParams;
}

namespace gl {
class GLFence;
}

namespace gpu {

class ImageTransportSurfaceOverlayMac : public gl::GLSurface,
                                        public ui::GpuSwitchingObserver {
 public:
  explicit ImageTransportSurfaceOverlayMac(
      base::WeakPtr<ImageTransportSurfaceDelegate> delegate);

  // GLSurface implementation
  bool Initialize(gl::GLSurfaceFormat format) override;
  void Destroy() override;
  bool Resize(const gfx::Size& size,
              float scale_factor,
              ColorSpace color_space,
              bool has_alpha) override;
  bool IsOffscreen() override;
  gfx::SwapResult SwapBuffers() override;
  gfx::SwapResult PostSubBuffer(int x, int y, int width, int height) override;
  bool SupportsPostSubBuffer() override;
  gfx::Size GetSize() override;
  void* GetHandle() override;
  gl::GLSurfaceFormat GetFormat() override;
  bool OnMakeCurrent(gl::GLContext* context) override;
  bool ScheduleOverlayPlane(int z_order,
                            gfx::OverlayTransform transform,
                            gl::GLImage* image,
                            const gfx::Rect& bounds_rect,
                            const gfx::RectF& crop_rect) override;
  bool ScheduleCALayer(const ui::CARendererLayerParams& params) override;
  void ScheduleCALayerInUseQuery(
      std::vector<CALayerInUseQuery> queries) override;
  bool IsSurfaceless() const override;

  // ui::GpuSwitchingObserver implementation.
  void OnGpuSwitched() override;

 private:
  ~ImageTransportSurfaceOverlayMac() override;

  void SetLatencyInfo(const std::vector<ui::LatencyInfo>& latency_info);
  void SendAcceleratedSurfaceBuffersSwapped(
      CAContextID ca_context_id,
      bool fullscreen_low_power_ca_context_valid,
      CAContextID fullscreen_low_power_ca_context_id,
      const gfx::ScopedRefCountedIOSurfaceMachPort& io_surface,
      const gfx::Size& size,
      float scale_factor,
      std::vector<ui::LatencyInfo> latency_info);
  gfx::SwapResult SwapBuffersInternal(const gfx::Rect& pixel_damage_rect);

  base::WeakPtr<ImageTransportSurfaceDelegate> delegate_;
  std::vector<ui::LatencyInfo> latency_info_;

  bool use_remote_layer_api_;
  base::scoped_nsobject<CAContext> ca_context_;
  base::scoped_nsobject<CAContext> fullscreen_low_power_ca_context_;
  std::unique_ptr<ui::CALayerTreeCoordinator> ca_layer_tree_coordinator_;

  gfx::Size pixel_size_;
  float scale_factor_;

  std::vector<CALayerInUseQuery> ca_layer_in_use_queries_;

  // A GLFence marking the end of the previous frame. Must only be accessed
  // while the associated |previous_frame_context_| is bound.
  std::unique_ptr<gl::GLFence> previous_frame_fence_;
  base::ScopedTypeRef<CGLContextObj> fence_context_obj_;

  // The renderer ID that all contexts made current to this surface should be
  // targeting.
  GLint gl_renderer_id_;
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_
