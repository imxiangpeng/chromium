// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_HOST_HIT_TEST_HIT_TEST_QUERY_H_
#define COMPONENTS_VIZ_HOST_HIT_TEST_HIT_TEST_QUERY_H_

#include <vector>

#include "base/macros.h"
#include "components/viz/common/hit_test/aggregated_hit_test_region.h"
#include "ui/gfx/geometry/point.h"

namespace viz {

struct Target {
  FrameSinkId frame_sink_id;
  // Coordinates in the coordinate system of the target FrameSinkId.
  gfx::Point location_in_target;
  // Different flags are defined in services/viz/public/interfaces/hit_test/
  // hit_test_region_list.mojom.
  uint32_t flags = 0;
};

// Finds the target for a given location based on the AggregatedHitTestRegion
// list aggregated by HitTestAggregator.
// TODO(riajiang): Handle 3d space cases correctly.
class HitTestQuery {
 public:
  HitTestQuery();
  ~HitTestQuery();

  // TODO(riajiang): Read from shmem directly once it's set up and delete this
  // function. For now, use fake data. Also need to validate the data received.
  // http://crbug.com/746470
  void set_aggregated_hit_test_region_list(
      AggregatedHitTestRegion* aggregated_hit_test_region_list,
      uint32_t aggregated_hit_test_region_list_size) {
    aggregated_hit_test_region_list_ = aggregated_hit_test_region_list;
    aggregated_hit_test_region_list_size_ =
        aggregated_hit_test_region_list_size;
  }

  // Finds Target for |location_in_root|, including the FrameSinkId of the
  // target, updated location in the coordinate system of the target and
  // hit-test flags for the target.
  // Assumptions about the AggregatedHitTestRegion list received.
  // 1. The list is in ascending (front to back) z-order.
  // 2. Children count includes children of children.
  // 3. After applying transform to the incoming point, point is in the same
  // coordinate system as the bounds it is comparing against.
  // For example,
  //  +e-------------+
  //  |   +c---------|
  //  | 1 |+a--+     |
  //  |   || 2 |     |
  //  |   |+b--------|
  //  |   ||         |
  //  |   ||   3     |
  //  +--------------+
  // In this case, after applying identity transform, 1 is in the coordinate
  // system of e; apply the transfrom-from-e-to-c and transform-from-c-to-a
  // then we get 2 in the coordinate system of a; apply the
  // transfrom-from-e-to-c and transform-from-c-to-b then we get 3 in the
  // coordinate system of b.
  Target FindTargetForLocation(const gfx::Point& location_in_root) const;

 private:
  // Helper function to find |target| for |location_in_parent| in the |region|,
  // returns true if a target is found and false otherwise. |location_in_parent|
  // is in the coordinate space of |region|'s parent.
  bool FindTargetInRegionForLocation(const gfx::Point& location_in_parent,
                                     AggregatedHitTestRegion* region,
                                     Target* target) const;

  AggregatedHitTestRegion* aggregated_hit_test_region_list_ = nullptr;
  uint32_t aggregated_hit_test_region_list_size_ = 0;

  DISALLOW_COPY_AND_ASSIGN(HitTestQuery);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_HOST_HIT_TEST_HIT_TEST_QUERY_H_
