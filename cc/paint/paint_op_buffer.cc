// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/paint_op_buffer.h"

#include "base/containers/stack_container.h"
#include "base/memory/ptr_util.h"
#include "cc/paint/decoded_draw_image.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/image_provider.h"
#include "cc/paint/paint_op_reader.h"
#include "cc/paint/paint_op_writer.h"
#include "cc/paint/paint_record.h"
#include "third_party/skia/include/core/SkAnnotation.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace cc {
namespace {

bool IsImageShader(const PaintFlags& flags) {
  return flags.HasShader() &&
         flags.getShader()->shader_type() == PaintShader::Type::kImage;
}

bool IsImageOp(const PaintOp* op) {
  PaintOpType type = static_cast<PaintOpType>(op->type);

  if (type == PaintOpType::DrawImage)
    return true;
  else if (type == PaintOpType::DrawImageRect)
    return true;
  else if (op->IsDrawOp() && op->IsPaintOpWithFlags())
    return IsImageShader(static_cast<const PaintOpWithFlags*>(op)->flags);

  return false;
}

bool QuickRejectDraw(const PaintOp* op, const SkCanvas* canvas) {
  DCHECK(op->IsDrawOp());

  SkRect rect;
  if (!PaintOp::GetBounds(op, &rect))
    return false;

  if (op->IsPaintOpWithFlags()) {
    SkPaint paint = static_cast<const PaintOpWithFlags*>(op)->flags.ToSkPaint();
    if (!paint.canComputeFastBounds())
      return false;
    paint.computeFastBounds(rect, &rect);
  }

  return canvas->quickReject(rect);
}

// Encapsulates a ImageProvider::DecodedImageHolder and a SkPaint. Use of
// this class ensures that the DecodedImageHolder outlives the dependent
// SkPaint.
class ScopedImageFlags {
 public:
  ScopedImageFlags(ImageProvider* image_provider,
                   const PaintFlags& flags,
                   const SkMatrix& ctm)
      : decoded_flags_(flags) {
    DCHECK(IsImageShader(flags));

    // Remove the original shader from the flags. In case we fail to decode the
    // image, the shader should be removed.
    decoded_flags_.setShader(nullptr);

    const PaintImage& paint_image = flags.getShader()->paint_image();
    SkMatrix matrix = flags.getShader()->GetLocalMatrix();

    SkMatrix total_image_matrix = matrix;
    total_image_matrix.preConcat(ctm);
    SkRect src_rect =
        SkRect::MakeIWH(paint_image.width(), paint_image.height());
    scoped_decoded_draw_image_ = image_provider->GetDecodedDrawImage(
        paint_image, src_rect, flags.getFilterQuality(), total_image_matrix);

    if (!scoped_decoded_draw_image_)
      return;
    const auto& decoded_image = scoped_decoded_draw_image_.decoded_image();
    DCHECK(decoded_image.image());

    bool need_scale = !decoded_image.is_scale_adjustment_identity();
    if (need_scale) {
      matrix.preScale(1.f / decoded_image.scale_adjustment().width(),
                      1.f / decoded_image.scale_adjustment().height());
    }

    sk_sp<SkImage> sk_image =
        sk_ref_sp<SkImage>(const_cast<SkImage*>(decoded_image.image().get()));
    PaintImage decoded_paint_image =
        paint_image.CloneWithSkImage(std::move(sk_image));
    decoded_flags_.setFilterQuality(decoded_image.filter_quality());
    decoded_flags_.setShader(
        PaintShader::MakeImage(decoded_paint_image, flags.getShader()->tx(),
                               flags.getShader()->ty(), &matrix));
  }

  PaintFlags* decoded_flags() { return &decoded_flags_; }

  ~ScopedImageFlags() = default;

 private:
  PaintFlags decoded_flags_;
  ImageProvider::ScopedDecodedDrawImage scoped_decoded_draw_image_;

  DISALLOW_COPY_AND_ASSIGN(ScopedImageFlags);
};

void RasterWithAlpha(const PaintOp* op,
                     SkCanvas* canvas,
                     const PlaybackParams& params,
                     const SkRect& bounds,
                     uint8_t alpha) {
  DCHECK(op->IsDrawOp());
  DCHECK_NE(static_cast<PaintOpType>(op->type), PaintOpType::DrawRecord);

  // TODO(enne): partially specialize RasterWithAlpha for draw color?
  if (op->IsPaintOpWithFlags()) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);

    // Replace the PaintFlags with a copy that holds the decoded image from the
    // ImageProvider if it consists of an image shader.
    base::Optional<ScopedImageFlags> scoped_flags;
    const PaintFlags* decoded_flags = &flags_op->flags;
    if (params.image_provider && IsImageShader(flags_op->flags)) {
      scoped_flags.emplace(params.image_provider, flags_op->flags,
                           canvas->getTotalMatrix());
      decoded_flags = scoped_flags.value().decoded_flags();
    }

    if (!decoded_flags->SupportsFoldingAlpha()) {
      bool unset = bounds.x() == PaintOp::kUnsetRect.x();
      canvas->saveLayerAlpha(unset ? nullptr : &bounds, alpha);
      flags_op->RasterWithFlags(canvas, decoded_flags, params);
      canvas->restore();
    } else if (alpha == 255) {
      flags_op->RasterWithFlags(canvas, decoded_flags, params);
    } else {
      if (scoped_flags.has_value()) {
        // If we already made a copy, just use that to override the alpha
        // instead of making another copy.
        PaintFlags* decoded_flags = scoped_flags.value().decoded_flags();
        decoded_flags->setAlpha(
            SkMulDiv255Round(decoded_flags->getAlpha(), alpha));
        flags_op->RasterWithFlags(canvas, decoded_flags, params);
      } else {
        PaintFlags alpha_flags = flags_op->flags;
        alpha_flags.setAlpha(SkMulDiv255Round(alpha_flags.getAlpha(), alpha));
        flags_op->RasterWithFlags(canvas, &alpha_flags, params);
      }
    }
  } else {
    bool unset = bounds.x() == PaintOp::kUnsetRect.x();
    canvas->saveLayerAlpha(unset ? nullptr : &bounds, alpha);
    op->Raster(canvas, params);
    canvas->restore();
  }
}

}  // namespace

#define TYPES(M)           \
  M(AnnotateOp)            \
  M(ClipPathOp)            \
  M(ClipRectOp)            \
  M(ClipRRectOp)           \
  M(ConcatOp)              \
  M(DrawArcOp)             \
  M(DrawCircleOp)          \
  M(DrawColorOp)           \
  M(DrawDRRectOp)          \
  M(DrawImageOp)           \
  M(DrawImageRectOp)       \
  M(DrawIRectOp)           \
  M(DrawLineOp)            \
  M(DrawOvalOp)            \
  M(DrawPathOp)            \
  M(DrawPosTextOp)         \
  M(DrawRecordOp)          \
  M(DrawRectOp)            \
  M(DrawRRectOp)           \
  M(DrawTextOp)            \
  M(DrawTextBlobOp)        \
  M(NoopOp)                \
  M(RestoreOp)             \
  M(RotateOp)              \
  M(SaveOp)                \
  M(SaveLayerOp)           \
  M(SaveLayerAlphaOp)      \
  M(ScaleOp)               \
  M(SetMatrixOp)           \
  M(TranslateOp)

static constexpr size_t kNumOpTypes =
    static_cast<size_t>(PaintOpType::LastPaintOpType) + 1;

// Verify that every op is in the TYPES macro.
#define M(T) +1
static_assert(kNumOpTypes == TYPES(M), "Missing op in list");
#undef M

template <typename T, bool HasFlags>
struct Rasterizer {
  static void RasterWithFlags(const T* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const PlaybackParams& params) {
    static_assert(
        !T::kHasPaintFlags,
        "This function should not be used for a PaintOp that has PaintFlags");
    NOTREACHED();
  }
  static void Raster(const T* op,
                     SkCanvas* canvas,
                     const PlaybackParams& params) {
    static_assert(
        !T::kHasPaintFlags,
        "This function should not be used for a PaintOp that has PaintFlags");
    T::Raster(op, canvas, params);
  }
};

template <typename T>
struct Rasterizer<T, true> {
  static void RasterWithFlags(const T* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const PlaybackParams& params) {
    static_assert(T::kHasPaintFlags,
                  "This function expects the PaintOp to have PaintFlags");
    T::RasterWithFlags(op, flags, canvas, params);
  }

  static void Raster(const T* op,
                     SkCanvas* canvas,
                     const PlaybackParams& params) {
    static_assert(T::kHasPaintFlags,
                  "This function expects the PaintOp to have PaintFlags");
    T::RasterWithFlags(op, &op->flags, canvas, params);
  }
};

using RasterFunction = void (*)(const PaintOp* op,
                                SkCanvas* canvas,
                                const PlaybackParams& params);
#define M(T)                                                              \
  [](const PaintOp* op, SkCanvas* canvas, const PlaybackParams& params) { \
    Rasterizer<T, T::kHasPaintFlags>::Raster(static_cast<const T*>(op),   \
                                             canvas, params);             \
  },
static const RasterFunction g_raster_functions[kNumOpTypes] = {TYPES(M)};
#undef M

using RasterWithFlagsFunction = void (*)(const PaintOp* op,
                                         const PaintFlags* flags,
                                         SkCanvas* canvas,
                                         const PlaybackParams& params);
#define M(T)                                                       \
  [](const PaintOp* op, const PaintFlags* flags, SkCanvas* canvas, \
     const PlaybackParams& params) {                               \
    Rasterizer<T, T::kHasPaintFlags>::RasterWithFlags(             \
        static_cast<const T*>(op), flags, canvas, params);         \
  },
static const RasterWithFlagsFunction
    g_raster_with_flags_functions[kNumOpTypes] = {TYPES(M)};
#undef M

using SerializeFunction = size_t (*)(const PaintOp* op,
                                     void* memory,
                                     size_t size,
                                     const PaintOp::SerializeOptions& options);
#define M(T) &T::Serialize,
static const SerializeFunction g_serialize_functions[kNumOpTypes] = {TYPES(M)};
#undef M

using DeserializeFunction = PaintOp* (*)(const void* input,
                                         size_t input_size,
                                         void* output,
                                         size_t output_size);

#define M(T) &T::Deserialize,
static const DeserializeFunction g_deserialize_functions[kNumOpTypes] = {
    TYPES(M)};
#undef M

// Most state ops (matrix, clip, save, restore) have a trivial destructor.
// TODO(enne): evaluate if we need the nullptr optimization or if
// we even need to differentiate trivial destructors here.
using VoidFunction = void (*)(PaintOp* op);
#define M(T)                                           \
  !std::is_trivially_destructible<T>::value            \
      ? [](PaintOp* op) { static_cast<T*>(op)->~T(); } \
      : static_cast<VoidFunction>(nullptr),
static const VoidFunction g_destructor_functions[kNumOpTypes] = {TYPES(M)};
#undef M

#define M(T) T::kIsDrawOp,
static bool g_is_draw_op[kNumOpTypes] = {TYPES(M)};
#undef M

#define M(T) T::kHasPaintFlags,
static bool g_has_paint_flags[kNumOpTypes] = {TYPES(M)};
#undef M

#define M(T)                                         \
  static_assert(sizeof(T) <= sizeof(LargestPaintOp), \
                #T " must be no bigger than LargestPaintOp");
TYPES(M);
#undef M

#define M(T)                                               \
  static_assert(alignof(T) <= PaintOpBuffer::PaintOpAlign, \
                #T " must have alignment no bigger than PaintOpAlign");
TYPES(M);
#undef M

#undef TYPES

const SkRect PaintOp::kUnsetRect = {SK_ScalarInfinity, 0, 0, 0};
const size_t PaintOp::kMaxSkip;

std::string PaintOpTypeToString(PaintOpType type) {
  switch (type) {
    case PaintOpType::Annotate:
      return "Annotate";
    case PaintOpType::ClipPath:
      return "ClipPath";
    case PaintOpType::ClipRect:
      return "ClipRect";
    case PaintOpType::ClipRRect:
      return "ClipRRect";
    case PaintOpType::Concat:
      return "Concat";
    case PaintOpType::DrawArc:
      return "DrawArc";
    case PaintOpType::DrawCircle:
      return "DrawCircle";
    case PaintOpType::DrawColor:
      return "DrawColor";
    case PaintOpType::DrawDRRect:
      return "DrawDRRect";
    case PaintOpType::DrawImage:
      return "DrawImage";
    case PaintOpType::DrawImageRect:
      return "DrawImageRect";
    case PaintOpType::DrawIRect:
      return "DrawIRect";
    case PaintOpType::DrawLine:
      return "DrawLine";
    case PaintOpType::DrawOval:
      return "DrawOval";
    case PaintOpType::DrawPath:
      return "DrawPath";
    case PaintOpType::DrawPosText:
      return "DrawPosText";
    case PaintOpType::DrawRecord:
      return "DrawRecord";
    case PaintOpType::DrawRect:
      return "DrawRect";
    case PaintOpType::DrawRRect:
      return "DrawRRect";
    case PaintOpType::DrawText:
      return "DrawText";
    case PaintOpType::DrawTextBlob:
      return "DrawTextBlob";
    case PaintOpType::Noop:
      return "Noop";
    case PaintOpType::Restore:
      return "Restore";
    case PaintOpType::Rotate:
      return "Rotate";
    case PaintOpType::Save:
      return "Save";
    case PaintOpType::SaveLayer:
      return "SaveLayer";
    case PaintOpType::SaveLayerAlpha:
      return "SaveLayerAlpha";
    case PaintOpType::Scale:
      return "Scale";
    case PaintOpType::SetMatrix:
      return "SetMatrix";
    case PaintOpType::Translate:
      return "Translate";
  }
  return "UNKNOWN";
}

template <typename T>
size_t SimpleSerialize(const PaintOp* op, void* memory, size_t size) {
  if (sizeof(T) > size)
    return 0;
  memcpy(memory, op, sizeof(T));
  return sizeof(T);
}

PlaybackParams::PlaybackParams(ImageProvider* image_provider,
                               const SkMatrix& original_ctm)
    : image_provider(image_provider), original_ctm(original_ctm) {}

size_t AnnotateOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const AnnotateOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->annotation_type);
  helper.Write(op->rect);
  helper.Write(op->data);
  return helper.size();
}

size_t ClipPathOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const ClipPathOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->path);
  helper.Write(op->op);
  helper.Write(op->antialias);
  return helper.size();
}

size_t ClipRectOp::Serialize(const PaintOp* op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  return SimpleSerialize<ClipRectOp>(op, memory, size);
}

size_t ClipRRectOp::Serialize(const PaintOp* op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  return SimpleSerialize<ClipRRectOp>(op, memory, size);
}

size_t ConcatOp::Serialize(const PaintOp* op,
                           void* memory,
                           size_t size,
                           const SerializeOptions& options) {
  return SimpleSerialize<ConcatOp>(op, memory, size);
}

size_t DrawArcOp::Serialize(const PaintOp* base_op,
                            void* memory,
                            size_t size,
                            const SerializeOptions& options) {
  auto* op = static_cast<const DrawArcOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->oval);
  helper.Write(op->start_angle);
  helper.Write(op->sweep_angle);
  helper.Write(op->use_center);
  return helper.size();
}

size_t DrawCircleOp::Serialize(const PaintOp* base_op,
                               void* memory,
                               size_t size,
                               const SerializeOptions& options) {
  auto* op = static_cast<const DrawCircleOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->cx);
  helper.Write(op->cy);
  helper.Write(op->radius);
  return helper.size();
}

size_t DrawColorOp::Serialize(const PaintOp* op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  return SimpleSerialize<DrawColorOp>(op, memory, size);
}

size_t DrawDRRectOp::Serialize(const PaintOp* base_op,
                               void* memory,
                               size_t size,
                               const SerializeOptions& options) {
  auto* op = static_cast<const DrawDRRectOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->outer);
  helper.Write(op->inner);
  return helper.size();
}

size_t DrawImageOp::Serialize(const PaintOp* base_op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  auto* op = static_cast<const DrawImageOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->image, options.decode_cache);
  helper.Write(op->left);
  helper.Write(op->top);
  return helper.size();
}

size_t DrawImageRectOp::Serialize(const PaintOp* base_op,
                                  void* memory,
                                  size_t size,
                                  const SerializeOptions& options) {
  auto* op = static_cast<const DrawImageRectOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->image, options.decode_cache);
  helper.Write(op->src);
  helper.Write(op->dst);
  helper.Write(op->constraint);
  return helper.size();
}

size_t DrawIRectOp::Serialize(const PaintOp* base_op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  auto* op = static_cast<const DrawIRectOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->rect);
  return helper.size();
}

size_t DrawLineOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const DrawLineOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->x0);
  helper.Write(op->y0);
  helper.Write(op->x1);
  helper.Write(op->y1);
  return helper.size();
}

size_t DrawOvalOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const DrawOvalOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->oval);
  return helper.size();
}

size_t DrawPathOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const DrawPathOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->path);
  return helper.size();
}

size_t DrawPosTextOp::Serialize(const PaintOp* base_op,
                                void* memory,
                                size_t size,
                                const SerializeOptions& options) {
  auto* op = static_cast<const DrawPosTextOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->count);
  helper.Write(op->bytes);
  helper.WriteArray(op->count, op->GetArray());
  helper.WriteData(op->bytes, op->GetData());
  return helper.size();
}

size_t DrawRecordOp::Serialize(const PaintOp* op,
                               void* memory,
                               size_t size,
                               const SerializeOptions& options) {
  // TODO(enne): these must be flattened.  Serializing this will not do
  // anything.
  NOTREACHED();
  return 0u;
}

size_t DrawRectOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const DrawRectOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->rect);
  return helper.size();
}

size_t DrawRRectOp::Serialize(const PaintOp* base_op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  auto* op = static_cast<const DrawRRectOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->rrect);
  return helper.size();
}

size_t DrawTextOp::Serialize(const PaintOp* base_op,
                             void* memory,
                             size_t size,
                             const SerializeOptions& options) {
  auto* op = static_cast<const DrawTextOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->x);
  helper.Write(op->y);
  helper.Write(op->bytes);
  helper.WriteData(op->bytes, op->GetData());
  return helper.size();
}

size_t DrawTextBlobOp::Serialize(const PaintOp* base_op,
                                 void* memory,
                                 size_t size,
                                 const SerializeOptions& options) {
  auto* op = static_cast<const DrawTextBlobOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->x);
  helper.Write(op->y);
  helper.Write(op->blob);
  return helper.size();
}

size_t NoopOp::Serialize(const PaintOp* op,
                         void* memory,
                         size_t size,
                         const SerializeOptions& options) {
  return SimpleSerialize<NoopOp>(op, memory, size);
}

size_t RestoreOp::Serialize(const PaintOp* op,
                            void* memory,
                            size_t size,
                            const SerializeOptions& options) {
  return SimpleSerialize<RestoreOp>(op, memory, size);
}

size_t RotateOp::Serialize(const PaintOp* op,
                           void* memory,
                           size_t size,
                           const SerializeOptions& options) {
  return SimpleSerialize<RotateOp>(op, memory, size);
}

size_t SaveOp::Serialize(const PaintOp* op,
                         void* memory,
                         size_t size,
                         const SerializeOptions& options) {
  return SimpleSerialize<SaveOp>(op, memory, size);
}

size_t SaveLayerOp::Serialize(const PaintOp* base_op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  auto* op = static_cast<const SaveLayerOp*>(base_op);
  PaintOpWriter helper(memory, size);
  helper.Write(op->flags);
  helper.Write(op->bounds);
  return helper.size();
}

size_t SaveLayerAlphaOp::Serialize(const PaintOp* op,
                                   void* memory,
                                   size_t size,
                                   const SerializeOptions& options) {
  return SimpleSerialize<SaveLayerAlphaOp>(op, memory, size);
}

size_t ScaleOp::Serialize(const PaintOp* op,
                          void* memory,
                          size_t size,
                          const SerializeOptions& options) {
  return SimpleSerialize<ScaleOp>(op, memory, size);
}

size_t SetMatrixOp::Serialize(const PaintOp* op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  return SimpleSerialize<SetMatrixOp>(op, memory, size);
}

size_t TranslateOp::Serialize(const PaintOp* op,
                              void* memory,
                              size_t size,
                              const SerializeOptions& options) {
  return SimpleSerialize<TranslateOp>(op, memory, size);
}

template <typename T>
void UpdateTypeAndSkip(T* op) {
  op->type = static_cast<uint8_t>(T::kType);
  op->skip = MathUtil::UncheckedRoundUp(sizeof(T), PaintOpBuffer::PaintOpAlign);
}

template <typename T>
T* SimpleDeserialize(const void* input,
                     size_t input_size,
                     void* output,
                     size_t output_size) {
  if (input_size < sizeof(T))
    return nullptr;
  memcpy(output, input, sizeof(T));

  T* op = reinterpret_cast<T*>(output);
  // Type and skip were already read once, so could have been changed.
  // Don't trust them and clobber them with something valid.
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* AnnotateOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(AnnotateOp));
  AnnotateOp* op = new (output) AnnotateOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->annotation_type);
  helper.Read(&op->rect);
  helper.Read(&op->data);
  if (!helper.valid()) {
    op->~AnnotateOp();
    return nullptr;
  }

  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* ClipPathOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(ClipPathOp));
  ClipPathOp* op = new (output) ClipPathOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->path);
  helper.Read(&op->op);
  helper.Read(&op->antialias);
  if (!helper.valid() || !IsValidSkClipOp(op->op)) {
    op->~ClipPathOp();
    return nullptr;
  }

  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* ClipRectOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  ClipRectOp* op =
      SimpleDeserialize<ClipRectOp>(input, input_size, output, output_size);
  return op && IsValidSkClipOp(op->op) ? op : nullptr;
}

PaintOp* ClipRRectOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  ClipRRectOp* op =
      SimpleDeserialize<ClipRRectOp>(input, input_size, output, output_size);
  return op && IsValidSkClipOp(op->op) ? op : nullptr;
}

PaintOp* ConcatOp::Deserialize(const void* input,
                               size_t input_size,
                               void* output,
                               size_t output_size) {
  return SimpleDeserialize<ConcatOp>(input, input_size, output, output_size);
}

PaintOp* DrawArcOp::Deserialize(const void* input,
                                size_t input_size,
                                void* output,
                                size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawArcOp));
  DrawArcOp* op = new (output) DrawArcOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->oval);
  helper.Read(&op->start_angle);
  helper.Read(&op->sweep_angle);
  helper.Read(&op->use_center);
  if (!helper.valid()) {
    op->~DrawArcOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawCircleOp::Deserialize(const void* input,
                                   size_t input_size,
                                   void* output,
                                   size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawCircleOp));
  DrawCircleOp* op = new (output) DrawCircleOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->cx);
  helper.Read(&op->cy);
  helper.Read(&op->radius);
  if (!helper.valid()) {
    op->~DrawCircleOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawColorOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  DrawColorOp* op =
      SimpleDeserialize<DrawColorOp>(input, input_size, output, output_size);
  return op && IsValidDrawColorSkBlendMode(op->mode) ? op : nullptr;
}

PaintOp* DrawDRRectOp::Deserialize(const void* input,
                                   size_t input_size,
                                   void* output,
                                   size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawDRRectOp));
  DrawDRRectOp* op = new (output) DrawDRRectOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->outer);
  helper.Read(&op->inner);
  if (!helper.valid()) {
    op->~DrawDRRectOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawImageOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawImageOp));
  DrawImageOp* op = new (output) DrawImageOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->image);
  helper.Read(&op->left);
  helper.Read(&op->top);
  if (!helper.valid()) {
    op->~DrawImageOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawImageRectOp::Deserialize(const void* input,
                                      size_t input_size,
                                      void* output,
                                      size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawImageRectOp));
  DrawImageRectOp* op = new (output) DrawImageRectOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->image);
  helper.Read(&op->src);
  helper.Read(&op->dst);
  helper.Read(&op->constraint);
  if (!helper.valid()) {
    op->~DrawImageRectOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawIRectOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawIRectOp));
  DrawIRectOp* op = new (output) DrawIRectOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->rect);
  if (!helper.valid()) {
    op->~DrawIRectOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawLineOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawLineOp));
  DrawLineOp* op = new (output) DrawLineOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->x0);
  helper.Read(&op->y0);
  helper.Read(&op->x1);
  helper.Read(&op->y1);
  if (!helper.valid()) {
    op->~DrawLineOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawOvalOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawOvalOp));
  DrawOvalOp* op = new (output) DrawOvalOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->oval);
  if (!helper.valid()) {
    op->~DrawOvalOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawPathOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawPathOp));
  DrawPathOp* op = new (output) DrawPathOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->path);
  if (!helper.valid()) {
    op->~DrawPathOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawPosTextOp::Deserialize(const void* input,
                                    size_t input_size,
                                    void* output,
                                    size_t output_size) {
  // TODO(enne): This is a bit of a weird condition, but to avoid the code
  // complexity of every Deserialize function being able to (re)allocate
  // an aligned buffer of the right size, this function asserts that it
  // will have enough size for the extra data.  It's guaranteed that any extra
  // memory is at most |input_size| so that plus the op size is an upper bound.
  // The caller has to awkwardly do this allocation though, sorry.
  CHECK_GE(output_size, sizeof(DrawPosTextOp) + input_size);
  DrawPosTextOp* op = new (output) DrawPosTextOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->count);
  helper.Read(&op->bytes);
  if (helper.valid()) {
    helper.ReadArray(op->count, op->GetArray());
    helper.ReadData(op->bytes, op->GetData());
  }
  if (!helper.valid()) {
    op->~DrawPosTextOp();
    return nullptr;
  }

  op->type = static_cast<uint8_t>(PaintOpType::DrawPosText);
  op->skip = MathUtil::UncheckedRoundUp(
      sizeof(DrawPosTextOp) + op->bytes + sizeof(SkPoint) * op->count,
      PaintOpBuffer::PaintOpAlign);

  return op;
}

PaintOp* DrawRecordOp::Deserialize(const void* input,
                                   size_t input_size,
                                   void* output,
                                   size_t output_size) {
  // TODO(enne): these must be flattened and not sent directly.
  // TODO(enne): could also consider caching these service side.
  return nullptr;
}

PaintOp* DrawRectOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawRectOp));
  DrawRectOp* op = new (output) DrawRectOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->rect);
  if (!helper.valid()) {
    op->~DrawRectOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawRRectOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawRRectOp));
  DrawRRectOp* op = new (output) DrawRRectOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->rrect);
  if (!helper.valid()) {
    op->~DrawRRectOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* DrawTextOp::Deserialize(const void* input,
                                 size_t input_size,
                                 void* output,
                                 size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawTextOp) + input_size);
  DrawTextOp* op = new (output) DrawTextOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->x);
  helper.Read(&op->y);
  helper.Read(&op->bytes);
  if (helper.valid())
    helper.ReadData(op->bytes, op->GetData());
  if (!helper.valid()) {
    op->~DrawTextOp();
    return nullptr;
  }

  op->type = static_cast<uint8_t>(PaintOpType::DrawText);
  op->skip = MathUtil::UncheckedRoundUp(sizeof(DrawTextOp) + op->bytes,
                                        PaintOpBuffer::PaintOpAlign);
  return op;
}

PaintOp* DrawTextBlobOp::Deserialize(const void* input,
                                     size_t input_size,
                                     void* output,
                                     size_t output_size) {
  CHECK_GE(output_size, sizeof(DrawTextBlobOp));
  DrawTextBlobOp* op = new (output) DrawTextBlobOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->x);
  helper.Read(&op->y);
  helper.Read(&op->blob);
  if (!helper.valid()) {
    op->~DrawTextBlobOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* NoopOp::Deserialize(const void* input,
                             size_t input_size,
                             void* output,
                             size_t output_size) {
  return SimpleDeserialize<NoopOp>(input, input_size, output, output_size);
}

PaintOp* RestoreOp::Deserialize(const void* input,
                                size_t input_size,
                                void* output,
                                size_t output_size) {
  return SimpleDeserialize<RestoreOp>(input, input_size, output, output_size);
}

PaintOp* RotateOp::Deserialize(const void* input,
                               size_t input_size,
                               void* output,
                               size_t output_size) {
  return SimpleDeserialize<RotateOp>(input, input_size, output, output_size);
}

PaintOp* SaveOp::Deserialize(const void* input,
                             size_t input_size,
                             void* output,
                             size_t output_size) {
  return SimpleDeserialize<SaveOp>(input, input_size, output, output_size);
}

PaintOp* SaveLayerOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  CHECK_GE(output_size, sizeof(SaveLayerOp));
  SaveLayerOp* op = new (output) SaveLayerOp;

  PaintOpReader helper(input, input_size);
  helper.Read(&op->flags);
  helper.Read(&op->bounds);
  if (!helper.valid()) {
    op->~SaveLayerOp();
    return nullptr;
  }
  UpdateTypeAndSkip(op);
  return op;
}

PaintOp* SaveLayerAlphaOp::Deserialize(const void* input,
                                       size_t input_size,
                                       void* output,
                                       size_t output_size) {
  return SimpleDeserialize<SaveLayerAlphaOp>(input, input_size, output,
                                             output_size);
}

PaintOp* ScaleOp::Deserialize(const void* input,
                              size_t input_size,
                              void* output,
                              size_t output_size) {
  return SimpleDeserialize<ScaleOp>(input, input_size, output, output_size);
}

PaintOp* SetMatrixOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  return SimpleDeserialize<SetMatrixOp>(input, input_size, output, output_size);
}

PaintOp* TranslateOp::Deserialize(const void* input,
                                  size_t input_size,
                                  void* output,
                                  size_t output_size) {
  return SimpleDeserialize<TranslateOp>(input, input_size, output, output_size);
}

void AnnotateOp::Raster(const AnnotateOp* op,
                        SkCanvas* canvas,
                        const PlaybackParams& params) {
  switch (op->annotation_type) {
    case PaintCanvas::AnnotationType::URL:
      SkAnnotateRectWithURL(canvas, op->rect, op->data.get());
      break;
    case PaintCanvas::AnnotationType::LINK_TO_DESTINATION:
      SkAnnotateLinkToDestination(canvas, op->rect, op->data.get());
      break;
    case PaintCanvas::AnnotationType::NAMED_DESTINATION: {
      SkPoint point = SkPoint::Make(op->rect.x(), op->rect.y());
      SkAnnotateNamedDestination(canvas, point, op->data.get());
      break;
    }
  }
}

void ClipPathOp::Raster(const ClipPathOp* op,
                        SkCanvas* canvas,
                        const PlaybackParams& params) {
  canvas->clipPath(op->path, op->op, op->antialias);
}

void ClipRectOp::Raster(const ClipRectOp* op,
                        SkCanvas* canvas,
                        const PlaybackParams& params) {
  canvas->clipRect(op->rect, op->op, op->antialias);
}

void ClipRRectOp::Raster(const ClipRRectOp* op,
                         SkCanvas* canvas,
                         const PlaybackParams& params) {
  canvas->clipRRect(op->rrect, op->op, op->antialias);
}

void ConcatOp::Raster(const ConcatOp* op,
                      SkCanvas* canvas,
                      const PlaybackParams& params) {
  canvas->concat(op->matrix);
}

void DrawArcOp::RasterWithFlags(const DrawArcOp* op,
                                const PaintFlags* flags,
                                SkCanvas* canvas,
                                const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawArc(op->oval, op->start_angle, op->sweep_angle, op->use_center,
                  paint);
}

void DrawCircleOp::RasterWithFlags(const DrawCircleOp* op,
                                   const PaintFlags* flags,
                                   SkCanvas* canvas,
                                   const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawCircle(op->cx, op->cy, op->radius, paint);
}

void DrawColorOp::Raster(const DrawColorOp* op,
                         SkCanvas* canvas,
                         const PlaybackParams& params) {
  canvas->drawColor(op->color, op->mode);
}

void DrawDRRectOp::RasterWithFlags(const DrawDRRectOp* op,
                                   const PaintFlags* flags,
                                   SkCanvas* canvas,
                                   const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawDRRect(op->outer, op->inner, paint);
}

void DrawImageOp::RasterWithFlags(const DrawImageOp* op,
                                  const PaintFlags* flags,
                                  SkCanvas* canvas,
                                  const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();

  if (!params.image_provider) {
    canvas->drawImage(op->image.GetSkImage().get(), op->left, op->top, &paint);
    return;
  }

  SkRect image_rect = SkRect::MakeIWH(op->image.width(), op->image.height());
  auto scoped_decoded_draw_image = params.image_provider->GetDecodedDrawImage(
      op->image, image_rect,
      flags ? flags->getFilterQuality() : kNone_SkFilterQuality,
      canvas->getTotalMatrix());
  if (!scoped_decoded_draw_image)
    return;

  const auto& decoded_image = scoped_decoded_draw_image.decoded_image();
  DCHECK(decoded_image.image());

  DCHECK_EQ(0, static_cast<int>(decoded_image.src_rect_offset().width()));
  DCHECK_EQ(0, static_cast<int>(decoded_image.src_rect_offset().height()));
  bool need_scale = !decoded_image.is_scale_adjustment_identity();
  if (need_scale) {
    canvas->save();
    canvas->scale(1.f / (decoded_image.scale_adjustment().width()),
                  1.f / (decoded_image.scale_adjustment().height()));
    }

    paint.setFilterQuality(decoded_image.filter_quality());
    canvas->drawImage(decoded_image.image().get(), op->left, op->top, &paint);
    if (need_scale)
      canvas->restore();
}

void DrawImageRectOp::RasterWithFlags(const DrawImageRectOp* op,
                                      const PaintFlags* flags,
                                      SkCanvas* canvas,
                                      const PlaybackParams& params) {
  // TODO(enne): Probably PaintCanvas should just use the skia enum directly.
  SkCanvas::SrcRectConstraint skconstraint =
      static_cast<SkCanvas::SrcRectConstraint>(op->constraint);
  SkPaint paint = flags->ToSkPaint();

  if (!params.image_provider) {
    canvas->drawImageRect(op->image.GetSkImage().get(), op->src, op->dst,
                          &paint, skconstraint);
    return;
  }

  SkMatrix matrix;
  matrix.setRectToRect(op->src, op->dst, SkMatrix::kFill_ScaleToFit);
  matrix.postConcat(canvas->getTotalMatrix());

  auto scoped_decoded_draw_image = params.image_provider->GetDecodedDrawImage(
      op->image, op->src,
      flags ? flags->getFilterQuality() : kNone_SkFilterQuality, matrix);
  if (!scoped_decoded_draw_image)
    return;

  const auto& decoded_image = scoped_decoded_draw_image.decoded_image();
  DCHECK(decoded_image.image());

  SkRect adjusted_src =
      op->src.makeOffset(decoded_image.src_rect_offset().width(),
                         decoded_image.src_rect_offset().height());
  if (!decoded_image.is_scale_adjustment_identity()) {
    float x_scale = decoded_image.scale_adjustment().width();
    float y_scale = decoded_image.scale_adjustment().height();
    adjusted_src = SkRect::MakeXYWH(
        adjusted_src.x() * x_scale, adjusted_src.y() * y_scale,
        adjusted_src.width() * x_scale, adjusted_src.height() * y_scale);
    }

    paint.setFilterQuality(decoded_image.filter_quality());
    canvas->drawImageRect(decoded_image.image().get(), adjusted_src, op->dst,
                          &paint, skconstraint);
}

void DrawIRectOp::RasterWithFlags(const DrawIRectOp* op,
                                  const PaintFlags* flags,
                                  SkCanvas* canvas,
                                  const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawIRect(op->rect, paint);
}

void DrawLineOp::RasterWithFlags(const DrawLineOp* op,
                                 const PaintFlags* flags,
                                 SkCanvas* canvas,
                                 const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawLine(op->x0, op->y0, op->x1, op->y1, paint);
}

void DrawOvalOp::RasterWithFlags(const DrawOvalOp* op,
                                 const PaintFlags* flags,
                                 SkCanvas* canvas,
                                 const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawOval(op->oval, paint);
}

void DrawPathOp::RasterWithFlags(const DrawPathOp* op,
                                 const PaintFlags* flags,
                                 SkCanvas* canvas,
                                 const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawPath(op->path, paint);
}

void DrawPosTextOp::RasterWithFlags(const DrawPosTextOp* op,
                                    const PaintFlags* flags,
                                    SkCanvas* canvas,
                                    const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawPosText(op->GetData(), op->bytes, op->GetArray(), paint);
}

void DrawRecordOp::Raster(const DrawRecordOp* op,
                          SkCanvas* canvas,
                          const PlaybackParams& params) {
  // Don't use drawPicture here, as it adds an implicit clip.
  op->record->Playback(canvas, params.image_provider);
}

void DrawRectOp::RasterWithFlags(const DrawRectOp* op,
                                 const PaintFlags* flags,
                                 SkCanvas* canvas,
                                 const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawRect(op->rect, paint);
}

void DrawRRectOp::RasterWithFlags(const DrawRRectOp* op,
                                  const PaintFlags* flags,
                                  SkCanvas* canvas,
                                  const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawRRect(op->rrect, paint);
}

void DrawTextOp::RasterWithFlags(const DrawTextOp* op,
                                 const PaintFlags* flags,
                                 SkCanvas* canvas,
                                 const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawText(op->GetData(), op->bytes, op->x, op->y, paint);
}

void DrawTextBlobOp::RasterWithFlags(const DrawTextBlobOp* op,
                                     const PaintFlags* flags,
                                     SkCanvas* canvas,
                                     const PlaybackParams& params) {
  SkPaint paint = flags->ToSkPaint();
  canvas->drawTextBlob(op->blob.get(), op->x, op->y, paint);
}

void RestoreOp::Raster(const RestoreOp* op,
                       SkCanvas* canvas,
                       const PlaybackParams& params) {
  canvas->restore();
}

void RotateOp::Raster(const RotateOp* op,
                      SkCanvas* canvas,
                      const PlaybackParams& params) {
  canvas->rotate(op->degrees);
}

void SaveOp::Raster(const SaveOp* op,
                    SkCanvas* canvas,
                    const PlaybackParams& params) {
  canvas->save();
}

void SaveLayerOp::RasterWithFlags(const SaveLayerOp* op,
                                  const PaintFlags* flags,
                                  SkCanvas* canvas,
                                  const PlaybackParams& params) {
  // See PaintOp::kUnsetRect
  SkPaint paint = flags->ToSkPaint();
  bool unset = op->bounds.left() == SK_ScalarInfinity;
  canvas->saveLayer(unset ? nullptr : &op->bounds, &paint);
}

void SaveLayerAlphaOp::Raster(const SaveLayerAlphaOp* op,
                              SkCanvas* canvas,
                              const PlaybackParams& params) {
  // See PaintOp::kUnsetRect
  bool unset = op->bounds.left() == SK_ScalarInfinity;
  if (op->preserve_lcd_text_requests) {
    SkPaint paint;
    paint.setAlpha(op->alpha);
    canvas->saveLayerPreserveLCDTextRequests(unset ? nullptr : &op->bounds,
                                             &paint);
  } else {
    canvas->saveLayerAlpha(unset ? nullptr : &op->bounds, op->alpha);
  }
}

void ScaleOp::Raster(const ScaleOp* op,
                     SkCanvas* canvas,
                     const PlaybackParams& params) {
  canvas->scale(op->sx, op->sy);
}

void SetMatrixOp::Raster(const SetMatrixOp* op,
                         SkCanvas* canvas,
                         const PlaybackParams& params) {
  canvas->setMatrix(SkMatrix::Concat(params.original_ctm, op->matrix));
}

void TranslateOp::Raster(const TranslateOp* op,
                         SkCanvas* canvas,
                         const PlaybackParams& params) {
  canvas->translate(op->dx, op->dy);
}

bool PaintOp::IsDrawOp() const {
  return g_is_draw_op[type];
}

bool PaintOp::IsPaintOpWithFlags() const {
  return g_has_paint_flags[type];
}

void PaintOp::Raster(SkCanvas* canvas, const PlaybackParams& params) const {
  g_raster_functions[type](this, canvas, params);
}

size_t PaintOp::Serialize(void* memory,
                          size_t size,
                          const SerializeOptions& options) const {
  // Need at least enough room for a skip/type header.
  if (size < 4)
    return 0u;

  DCHECK_EQ(0u,
            reinterpret_cast<uintptr_t>(memory) % PaintOpBuffer::PaintOpAlign);

  size_t written = g_serialize_functions[type](this, memory, size, options);
  DCHECK_LE(written, size);
  if (written < 4)
    return 0u;

  size_t aligned_written =
      MathUtil::UncheckedRoundUp(written, PaintOpBuffer::PaintOpAlign);
  if (aligned_written >= kMaxSkip)
    return 0u;
  if (aligned_written > size)
    return 0u;

  // Update skip and type now that the size is known.
  uint32_t skip = static_cast<uint32_t>(aligned_written);
  static_cast<uint32_t*>(memory)[0] = type | skip << 8;
  return skip;
}

PaintOp* PaintOp::Deserialize(const void* input,
                              size_t input_size,
                              void* output,
                              size_t output_size) {
  // TODO(enne): assert that output_size is big enough.
  const PaintOp* serialized = reinterpret_cast<const PaintOp*>(input);
  uint32_t skip = serialized->skip;
  if (input_size < skip)
    return nullptr;
  if (skip % PaintOpBuffer::PaintOpAlign != 0)
    return nullptr;
  uint8_t type = serialized->type;
  if (type > static_cast<uint8_t>(PaintOpType::LastPaintOpType))
    return nullptr;

  return g_deserialize_functions[serialized->type](input, skip, output,
                                                   output_size);
}

// static
bool PaintOp::GetBounds(const PaintOp* op, SkRect* rect) {
  DCHECK(op->IsDrawOp());

  switch (op->GetType()) {
    case PaintOpType::DrawArc: {
      auto* arc_op = static_cast<const DrawArcOp*>(op);
      *rect = arc_op->oval;
      rect->sort();
      return true;
    }
    case PaintOpType::DrawCircle: {
      auto* circle_op = static_cast<const DrawCircleOp*>(op);
      *rect = SkRect::MakeXYWH(circle_op->cx - circle_op->radius,
                               circle_op->cy - circle_op->radius,
                               2 * circle_op->radius, 2 * circle_op->radius);
      rect->sort();
      return true;
    }
    case PaintOpType::DrawColor:
      return false;
    case PaintOpType::DrawDRRect: {
      auto* rect_op = static_cast<const DrawDRRectOp*>(op);
      *rect = rect_op->outer.getBounds();
      rect->sort();
      return true;
    }
    case PaintOpType::DrawImage: {
      auto* image_op = static_cast<const DrawImageOp*>(op);
      *rect =
          SkRect::MakeXYWH(image_op->left, image_op->top,
                           image_op->image.width(), image_op->image.height());
      rect->sort();
      return true;
    }
    case PaintOpType::DrawImageRect: {
      auto* image_rect_op = static_cast<const DrawImageRectOp*>(op);
      *rect = image_rect_op->dst;
      rect->sort();
      return true;
    }
    case PaintOpType::DrawIRect: {
      auto* rect_op = static_cast<const DrawIRectOp*>(op);
      *rect = SkRect::Make(rect_op->rect);
      rect->sort();
      return true;
    }
    case PaintOpType::DrawLine: {
      auto* line_op = static_cast<const DrawLineOp*>(op);
      rect->set(line_op->x0, line_op->y0, line_op->x1, line_op->y1);
      rect->sort();
      return true;
    }
    case PaintOpType::DrawOval: {
      auto* oval_op = static_cast<const DrawOvalOp*>(op);
      *rect = oval_op->oval;
      rect->sort();
      return true;
    }
    case PaintOpType::DrawPath: {
      auto* path_op = static_cast<const DrawPathOp*>(op);
      *rect = path_op->path.getBounds();
      rect->sort();
      return true;
    }
    case PaintOpType::DrawPosText:
      return false;
    case PaintOpType::DrawRect: {
      auto* rect_op = static_cast<const DrawRectOp*>(op);
      *rect = rect_op->rect;
      rect->sort();
      return true;
    }
    case PaintOpType::DrawRRect: {
      auto* rect_op = static_cast<const DrawRRectOp*>(op);
      *rect = rect_op->rrect.rect();
      rect->sort();
      return true;
    }
    case PaintOpType::DrawRecord:
      return false;
    case PaintOpType::DrawText:
      return false;
    case PaintOpType::DrawTextBlob: {
      auto* text_op = static_cast<const DrawTextBlobOp*>(op);
      *rect = text_op->blob->bounds().makeOffset(text_op->x, text_op->y);
      rect->sort();
      return true;
    }
    default:
      NOTREACHED();
  }
  return false;
}

void PaintOp::DestroyThis() {
  auto func = g_destructor_functions[type];
  if (func)
    func(this);
}

void PaintOpWithFlags::RasterWithFlags(SkCanvas* canvas,
                                       const PaintFlags* flags,
                                       const PlaybackParams& params) const {
  g_raster_with_flags_functions[type](this, flags, canvas, params);
}

int ClipPathOp::CountSlowPaths() const {
  return antialias && !path.isConvex() ? 1 : 0;
}

int DrawLineOp::CountSlowPaths() const {
  if (const SkPathEffect* effect = flags.getPathEffect().get()) {
    SkPathEffect::DashInfo info;
    SkPathEffect::DashType dashType = effect->asADash(&info);
    if (flags.getStrokeCap() != PaintFlags::kRound_Cap &&
        dashType == SkPathEffect::kDash_DashType && info.fCount == 2) {
      // The PaintFlags will count this as 1, so uncount that here as
      // this kind of line is special cased and not slow.
      return -1;
    }
  }
  return 0;
}

int DrawPathOp::CountSlowPaths() const {
  // This logic is copied from SkPathCounter instead of attempting to expose
  // that from Skia.
  if (!flags.isAntiAlias() || path.isConvex())
    return 0;

  PaintFlags::Style paintStyle = flags.getStyle();
  const SkRect& pathBounds = path.getBounds();
  if (paintStyle == PaintFlags::kStroke_Style && flags.getStrokeWidth() == 0) {
    // AA hairline concave path is not slow.
    return 0;
  } else if (paintStyle == PaintFlags::kFill_Style &&
             pathBounds.width() < 64.f && pathBounds.height() < 64.f &&
             !path.isVolatile()) {
    // AADF eligible concave path is not slow.
    return 0;
  } else {
    return 1;
  }
}

int DrawRecordOp::CountSlowPaths() const {
  return record->numSlowPaths();
}

bool DrawRecordOp::HasNonAAPaint() const {
  return record->HasNonAAPaint();
}

AnnotateOp::AnnotateOp() = default;

AnnotateOp::AnnotateOp(PaintCanvas::AnnotationType annotation_type,
                       const SkRect& rect,
                       sk_sp<SkData> data)
    : annotation_type(annotation_type), rect(rect), data(std::move(data)) {}

AnnotateOp::~AnnotateOp() = default;

DrawImageOp::DrawImageOp(const PaintImage& image,
                         SkScalar left,
                         SkScalar top,
                         const PaintFlags* flags)
    : PaintOpWithFlags(flags ? *flags : PaintFlags()),
      image(image),
      left(left),
      top(top) {}

bool DrawImageOp::HasDiscardableImages() const {
  // TODO(khushalsagar): Callers should not be able to change the lazy generated
  // state for a PaintImage.
  return image.IsLazyGenerated();
}

DrawImageOp::~DrawImageOp() = default;

DrawImageRectOp::DrawImageRectOp() = default;

DrawImageRectOp::DrawImageRectOp(const PaintImage& image,
                                 const SkRect& src,
                                 const SkRect& dst,
                                 const PaintFlags* flags,
                                 PaintCanvas::SrcRectConstraint constraint)
    : PaintOpWithFlags(flags ? *flags : PaintFlags()),
      image(image),
      src(src),
      dst(dst),
      constraint(constraint) {}

bool DrawImageRectOp::HasDiscardableImages() const {
  return image.IsLazyGenerated();
}

DrawImageRectOp::~DrawImageRectOp() = default;

DrawPosTextOp::DrawPosTextOp() = default;

DrawPosTextOp::DrawPosTextOp(size_t bytes,
                             size_t count,
                             const PaintFlags& flags)
    : PaintOpWithArray(flags, bytes, count) {}

DrawPosTextOp::~DrawPosTextOp() = default;

DrawRecordOp::DrawRecordOp() = default;

DrawRecordOp::DrawRecordOp(sk_sp<const PaintRecord> record)
    : record(std::move(record)) {}

DrawRecordOp::~DrawRecordOp() = default;

size_t DrawRecordOp::AdditionalBytesUsed() const {
  return record->bytes_used();
}

bool DrawRecordOp::HasDiscardableImages() const {
  return record->HasDiscardableImages();
}

DrawTextBlobOp::DrawTextBlobOp() = default;

DrawTextBlobOp::DrawTextBlobOp(sk_sp<SkTextBlob> blob,
                               SkScalar x,
                               SkScalar y,
                               const PaintFlags& flags)
    : PaintOpWithFlags(flags), blob(std::move(blob)), x(x), y(y) {}

DrawTextBlobOp::~DrawTextBlobOp() = default;

PaintOpBuffer::CompositeIterator::CompositeIterator(
    const PaintOpBuffer* buffer,
    const std::vector<size_t>* offsets)
    : using_offsets_(!!offsets) {
  if (using_offsets_)
    offset_iter_.emplace(buffer, offsets);
  else
    iter_.emplace(buffer);
}

PaintOpBuffer::CompositeIterator::CompositeIterator(
    const CompositeIterator& other) = default;
PaintOpBuffer::CompositeIterator::CompositeIterator(CompositeIterator&& other) =
    default;

PaintOpBuffer::PaintOpBuffer()
    : has_non_aa_paint_(false), has_discardable_images_(false) {}

PaintOpBuffer::PaintOpBuffer(PaintOpBuffer&& other) {
  *this = std::move(other);
}

PaintOpBuffer::~PaintOpBuffer() {
  Reset();
}

void PaintOpBuffer::operator=(PaintOpBuffer&& other) {
  data_ = std::move(other.data_);
  used_ = other.used_;
  reserved_ = other.reserved_;
  op_count_ = other.op_count_;
  num_slow_paths_ = other.num_slow_paths_;
  subrecord_bytes_used_ = other.subrecord_bytes_used_;
  has_non_aa_paint_ = other.has_non_aa_paint_;
  has_discardable_images_ = other.has_discardable_images_;

  // Make sure the other pob can destruct safely.
  other.used_ = 0;
  other.op_count_ = 0;
  other.reserved_ = 0;
}

void PaintOpBuffer::Reset() {
  for (auto* op : Iterator(this))
    op->DestroyThis();

  // Leave data_ allocated, reserved_ unchanged. ShrinkToFit will take care of
  // that if called.
  used_ = 0;
  op_count_ = 0;
  num_slow_paths_ = 0;
  has_non_aa_paint_ = false;
  subrecord_bytes_used_ = 0;
  has_discardable_images_ = false;
}

// When |op| is a nested PaintOpBuffer, this returns the PaintOp inside
// that buffer if the buffer contains a single drawing op, otherwise it
// returns null. This searches recursively if the PaintOpBuffer contains only
// another PaintOpBuffer.
static const PaintOp* GetNestedSingleDrawingOp(const PaintOp* op) {
  if (!op->IsDrawOp())
    return nullptr;

  while (op->GetType() == PaintOpType::DrawRecord) {
    auto* draw_record_op = static_cast<const DrawRecordOp*>(op);
    if (draw_record_op->record->size() > 1) {
      // If there's more than one op, then we need to keep the
      // SaveLayer.
      return nullptr;
    }

    // Recurse into the single-op DrawRecordOp and make sure it's a
    // drawing op.
    op = draw_record_op->record->GetFirstOp();
    if (!op->IsDrawOp())
      return nullptr;
  }

  return op;
}

void PaintOpBuffer::Playback(SkCanvas* canvas,
                             ImageProvider* image_provider,
                             SkPicture::AbortCallback* callback) const {
  Playback(canvas, image_provider, callback, nullptr);
}

void PaintOpBuffer::Playback(SkCanvas* canvas,
                             ImageProvider* image_provider,
                             SkPicture::AbortCallback* callback,
                             const std::vector<size_t>* offsets) const {
  if (!op_count_)
    return;
  if (offsets && offsets->empty())
    return;

  // Prevent PaintOpBuffers from having side effects back into the canvas.
  SkAutoCanvasRestore save_restore(canvas, true);

  // TODO(enne): a PaintRecord that contains a SetMatrix assumes that the
  // SetMatrix is local to that PaintRecord itself.  Said differently, if you
  // translate(x, y), then draw a paint record with a SetMatrix(identity),
  // the translation should be preserved instead of clobbering the top level
  // transform.  This could probably be done more efficiently.
  PlaybackParams params(image_provider, canvas->getTotalMatrix());

  // FIFO queue of paint ops that have been peeked at.
  base::StackVector<const PaintOp*, 3> stack;
  CompositeIterator iter(this, offsets);
  auto next_op = [&stack, &iter]() -> const PaintOp* {
    if (stack->size()) {
      const PaintOp* op = stack->front();
      // Shift paintops forward.
      stack->erase(stack->begin());
      return op;
    }
    if (!iter)
      return nullptr;
    const PaintOp* op = *iter;
    ++iter;
    return op;
  };

  while (const PaintOp* op = next_op()) {
    // Check if we should abort. This should happen at the start of loop since
    // there are a couple of raster branches below, and we need to ensure that
    // we do this check after every one of them.
    if (callback && callback->abort())
      return;

    // Optimize out save/restores or save/draw/restore that can be a single
    // draw.  See also: similar code in SkRecordOpts.
    // TODO(enne): consider making this recursive?
    // TODO(enne): should we avoid this if the SaveLayerAlphaOp has bounds?
    if (op->GetType() == PaintOpType::SaveLayerAlpha) {
      const PaintOp* second = next_op();
      const PaintOp* third = nullptr;
      if (second) {
        if (second->GetType() == PaintOpType::Restore) {
          continue;
        }

        // Find a nested drawing PaintOp to replace |second| if possible, while
        // holding onto the pointer to |second| in case we can't find a nested
        // drawing op to replace it with.
        const PaintOp* draw_op = GetNestedSingleDrawingOp(second);

        if (draw_op) {
          // This is an optimization to replicate the behaviour in SkCanvas
          // which rejects ops that draw outside the current clip. In the
          // general case we defer this to the SkCanvas but if we will be
          // using an ImageProvider for pre-decoding images, we can save
          // performing an expensive decode that will never be rasterized.
          const bool skip_op = params.image_provider && IsImageOp(draw_op) &&
                               QuickRejectDraw(draw_op, canvas);
          if (skip_op) {
            // Now that we know this op will be skipped, we can push the save
            // layer op back to the stack and continue iterating .
            // In the case with the following list of ops:
            // [SaveLayer, DrawImage, DrawRect, Restore], where draw_op is the
            // DrawImage op, this starts the iteration again from SaveLayer and
            // eliminates the DrawImage op.
            DCHECK(stack->empty());
            stack->push_back(op);
            continue;
          }

          third = next_op();
          if (third && third->GetType() == PaintOpType::Restore) {
            auto* save_op = static_cast<const SaveLayerAlphaOp*>(op);
            RasterWithAlpha(draw_op, canvas, params, save_op->bounds,
                            save_op->alpha);
            continue;
          }
        }

        // Store deferred ops for later.
        stack->push_back(second);
        if (third)
          stack->push_back(third);
      }
    }

    if (params.image_provider && IsImageOp(op)) {
      if (QuickRejectDraw(op, canvas))
        continue;

      auto* flags_op = op->IsPaintOpWithFlags()
                           ? static_cast<const PaintOpWithFlags*>(op)
                           : nullptr;
      if (flags_op && IsImageShader(flags_op->flags)) {
        ScopedImageFlags scoped_flags(image_provider, flags_op->flags,
                                      canvas->getTotalMatrix());
        flags_op->RasterWithFlags(canvas, scoped_flags.decoded_flags(), params);
        continue;
      }
    }

    // TODO(enne): skip SaveLayer followed by restore with nothing in
    // between, however SaveLayer with image filters on it (or maybe
    // other PaintFlags options) are not a noop.  Figure out what these
    // are so we can skip them correctly.
    op->Raster(canvas, params);
  }
}

void PaintOpBuffer::ReallocBuffer(size_t new_size) {
  DCHECK_GE(new_size, used_);
  std::unique_ptr<char, base::AlignedFreeDeleter> new_data(
      static_cast<char*>(base::AlignedAlloc(new_size, PaintOpAlign)));
  if (data_)
    memcpy(new_data.get(), data_.get(), used_);
  data_ = std::move(new_data);
  reserved_ = new_size;
}

std::pair<void*, size_t> PaintOpBuffer::AllocatePaintOp(size_t sizeof_op,
                                                        size_t bytes) {
  // Compute a skip such that all ops in the buffer are aligned to the
  // maximum required alignment of all ops.
  size_t skip = MathUtil::UncheckedRoundUp(sizeof_op + bytes, PaintOpAlign);
  DCHECK_LT(skip, PaintOp::kMaxSkip);
  if (used_ + skip > reserved_) {
    // Start reserved_ at kInitialBufferSize and then double.
    // ShrinkToFit can make this smaller afterwards.
    size_t new_size = reserved_ ? reserved_ : kInitialBufferSize;
    while (used_ + skip > new_size)
      new_size *= 2;
    ReallocBuffer(new_size);
  }
  DCHECK_LE(used_ + skip, reserved_);

  void* op = data_.get() + used_;
  used_ += skip;
  op_count_++;
  return std::make_pair(op, skip);
}

void PaintOpBuffer::ShrinkToFit() {
  if (used_ == reserved_)
    return;
  if (!used_) {
    reserved_ = 0;
    data_.reset();
  } else {
    ReallocBuffer(used_);
  }
}

}  // namespace cc
