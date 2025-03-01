// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/vr/elements/transient_url_bar.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/vr/elements/url_bar_texture.h"
#include "chrome/browser/vr/target_property.h"

namespace vr {

using TargetProperty::OPACITY;
using TargetProperty::VISIBILITY;

TransientUrlBar::TransientUrlBar(
    int preferred_width,
    const base::TimeDelta& timeout,
    const base::Callback<void(UiUnsupportedMode)>& failure_callback)
    : TexturedElement(preferred_width),
      texture_(base::MakeUnique<UrlBarTexture>(true, failure_callback)),
      transience_(this, 1.0f, timeout) {}

TransientUrlBar::~TransientUrlBar() = default;

UiTexture* TransientUrlBar::GetTexture() const {
  return texture_.get();
}

void TransientUrlBar::SetEnabled(bool enabled) {
  transience_.SetEnabled(enabled);
  if (enabled)
    animation_player().SetTransitionedProperties({OPACITY, VISIBILITY});
  else
    animation_player().SetTransitionedProperties({});
}

void TransientUrlBar::SetToolbarState(const ToolbarState& state) {
  texture_->SetToolbarState(state);
}

}  // namespace vr
