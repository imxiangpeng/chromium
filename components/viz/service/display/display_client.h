// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_DISPLAY_CLIENT_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_DISPLAY_CLIENT_H_

#include "cc/quads/render_pass.h"

namespace viz {

class DisplayClient {
 public:
  virtual ~DisplayClient() {}
  virtual void DisplayOutputSurfaceLost() = 0;
  virtual void DisplayWillDrawAndSwap(
      bool will_draw_and_swap,
      const cc::RenderPassList& render_passes) = 0;
  virtual void DisplayDidDrawAndSwap() = 0;
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_DISPLAY_CLIENT_H_
