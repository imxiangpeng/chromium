// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TEST_FAKE_LAYER_TREE_FRAME_SINK_H_
#define CC_TEST_FAKE_LAYER_TREE_FRAME_SINK_H_

#include <stddef.h>

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/layer_tree_frame_sink.h"
#include "cc/output/software_output_device.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_gles2_interface.h"
#include "cc/test/test_gpu_memory_buffer_manager.h"
#include "cc/test/test_shared_bitmap_manager.h"
#include "cc/test/test_web_graphics_context_3d.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"

namespace viz {
class BeginFrameSource;
}

namespace cc {

class FakeLayerTreeFrameSink : public LayerTreeFrameSink {
 public:
  ~FakeLayerTreeFrameSink() override;

  static std::unique_ptr<FakeLayerTreeFrameSink> Create3d() {
    return base::WrapUnique(new FakeLayerTreeFrameSink(
        TestContextProvider::Create(), TestContextProvider::CreateWorker()));
  }

  static std::unique_ptr<FakeLayerTreeFrameSink> Create3d(
      scoped_refptr<TestContextProvider> context_provider) {
    return base::WrapUnique(new FakeLayerTreeFrameSink(
        context_provider, TestContextProvider::CreateWorker()));
  }

  static std::unique_ptr<FakeLayerTreeFrameSink> Create3d(
      std::unique_ptr<TestWebGraphicsContext3D> context) {
    return base::WrapUnique(new FakeLayerTreeFrameSink(
        TestContextProvider::Create(std::move(context)),
        TestContextProvider::CreateWorker()));
  }

  static std::unique_ptr<FakeLayerTreeFrameSink> Create3dForGpuRasterization() {
    auto context = TestWebGraphicsContext3D::Create();
    context->set_gpu_rasterization(true);
    auto context_provider = TestContextProvider::Create(std::move(context));
    return base::WrapUnique(new FakeLayerTreeFrameSink(
        std::move(context_provider), TestContextProvider::CreateWorker()));
  }

  static std::unique_ptr<FakeLayerTreeFrameSink> CreateSoftware() {
    return base::WrapUnique(new FakeLayerTreeFrameSink(nullptr, nullptr));
  }

  // LayerTreeFrameSink implementation.
  void SubmitCompositorFrame(CompositorFrame frame) override;
  void DidNotProduceFrame(const viz::BeginFrameAck& ack) override;
  bool BindToClient(LayerTreeFrameSinkClient* client) override;
  void DetachFromClient() override;

  CompositorFrame* last_sent_frame() { return last_sent_frame_.get(); }
  size_t num_sent_frames() { return num_sent_frames_; }

  LayerTreeFrameSinkClient* client() { return client_; }

  const std::vector<viz::TransferableResource>& resources_held_by_parent() {
    return resources_held_by_parent_;
  }

  gfx::Rect last_swap_rect() const { return last_swap_rect_; }

  void ReturnResourcesHeldByParent();

 protected:
  FakeLayerTreeFrameSink(
      scoped_refptr<viz::ContextProvider> context_provider,
      scoped_refptr<viz::ContextProvider> worker_context_provider);

  TestGpuMemoryBufferManager test_gpu_memory_buffer_manager_;
  TestSharedBitmapManager test_shared_bitmap_manager_;

  std::unique_ptr<CompositorFrame> last_sent_frame_;
  size_t num_sent_frames_ = 0;
  std::vector<viz::TransferableResource> resources_held_by_parent_;
  gfx::Rect last_swap_rect_;

 private:
  void DidReceiveCompositorFrameAck();

  std::unique_ptr<viz::BeginFrameSource> begin_frame_source_;
  base::WeakPtrFactory<FakeLayerTreeFrameSink> weak_ptr_factory_;
};

}  // namespace cc

#endif  // CC_TEST_FAKE_LAYER_TREE_FRAME_SINK_H_
