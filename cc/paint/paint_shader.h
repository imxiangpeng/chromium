// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PAINT_PAINT_SHADER_H_
#define CC_PAINT_PAINT_SHADER_H_

#include <memory>
#include <vector>

#include "base/optional.h"
#include "cc/paint/paint_export.h"
#include "cc/paint/paint_image.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkShader.h"

namespace cc {

class PaintOpBuffer;
using PaintRecord = PaintOpBuffer;

class CC_PAINT_EXPORT PaintShader : public SkRefCnt {
 public:
  enum class Type {
    kColor,
    kLinearGradient,
    kRadialGradient,
    kTwoPointConicalGradient,
    kSweepGradient,
    kImage,
    kPaintRecord,
    kShaderCount
  };

  // Scaling behavior dictates how a PaintRecord shader will behave. Use
  // RasterAtScale to create a picture shader. Use FixedScale to create an image
  // shader that is backed by the paint record.
  enum class ScalingBehavior { kRasterAtScale, kFixedScale };

  static sk_sp<PaintShader> MakeColor(SkColor color);

  static sk_sp<PaintShader> MakeLinearGradient(
      const SkPoint* points,
      const SkColor* colors,
      const SkScalar* pos,
      int count,
      SkShader::TileMode mode,
      uint32_t flags = 0,
      const SkMatrix* local_matrix = nullptr,
      SkColor fallback_color = SK_ColorTRANSPARENT);

  static sk_sp<PaintShader> MakeRadialGradient(
      const SkPoint& center,
      SkScalar radius,
      const SkColor colors[],
      const SkScalar pos[],
      int color_count,
      SkShader::TileMode mode,
      uint32_t flags = 0,
      const SkMatrix* local_matrix = nullptr,
      SkColor fallback_color = SK_ColorTRANSPARENT);

  static sk_sp<PaintShader> MakeTwoPointConicalGradient(
      const SkPoint& start,
      SkScalar start_radius,
      const SkPoint& end,
      SkScalar end_radius,
      const SkColor colors[],
      const SkScalar pos[],
      int color_count,
      SkShader::TileMode mode,
      uint32_t flags = 0,
      const SkMatrix* local_matrix = nullptr,
      SkColor fallback_color = SK_ColorTRANSPARENT);

  static sk_sp<PaintShader> MakeSweepGradient(
      SkScalar cx,
      SkScalar cy,
      const SkColor colors[],
      const SkScalar pos[],
      int color_count,
      uint32_t flags = 0,
      const SkMatrix* local_matrix = nullptr,
      SkColor fallback_color = SK_ColorTRANSPARENT);

  static sk_sp<PaintShader> MakeImage(const PaintImage& image,
                                      SkShader::TileMode tx,
                                      SkShader::TileMode ty,
                                      const SkMatrix* local_matrix);

  static sk_sp<PaintShader> MakePaintRecord(
      sk_sp<PaintRecord> record,
      const SkRect& tile,
      SkShader::TileMode tx,
      SkShader::TileMode ty,
      const SkMatrix* local_matrix,
      ScalingBehavior scaling_behavior = ScalingBehavior::kRasterAtScale);

  ~PaintShader() override;

  SkMatrix GetLocalMatrix() const {
    return local_matrix_ ? *local_matrix_ : SkMatrix::I();
  }
  Type shader_type() const { return shader_type_; }
  const PaintImage& paint_image() const {
    DCHECK_EQ(Type::kImage, shader_type_);
    return image_;
  }

  SkShader::TileMode tx() const { return tx_; }
  SkShader::TileMode ty() const { return ty_; }

  bool IsOpaque() const;

 private:
  friend class PaintFlags;

  explicit PaintShader(Type type);

  sk_sp<SkShader> GetSkShader() const;

  void SetColorsAndPositions(const SkColor* colors,
                             const SkScalar* positions,
                             int count);
  void SetMatrixAndTiling(const SkMatrix* matrix,
                          SkShader::TileMode tx,
                          SkShader::TileMode ty);
  void SetFlagsAndFallback(uint32_t flags, SkColor fallback_color);

  Type shader_type_ = Type::kShaderCount;

  uint32_t flags_ = 0;
  SkScalar end_radius_ = 0;
  SkScalar start_radius_ = 0;
  SkShader::TileMode tx_ = SkShader::kClamp_TileMode;
  SkShader::TileMode ty_ = SkShader::kClamp_TileMode;
  SkColor fallback_color_ = SK_ColorTRANSPARENT;
  ScalingBehavior scaling_behavior_ = ScalingBehavior::kRasterAtScale;

  base::Optional<SkMatrix> local_matrix_;
  SkPoint center_ = SkPoint::Make(0, 0);
  SkRect tile_ = SkRect::MakeEmpty();

  SkPoint start_point_ = SkPoint::Make(0, 0);
  SkPoint end_point_ = SkPoint::Make(0, 0);

  PaintImage image_;
  sk_sp<PaintRecord> record_;

  std::vector<SkColor> colors_;
  std::vector<SkScalar> positions_;

  mutable sk_sp<SkShader> cached_shader_;

  DISALLOW_COPY_AND_ASSIGN(PaintShader);
};

}  // namespace cc

#endif  // CC_PAINT_PAINT_SHADER_H_
