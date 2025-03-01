// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_LAYER_IMPL_TEST_PROPERTIES_H_
#define CC_LAYERS_LAYER_IMPL_TEST_PROPERTIES_H_

#include <set>
#include <vector>

#include "base/memory/ptr_util.h"
#include "cc/base/filter_operations.h"
#include "cc/input/scroll_boundary_behavior.h"
#include "cc/layers/layer_collections.h"
#include "cc/layers/layer_position_constraint.h"
#include "cc/layers/layer_sticky_position_constraint.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "ui/gfx/geometry/point3_f.h"
#include "ui/gfx/transform.h"

namespace viz {
class CopyOutputRequest;
}

namespace cc {

class LayerImpl;

struct CC_EXPORT LayerImplTestProperties {
  explicit LayerImplTestProperties(LayerImpl* owning_layer);
  ~LayerImplTestProperties();

  void AddChild(std::unique_ptr<LayerImpl> child);
  std::unique_ptr<LayerImpl> RemoveChild(LayerImpl* child);
  void SetMaskLayer(std::unique_ptr<LayerImpl> mask);

  LayerImpl* owning_layer;
  bool double_sided;
  bool cache_render_surface;
  bool force_render_surface;
  bool is_container_for_fixed_position_layers;
  bool should_flatten_transform;
  bool hide_layer_and_subtree;
  bool opacity_can_animate;
  bool subtree_has_copy_request;
  int sorting_context_id;
  float opacity;
  FilterOperations filters;
  FilterOperations background_filters;
  gfx::PointF filters_origin;
  SkBlendMode blend_mode;
  LayerPositionConstraint position_constraint;
  LayerStickyPositionConstraint sticky_position_constraint;
  gfx::Point3F transform_origin;
  gfx::Transform transform;
  LayerImpl* scroll_parent;
  std::unique_ptr<std::set<LayerImpl*>> scroll_children;
  LayerImpl* clip_parent;
  std::unique_ptr<std::set<LayerImpl*>> clip_children;
  std::vector<std::unique_ptr<viz::CopyOutputRequest>> copy_requests;
  LayerImplList children;
  LayerImpl* mask_layer;
  LayerImpl* parent;
  bool user_scrollable_horizontal = true;
  bool user_scrollable_vertical = true;
  ScrollBoundaryBehavior scroll_boundary_behavior;
};

}  // namespace cc

#endif  // CC_LAYERS_LAYER_IMPL_TEST_PROPERTIES_H_
