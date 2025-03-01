// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/fake_layer_tree_frame_sink.h"

#include "base/bind.h"
#include "base/threading/thread_task_runner_handle.h"
#include "cc/output/layer_tree_frame_sink_client.h"
#include "components/viz/common/frame_sinks/begin_frame_source.h"
#include "components/viz/common/frame_sinks/delay_based_time_source.h"
#include "components/viz/common/resources/returned_resource.h"
#include "components/viz/test/begin_frame_args_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {

FakeLayerTreeFrameSink::FakeLayerTreeFrameSink(
    scoped_refptr<viz::ContextProvider> context_provider,
    scoped_refptr<viz::ContextProvider> worker_context_provider)
    : LayerTreeFrameSink(std::move(context_provider),
                         std::move(worker_context_provider),
                         nullptr,
                         nullptr),
      weak_ptr_factory_(this) {
  gpu_memory_buffer_manager_ =
      context_provider_ ? &test_gpu_memory_buffer_manager_ : nullptr;
  shared_bitmap_manager_ =
      context_provider_ ? nullptr : &test_shared_bitmap_manager_;
}

FakeLayerTreeFrameSink::~FakeLayerTreeFrameSink() = default;

bool FakeLayerTreeFrameSink::BindToClient(LayerTreeFrameSinkClient* client) {
  if (!LayerTreeFrameSink::BindToClient(client))
    return false;
  begin_frame_source_ = base::MakeUnique<viz::BackToBackBeginFrameSource>(
      base::MakeUnique<viz::DelayBasedTimeSource>(
          base::ThreadTaskRunnerHandle::Get().get()));
  client_->SetBeginFrameSource(begin_frame_source_.get());
  return true;
}

void FakeLayerTreeFrameSink::DetachFromClient() {
  ReturnResourcesHeldByParent();
  LayerTreeFrameSink::DetachFromClient();
}

void FakeLayerTreeFrameSink::SubmitCompositorFrame(CompositorFrame frame) {
  ReturnResourcesHeldByParent();

  last_sent_frame_.reset(new CompositorFrame(std::move(frame)));
  ++num_sent_frames_;

  last_swap_rect_ = last_sent_frame_->render_pass_list.back()->damage_rect;

  resources_held_by_parent_.insert(resources_held_by_parent_.end(),
                                   last_sent_frame_->resource_list.begin(),
                                   last_sent_frame_->resource_list.end());

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeLayerTreeFrameSink::DidReceiveCompositorFrameAck,
                     weak_ptr_factory_.GetWeakPtr()));
}

void FakeLayerTreeFrameSink::DidNotProduceFrame(const viz::BeginFrameAck& ack) {
}

void FakeLayerTreeFrameSink::DidReceiveCompositorFrameAck() {
  client_->DidReceiveCompositorFrameAck();
}

void FakeLayerTreeFrameSink::ReturnResourcesHeldByParent() {
  if (last_sent_frame_) {
    // Return the last frame's resources immediately.
    std::vector<viz::ReturnedResource> resources;
    for (const auto& resource : resources_held_by_parent_)
      resources.push_back(resource.ToReturnedResource());
    resources_held_by_parent_.clear();
    client_->ReclaimResources(resources);
  }
}

}  // namespace cc
