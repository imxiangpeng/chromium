// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_QUADS_STREAM_VIDEO_DRAW_QUAD_H_
#define CC_QUADS_STREAM_VIDEO_DRAW_QUAD_H_

#include <stddef.h>

#include <memory>

#include "cc/cc_export.h"
#include "cc/quads/draw_quad.h"
#include "ui/gfx/transform.h"

namespace cc {

class CC_EXPORT StreamVideoDrawQuad : public DrawQuad {
 public:
  static const size_t kResourceIdIndex = 0;

  StreamVideoDrawQuad();

  void SetNew(const SharedQuadState* shared_quad_state,
              const gfx::Rect& rect,
              const gfx::Rect& opaque_rect,
              const gfx::Rect& visible_rect,
              unsigned resource_id,
              gfx::Size resource_size_in_pixels,
              const gfx::Transform& matrix);

  void SetAll(const SharedQuadState* shared_quad_state,
              const gfx::Rect& rect,
              const gfx::Rect& opaque_rect,
              const gfx::Rect& visible_rect,
              bool needs_blending,
              unsigned resource_id,
              gfx::Size resource_size_in_pixels,
              const gfx::Transform& matrix);

  gfx::Transform matrix;

  struct OverlayResources {
    OverlayResources();
    gfx::Size size_in_pixels[Resources::kMaxResourceIdCount];
  };
  OverlayResources overlay_resources;

  static const StreamVideoDrawQuad* MaterialCast(const DrawQuad*);

  viz::ResourceId resource_id() const {
    return resources.ids[kResourceIdIndex];
  }
  const gfx::Size& resource_size_in_pixels() const {
    return overlay_resources.size_in_pixels[kResourceIdIndex];
  }

 private:
  void ExtendValue(base::trace_event::TracedValue* value) const override;
};

}  // namespace cc

#endif  // CC_QUADS_STREAM_VIDEO_DRAW_QUAD_H_
