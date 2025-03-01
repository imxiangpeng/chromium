// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_DIRECT_COMPOSITION_SURFACE_WIN_H_
#define GPU_IPC_SERVICE_DIRECT_COMPOSITION_SURFACE_WIN_H_

#include <d3d11.h>
#include <dcomp.h>
#include <windows.h>

#include "base/memory/weak_ptr.h"
#include "base/win/scoped_comptr.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/service/child_window_win.h"
#include "gpu/ipc/service/image_transport_surface_delegate.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/gl_surface_egl.h"

namespace gpu {

class DCLayerTree;
class DirectCompositionChildSurfaceWin;

class GPU_EXPORT DirectCompositionSurfaceWin : public gl::GLSurfaceEGL {
 public:
  DirectCompositionSurfaceWin(
      std::unique_ptr<gfx::VSyncProvider> vsync_provider,
      base::WeakPtr<ImageTransportSurfaceDelegate> delegate,
      HWND parent_window);

  // Returns true if there's an output on the current adapter that can
  // use overlays.
  static bool AreOverlaysSupported();

  // Returns true if there is an HDR capable display connected.
  static bool IsHDRSupported();

  bool InitializeNativeWindow();

  // GLSurfaceEGL implementation.
  using GLSurfaceEGL::Initialize;
  bool Initialize(gl::GLSurfaceFormat format) override;
  void Destroy() override;
  gfx::Size GetSize() override;
  bool IsOffscreen() override;
  void* GetHandle() override;
  bool Resize(const gfx::Size& size,
              float scale_factor,
              ColorSpace color_space,
              bool has_alpha) override;
  gfx::SwapResult SwapBuffers() override;
  gfx::SwapResult PostSubBuffer(int x, int y, int width, int height) override;
  gfx::VSyncProvider* GetVSyncProvider() override;
  bool SetEnableDCLayers(bool enable) override;
  bool FlipsVertically() const override;
  bool SupportsPostSubBuffer() override;
  bool OnMakeCurrent(gl::GLContext* context) override;
  bool SupportsDCLayers() const override;
  bool UseOverlaysForVideo() const override;
  bool SetDrawRectangle(const gfx::Rect& rect) override;
  gfx::Vector2d GetDrawOffset() const override;
  void WaitForSnapshotRendering() override;

  // This schedules an overlay plane to be displayed on the next SwapBuffers
  // or PostSubBuffer call. Overlay planes must be scheduled before every swap
  // to remain in the layer tree. This surface's backbuffer doesn't have to be
  // scheduled with ScheduleDCLayer, as it's automatically placed in the layer
  // tree at z-order 0.
  bool ScheduleDCLayer(const ui::DCRendererLayerParams& params) override;

  const base::win::ScopedComPtr<IDCompositionSurface> dcomp_surface() const;
  const base::win::ScopedComPtr<IDXGISwapChain1> swap_chain() const;

  scoped_refptr<base::TaskRunner> GetWindowTaskRunnerForTesting();

  base::win::ScopedComPtr<IDXGISwapChain1> GetLayerSwapChainForTesting(
      size_t index) const;

  const GpuDriverBugWorkarounds& workarounds() const { return workarounds_; }

 protected:
  ~DirectCompositionSurfaceWin() override;

 private:
  bool RecreateRootSurface();

  ChildWindowWin child_window_;

  GpuDriverBugWorkarounds workarounds_;

  HWND window_ = nullptr;
  // This is a placeholder surface used when not rendering to the
  // DirectComposition surface.
  EGLSurface default_surface_ = 0;

  gfx::Size size_ = gfx::Size(1, 1);
  bool enable_dc_layers_ = false;
  bool is_hdr_ = false;
  bool has_alpha_ = true;
  std::unique_ptr<gfx::VSyncProvider> vsync_provider_;
  scoped_refptr<DirectCompositionChildSurfaceWin> root_surface_;
  std::unique_ptr<DCLayerTree> layer_tree_;

  base::win::ScopedComPtr<ID3D11Device> d3d11_device_;
  base::win::ScopedComPtr<IDCompositionDevice2> dcomp_device_;

  DISALLOW_COPY_AND_ASSIGN(DirectCompositionSurfaceWin);
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_DIRECT_COMPOSITION_SURFACE_WIN_H_
