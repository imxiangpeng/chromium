// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_LAYER_TREE_FRAME_SINK_H_
#define CC_OUTPUT_LAYER_TREE_FRAME_SINK_H_

#include <deque>
#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/thread_checker.h"
#include "cc/cc_export.h"
#include "cc/output/overlay_candidate_validator.h"
#include "cc/output/vulkan_context_provider.h"
#include "components/viz/common/gpu/context_provider.h"
#include "components/viz/common/resources/returned_resource.h"
#include "gpu/command_buffer/common/texture_in_use_response.h"
#include "ui/gfx/color_space.h"

namespace gpu {
class GpuMemoryBufferManager;
}

namespace viz {
class LocalSurfaceId;
class SharedBitmapManager;
struct BeginFrameAck;
}

namespace cc {

class CompositorFrame;
class LayerTreeFrameSinkClient;

// An interface for submitting CompositorFrames to a display compositor
// which will compose frames from multiple clients to show on screen to the
// user.
// If a context_provider() is present, frames should be submitted with
// OpenGL resources (created with the context_provider()). If not, then
// SharedBitmap resources should be used.
class CC_EXPORT LayerTreeFrameSink {
 public:
  struct Capabilities {
    Capabilities() = default;

    // True if we must always swap, even if there is no damage to the frame.
    // Needed for both the browser compositor as well as layout tests.
    // TODO(ericrk): This should be test-only for layout tests, but tab
    // capture has issues capturing offscreen tabs whithout this. We should
    // remove this dependency. crbug.com/680196
    bool must_always_swap = false;

    // True if sync points for resources are needed when swapping delegated
    // frames.
    bool delegated_sync_points_required = true;
  };

  // Constructor for GL-based and/or software resources.
  // gpu_memory_buffer_manager and shared_bitmap_manager must outlive the
  // LayerTreeFrameSink.
  // shared_bitmap_manager is optional (won't be used) if context_provider is
  // present.
  // gpu_memory_buffer_manager is optional (won't be used) if context_provider
  // is not present.
  LayerTreeFrameSink(
      scoped_refptr<viz::ContextProvider> context_provider,
      scoped_refptr<viz::ContextProvider> worker_context_provider,
      gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
      viz::SharedBitmapManager* shared_bitmap_manager);

  // Constructor for Vulkan-based resources.
  explicit LayerTreeFrameSink(
      scoped_refptr<VulkanContextProvider> vulkan_context_provider);

  virtual ~LayerTreeFrameSink();

  // Called by the compositor on the compositor thread. This is a place where
  // thread-specific data for the output surface can be initialized, since from
  // this point to when DetachFromClient() is called the output surface will
  // only be used on the compositor thread.
  // The caller should call DetachFromClient() on the same thread before
  // destroying the LayerTreeFrameSink, even if this fails. And BindToClient
  // should not be called twice for a given LayerTreeFrameSink.
  virtual bool BindToClient(LayerTreeFrameSinkClient* client);

  // Must be called from the thread where BindToClient was called if
  // BindToClient succeeded, after which the LayerTreeFrameSink may be
  // destroyed from any thread. This is a place where thread-specific data for
  // the object can be uninitialized.
  virtual void DetachFromClient();

  bool HasClient() { return !!client_; }

  const Capabilities& capabilities() const { return capabilities_; }

  // The viz::ContextProviders may be null if frames should be submitted with
  // software SharedBitmap resources.
  viz::ContextProvider* context_provider() const {
    return context_provider_.get();
  }
  viz::ContextProvider* worker_context_provider() const {
    return worker_context_provider_.get();
  }
  VulkanContextProvider* vulkan_context_provider() const {
    return vulkan_context_provider_.get();
  }
  gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager() const {
    return gpu_memory_buffer_manager_;
  }
  viz::SharedBitmapManager* shared_bitmap_manager() const {
    return shared_bitmap_manager_;
  }

  // If supported, this sets the viz::LocalSurfaceId the LayerTreeFrameSink will
  // use to submit a CompositorFrame.
  virtual void SetLocalSurfaceId(const viz::LocalSurfaceId& local_surface_id) {}

  // Support for a pull-model where draws are requested by the implementation of
  // LayerTreeFrameSink. This is called by the compositor to notify that there's
  // new content.
  virtual void Invalidate() {}

  // For successful swaps, the implementation must call
  // DidReceiveCompositorFrameAck() asynchronously when the frame has been
  // processed in order to unthrottle the next frame.
  virtual void SubmitCompositorFrame(CompositorFrame frame) = 0;

  // Signals that a BeginFrame issued by the viz::BeginFrameSource provided to
  // the client did not lead to a CompositorFrame submission.
  virtual void DidNotProduceFrame(const viz::BeginFrameAck& ack) = 0;

 protected:
  // Bound to the viz::ContextProvider to hear about when it is lost and inform
  // the |client_|.
  void DidLoseLayerTreeFrameSink();

  LayerTreeFrameSinkClient* client_ = nullptr;

  struct LayerTreeFrameSink::Capabilities capabilities_;
  scoped_refptr<viz::ContextProvider> context_provider_;
  scoped_refptr<viz::ContextProvider> worker_context_provider_;
  scoped_refptr<VulkanContextProvider> vulkan_context_provider_;
  gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager_;
  viz::SharedBitmapManager* shared_bitmap_manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LayerTreeFrameSink);
};

}  // namespace cc

#endif  // CC_OUTPUT_LAYER_TREE_FRAME_SINK_H_
