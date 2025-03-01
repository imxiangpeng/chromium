// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_
#define SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_

#include "base/macros.h"
#include "components/viz/common/surfaces/local_surface_id_allocator.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/viz/compositing/privileged/interfaces/frame_sink_manager.mojom.h"
#include "services/viz/public/interfaces/compositing/compositor_frame_sink.mojom.h"

namespace ui {
namespace ws {

// CompositorFrameSinkClientBinding manages the binding between a FrameGenerator
// and its CompositorFrameSink. CompositorFrameSinkClientBinding exists so
// that a mock implementation of CompositorFrameSink can be injected for
// tests. FrameGenerator owns its associated CompositorFrameSinkClientBinding.
class CompositorFrameSinkClientBinding
    : public viz::mojom::CompositorFrameSink {
 public:
  CompositorFrameSinkClientBinding(
      viz::mojom::CompositorFrameSinkClient* sink_client,
      viz::mojom::CompositorFrameSinkClientRequest sink_client_request,
      viz::mojom::CompositorFrameSinkAssociatedPtr compositor_frame_sink,
      viz::mojom::DisplayPrivateAssociatedPtr display_private);
  ~CompositorFrameSinkClientBinding() override;

 private:
  // viz::mojom::CompositorFrameSink implementation:
  void SubmitCompositorFrame(const viz::LocalSurfaceId& local_surface_id,
                             cc::CompositorFrame frame) override;
  void SetNeedsBeginFrame(bool needs_begin_frame) override;
  void DidNotProduceFrame(const viz::BeginFrameAck& ack) override;

  mojo::Binding<viz::mojom::CompositorFrameSinkClient> binding_;
  viz::mojom::DisplayPrivateAssociatedPtr display_private_;
  viz::mojom::CompositorFrameSinkAssociatedPtr compositor_frame_sink_;
  viz::LocalSurfaceId local_surface_id_;
  viz::LocalSurfaceIdAllocator id_allocator_;
  gfx::Size last_submitted_frame_size_;

  DISALLOW_COPY_AND_ASSIGN(CompositorFrameSinkClientBinding);
};
}
}

#endif  // SERVICES_UI_WS_COMPOSITOR_FRAME_SINK_CLIENT_BINDING_H_
