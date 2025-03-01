/*
 * Copyright (C) 2007, 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Google, Inc.  All rights reserved.
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/paint/SVGPaintContext.h"

#include "core/layout/svg/LayoutSVGResourceFilter.h"
#include "core/layout/svg/LayoutSVGResourceMasker.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/layout/svg/SVGResources.h"
#include "core/layout/svg/SVGResourcesCache.h"
#include "core/paint/SVGMaskPainter.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

SVGPaintContext::~SVGPaintContext() {
  if (filter_) {
    DCHECK(SVGResourcesCache::CachedResourcesForLayoutObject(&object_));
    DCHECK(
        SVGResourcesCache::CachedResourcesForLayoutObject(&object_)->Filter() ==
        filter_);
    DCHECK(filter_recording_context_);
    SVGFilterPainter(*filter_).FinishEffect(object_,
                                            *filter_recording_context_);

    // Reset the paint info after the filter effect has been completed.
    filter_paint_info_ = nullptr;
  }

  if (masker_) {
    DCHECK(SVGResourcesCache::CachedResourcesForLayoutObject(&object_));
    DCHECK(
        SVGResourcesCache::CachedResourcesForLayoutObject(&object_)->Masker() ==
        masker_);
    SVGMaskPainter(*masker_).FinishEffect(object_, GetPaintInfo().context);
  }
}

bool SVGPaintContext::ApplyClipMaskAndFilterIfNecessary() {
#if DCHECK_IS_ON()
  DCHECK(!apply_clip_mask_and_filter_if_necessary_called_);
  apply_clip_mask_and_filter_if_necessary_called_ = true;
#endif
  // In SPv2 we should early exit once the paint property state has been
  // applied, because all meta (non-drawing) display items are ignored in
  // SPv2. However we can't simply omit them because there are still
  // non-composited painting (e.g. SVG filters in particular) that rely on
  // these meta display items.
  ApplyPaintPropertyState();

  // When rendering clip paths as masks, only geometric operations should be
  // included so skip non-geometric operations such as compositing, masking, and
  // filtering.
  if (GetPaintInfo().IsRenderingClipPathAsMaskImage()) {
    DCHECK(!object_.IsSVGRoot());
    ApplyClipIfNecessary();
    return true;
  }

  bool is_svg_root = object_.IsSVGRoot();

  // Layer takes care of root opacity and blend mode.
  if (is_svg_root) {
    DCHECK(!(object_.IsTransparent() || object_.StyleRef().HasBlendMode()) ||
           object_.HasLayer());
  } else {
    ApplyCompositingIfNecessary();
  }

  if (is_svg_root) {
    DCHECK(!object_.StyleRef().ClipPath() || object_.HasLayer());
  } else {
    ApplyClipIfNecessary();
  }

  SVGResources* resources =
      SVGResourcesCache::CachedResourcesForLayoutObject(&object_);

  if (!ApplyMaskIfNecessary(resources))
    return false;

  if (is_svg_root) {
    DCHECK(!object_.StyleRef().HasFilter() || object_.HasLayer());
  } else if (!ApplyFilterIfNecessary(resources)) {
    return false;
  }

  if (!IsIsolationInstalled() &&
      SVGLayoutSupport::IsIsolationRequired(&object_)) {
    compositing_recorder_ = WTF::WrapUnique(new CompositingRecorder(
        GetPaintInfo().context, object_, SkBlendMode::kSrcOver, 1));
  }

  return true;
}

void SVGPaintContext::ApplyPaintPropertyState() {
  if (!RuntimeEnabledFeatures::SlimmingPaintV2Enabled())
    return;

  // SVGRoot works like normal CSS replaced element and its effects are
  // applied as stacking context effect by PaintLayerPainter.
  if (object_.IsSVGRoot())
    return;

  const auto* paint_properties =
      object_.FirstFragment() ? object_.FirstFragment()->PaintProperties()
                              : nullptr;
  const EffectPaintPropertyNode* effect =
      paint_properties ? paint_properties->Effect() : nullptr;
  if (!effect)
    return;

  auto& paint_controller = GetPaintInfo().context.GetPaintController();
  PaintChunkProperties properties(
      paint_controller.CurrentPaintChunkProperties());
  properties.property_tree_state.SetEffect(effect);
  scoped_paint_chunk_properties_.emplace(paint_controller, object_, properties);
}

void SVGPaintContext::ApplyCompositingIfNecessary() {
  DCHECK(!GetPaintInfo().IsRenderingClipPathAsMaskImage());

  const ComputedStyle& style = object_.StyleRef();
  float opacity = style.Opacity();
  WebBlendMode blend_mode = style.HasBlendMode() && object_.IsBlendingAllowed()
                                ? style.BlendMode()
                                : WebBlendMode::kNormal;
  if (opacity < 1 || blend_mode != WebBlendMode::kNormal) {
    const FloatRect compositing_bounds =
        object_.VisualRectInLocalSVGCoordinates();
    compositing_recorder_ = WTF::WrapUnique(new CompositingRecorder(
        GetPaintInfo().context, object_,
        WebCoreCompositeToSkiaComposite(kCompositeSourceOver, blend_mode),
        opacity, &compositing_bounds));
  }
}

void SVGPaintContext::ApplyClipIfNecessary() {
  ClipPathOperation* clip_path_operation = object_.StyleRef().ClipPath();
  if (!clip_path_operation)
    return;
  if (!RuntimeEnabledFeatures::SlimmingPaintV2Enabled()) {
    clip_path_clipper_.emplace(GetPaintInfo().context, *clip_path_operation,
                               object_, object_.ObjectBoundingBox(),
                               FloatPoint());
  }
}

bool SVGPaintContext::ApplyMaskIfNecessary(SVGResources* resources) {
  if (LayoutSVGResourceMasker* masker =
          resources ? resources->Masker() : nullptr) {
    if (!SVGMaskPainter(*masker).PrepareEffect(object_, GetPaintInfo().context))
      return false;
    masker_ = masker;
  }
  return true;
}

static bool HasReferenceFilterOnly(const ComputedStyle& style) {
  if (!style.HasFilter())
    return false;
  const FilterOperations& operations = style.Filter();
  if (operations.size() != 1)
    return false;
  return operations.at(0)->GetType() == FilterOperation::REFERENCE;
}

bool SVGPaintContext::ApplyFilterIfNecessary(SVGResources* resources) {
  if (!resources)
    return !HasReferenceFilterOnly(object_.StyleRef());

  LayoutSVGResourceFilter* filter = resources->Filter();
  if (!filter)
    return true;
  filter_recording_context_ =
      WTF::WrapUnique(new SVGFilterRecordingContext(GetPaintInfo().context));
  filter_ = filter;
  GraphicsContext* filter_context = SVGFilterPainter(*filter).PrepareEffect(
      object_, *filter_recording_context_);
  if (!filter_context)
    return false;

  // Because the filter needs to cache its contents we replace the context
  // during filtering with the filter's context.
  filter_paint_info_ =
      WTF::WrapUnique(new PaintInfo(*filter_context, paint_info_));

  // Because we cache the filter contents and do not invalidate on paint
  // invalidation rect changes, we need to paint the entire filter region
  // so elements outside the initial paint (due to scrolling, etc) paint.
  filter_paint_info_->cull_rect_.rect_ = LayoutRect::InfiniteIntRect();
  return true;
}

bool SVGPaintContext::IsIsolationInstalled() const {
  if (compositing_recorder_)
    return true;
  if (masker_ || filter_)
    return true;
  if (!RuntimeEnabledFeatures::SlimmingPaintV2Enabled() && clip_path_clipper_ &&
      clip_path_clipper_->UsingMask())
    return true;
  return false;
}

void SVGPaintContext::PaintResourceSubtree(GraphicsContext& context,
                                           const LayoutObject* item) {
  DCHECK(item);
  DCHECK(!item->NeedsLayout());

  PaintInfo info(context, LayoutRect::InfiniteIntRect(), kPaintPhaseForeground,
                 kGlobalPaintNormalPhase,
                 kPaintLayerPaintingRenderingResourceSubtree);
  item->Paint(info, IntPoint());
}

bool SVGPaintContext::PaintForLayoutObject(
    const PaintInfo& paint_info,
    const ComputedStyle& style,
    const LayoutObject& layout_object,
    LayoutSVGResourceMode resource_mode,
    PaintFlags& flags,
    const AffineTransform* additional_paint_server_transform) {
  if (paint_info.IsRenderingClipPathAsMaskImage()) {
    if (resource_mode == kApplyToStrokeMode)
      return false;
    flags.setColor(SVGComputedStyle::InitialFillPaintColor().Rgb());
    flags.setShader(nullptr);
    return true;
  }

  SVGPaintServer paint_server = SVGPaintServer::RequestForLayoutObject(
      layout_object, style, resource_mode);
  if (!paint_server.IsValid())
    return false;

  if (additional_paint_server_transform && paint_server.IsTransformDependent())
    paint_server.PrependTransform(*additional_paint_server_transform);

  const SVGComputedStyle& svg_style = style.SvgStyle();
  float alpha = resource_mode == kApplyToFillMode ? svg_style.FillOpacity()
                                                  : svg_style.StrokeOpacity();
  paint_server.ApplyToPaintFlags(flags, alpha);

  // We always set filter quality to 'low' here. This value will only have an
  // effect for patterns, which are SkPictures, so using high-order filter
  // should have little effect on the overall quality.
  flags.setFilterQuality(kLow_SkFilterQuality);

  // TODO(fs): The color filter can set when generating a picture for a mask -
  // due to color-interpolation. We could also just apply the
  // color-interpolation property from the the shape itself (which could mean
  // the paintserver if it has it specified), since that would be more in line
  // with the spec for color-interpolation. For now, just steal it from the GC
  // though.
  // Additionally, it's not really safe/guaranteed to be correct, as
  // something down the flags pipe may want to farther tweak the color
  // filter, which could yield incorrect results. (Consider just using
  // saveLayer() w/ this color filter explicitly instead.)
  flags.setColorFilter(sk_ref_sp(paint_info.context.GetColorFilter()));
  return true;
}

}  // namespace blink
