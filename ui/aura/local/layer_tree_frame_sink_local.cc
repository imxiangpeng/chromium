// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/local/layer_tree_frame_sink_local.h"

#include "cc/output/layer_tree_frame_sink_client.h"
#include "components/viz/host/host_frame_sink_manager.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"

namespace aura {

LayerTreeFrameSinkLocal::LayerTreeFrameSinkLocal(
    const viz::FrameSinkId& frame_sink_id,
    viz::HostFrameSinkManager* host_frame_sink_manager)
    : cc::LayerTreeFrameSink(nullptr, nullptr, nullptr, nullptr),
      frame_sink_id_(frame_sink_id),
      host_frame_sink_manager_(host_frame_sink_manager) {}

LayerTreeFrameSinkLocal::~LayerTreeFrameSinkLocal() {}

bool LayerTreeFrameSinkLocal::BindToClient(
    cc::LayerTreeFrameSinkClient* client) {
  if (!cc::LayerTreeFrameSink::BindToClient(client))
    return false;
  DCHECK(!thread_checker_);
  thread_checker_ = base::MakeUnique<base::ThreadChecker>();

  support_ = host_frame_sink_manager_->CreateCompositorFrameSinkSupport(
      this, frame_sink_id_, false /* is_root */,
      true /* handles_frame_sink_id_invalidation */,
      true /* needs_sync_points */);
  begin_frame_source_ = base::MakeUnique<viz::ExternalBeginFrameSource>(this);
  client->SetBeginFrameSource(begin_frame_source_.get());
  return true;
}

void LayerTreeFrameSinkLocal::SetSurfaceChangedCallback(
    const SurfaceChangedCallback& callback) {
  DCHECK(!surface_changed_callback_);
  surface_changed_callback_ = callback;
}

void LayerTreeFrameSinkLocal::DetachFromClient() {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  client_->SetBeginFrameSource(nullptr);
  begin_frame_source_.reset();
  support_->EvictCurrentSurface();
  support_.reset();
  thread_checker_.reset();
  cc::LayerTreeFrameSink::DetachFromClient();
}

void LayerTreeFrameSinkLocal::SubmitCompositorFrame(cc::CompositorFrame frame) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  DCHECK(frame.metadata.begin_frame_ack.has_damage);
  DCHECK_LE(viz::BeginFrameArgs::kStartingFrameNumber,
            frame.metadata.begin_frame_ack.sequence_number);

  viz::LocalSurfaceId old_local_surface_id = local_surface_id_;
  const auto& frame_size = frame.render_pass_list.back()->output_rect.size();
  if (frame_size != surface_size_ ||
      frame.metadata.device_scale_factor != device_scale_factor_ ||
      !local_surface_id_.is_valid()) {
    surface_size_ = frame_size;
    device_scale_factor_ = frame.metadata.device_scale_factor;
    local_surface_id_ = id_allocator_.GenerateId();
  }
  bool result =
      support_->SubmitCompositorFrame(local_surface_id_, std::move(frame));
  DCHECK(result);

  if (local_surface_id_ != old_local_surface_id) {
    surface_changed_callback_.Run(
        viz::SurfaceId(frame_sink_id_, local_surface_id_), surface_size_);
  }
}

void LayerTreeFrameSinkLocal::DidNotProduceFrame(
    const viz::BeginFrameAck& ack) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  DCHECK(!ack.has_damage);
  DCHECK_LE(viz::BeginFrameArgs::kStartingFrameNumber, ack.sequence_number);
  support_->DidNotProduceFrame(ack);
}

void LayerTreeFrameSinkLocal::DidReceiveCompositorFrameAck(
    const std::vector<viz::ReturnedResource>& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  if (!resources.empty())
    client_->ReclaimResources(resources);
  client_->DidReceiveCompositorFrameAck();
}

void LayerTreeFrameSinkLocal::OnBeginFrame(const viz::BeginFrameArgs& args) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  begin_frame_source_->OnBeginFrame(args);
}

void LayerTreeFrameSinkLocal::OnBeginFramePausedChanged(bool paused) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  begin_frame_source_->OnSetBeginFrameSourcePaused(paused);
}

void LayerTreeFrameSinkLocal::ReclaimResources(
    const std::vector<viz::ReturnedResource>& resources) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  if (!client_)
    return;
  client_->ReclaimResources(resources);
}

void LayerTreeFrameSinkLocal::OnNeedsBeginFrames(bool needs_begin_frames) {
  DCHECK(thread_checker_);
  DCHECK(thread_checker_->CalledOnValidThread());
  support_->SetNeedsBeginFrame(needs_begin_frames);
}

}  // namespace aura
