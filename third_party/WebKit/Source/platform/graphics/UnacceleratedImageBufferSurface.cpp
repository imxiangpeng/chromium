/*
 * Copyright (c) 2013, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/graphics/UnacceleratedImageBufferSurface.h"

#include "platform/RuntimeEnabledFeatures.h"
#include "platform/graphics/StaticBitmapImage.h"
#include "platform/graphics/skia/SkiaUtils.h"
#include "platform/wtf/PassRefPtr.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

UnacceleratedImageBufferSurface::UnacceleratedImageBufferSurface(
    const IntSize& size,
    OpacityMode opacity_mode,
    ImageInitializationMode initialization_mode,
    const CanvasColorParams& color_params)
    : ImageBufferSurface(size, opacity_mode, color_params) {
  SkAlphaType alpha_type =
      (kOpaque == opacity_mode) ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
  SkImageInfo info = SkImageInfo::Make(
      size.Width(), size.Height(), color_params.GetSkColorType(), alpha_type,
      color_params.GetSkColorSpaceForSkSurfaces());
  SkSurfaceProps disable_lcd_props(0, kUnknown_SkPixelGeometry);
  surface_ = SkSurface::MakeRaster(
      info, kOpaque == opacity_mode ? 0 : &disable_lcd_props);

  if (!surface_)
    return;

  // Always save an initial frame, to support resetting the top level matrix
  // and clip.
  canvas_ = WTF::WrapUnique(new SkiaPaintCanvas(
      surface_->getCanvas(),
      RuntimeEnabledFeatures::ColorCorrectRenderingEnabled() &&
              color_params.UsesOutputSpaceBlending()
          ? color_params.GetSkColorSpace()
          : nullptr));
  canvas_->save();

  if (initialization_mode == kInitializeImagePixels)
    Clear();
}

UnacceleratedImageBufferSurface::~UnacceleratedImageBufferSurface() {}

PaintCanvas* UnacceleratedImageBufferSurface::Canvas() {
  return canvas_.get();
}

bool UnacceleratedImageBufferSurface::IsValid() const {
  return surface_;
}

bool UnacceleratedImageBufferSurface::WritePixels(const SkImageInfo& orig_info,
                                                  const void* pixels,
                                                  size_t row_bytes,
                                                  int x,
                                                  int y) {
  return surface_->getCanvas()->writePixels(orig_info, pixels, row_bytes, x, y);
}

RefPtr<StaticBitmapImage> UnacceleratedImageBufferSurface::NewImageSnapshot(
    AccelerationHint,
    SnapshotReason) {
  return StaticBitmapImage::Create(surface_->makeImageSnapshot());
}

}  // namespace blink
