/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011-2012. All rights reserved.
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
 *
 */

#include "core/layout/LayoutReplaced.h"

#include "core/editing/PositionWithAffinity.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutBlock.h"
#include "core/layout/LayoutEmbeddedContent.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutVideo.h"
#include "core/layout/api/LineLayoutBlockFlow.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/ReplacedPainter.h"
#include "platform/LengthFunctions.h"

namespace blink {

const int LayoutReplaced::kDefaultWidth = 300;
const int LayoutReplaced::kDefaultHeight = 150;

LayoutReplaced::LayoutReplaced(Element* element)
    : LayoutBox(element), intrinsic_size_(kDefaultWidth, kDefaultHeight) {
  // TODO(jchaffraix): We should not set this boolean for block-level
  // replaced elements (crbug.com/567964).
  SetIsAtomicInlineLevel(true);
}

LayoutReplaced::LayoutReplaced(Element* element,
                               const LayoutSize& intrinsic_size)
    : LayoutBox(element), intrinsic_size_(intrinsic_size) {
  // TODO(jchaffraix): We should not set this boolean for block-level
  // replaced elements (crbug.com/567964).
  SetIsAtomicInlineLevel(true);
}

LayoutReplaced::~LayoutReplaced() {}

void LayoutReplaced::WillBeDestroyed() {
  if (!DocumentBeingDestroyed() && Parent())
    Parent()->DirtyLinesFromChangedChild(this);

  LayoutBox::WillBeDestroyed();
}

void LayoutReplaced::StyleDidChange(StyleDifference diff,
                                    const ComputedStyle* old_style) {
  LayoutBox::StyleDidChange(diff, old_style);

  bool had_style = (old_style != 0);
  float old_zoom =
      had_style ? old_style->EffectiveZoom() : ComputedStyle::InitialZoom();
  if (Style() && Style()->EffectiveZoom() != old_zoom)
    IntrinsicSizeChanged();
}

void LayoutReplaced::UpdateLayout() {
  DCHECK(NeedsLayout());
  LayoutAnalyzer::Scope analyzer(*this);

  LayoutRect old_content_rect = ReplacedContentRect();

  SetHeight(MinimumReplacedHeight());

  UpdateLogicalWidth();
  UpdateLogicalHeight();

  overflow_.reset();
  AddVisualEffectOverflow();
  UpdateAfterLayout();
  InvalidateBackgroundObscurationStatus();

  ClearNeedsLayout();

  if (!RuntimeEnabledFeatures::SlimmingPaintV2Enabled() &&
      ReplacedContentRect() != old_content_rect)
    SetShouldDoFullPaintInvalidation();
}

void LayoutReplaced::IntrinsicSizeChanged() {
  int scaled_width = static_cast<int>(kDefaultWidth * Style()->EffectiveZoom());
  int scaled_height =
      static_cast<int>(kDefaultHeight * Style()->EffectiveZoom());
  intrinsic_size_ = LayoutSize(scaled_width, scaled_height);
  SetNeedsLayoutAndPrefWidthsRecalcAndFullPaintInvalidation(
      LayoutInvalidationReason::kSizeChanged);
}

void LayoutReplaced::Paint(const PaintInfo& paint_info,
                           const LayoutPoint& paint_offset) const {
  ReplacedPainter(*this).Paint(paint_info, paint_offset);
}

bool LayoutReplaced::HasReplacedLogicalHeight() const {
  if (Style()->LogicalHeight().IsAuto())
    return false;

  if (Style()->LogicalHeight().IsSpecified()) {
    if (HasAutoHeightOrContainingBlockWithAutoHeight())
      return false;
    return true;
  }

  if (Style()->LogicalHeight().IsIntrinsic())
    return true;

  return false;
}

bool LayoutReplaced::NeedsPreferredWidthsRecalculation() const {
  // If the height is a percentage and the width is auto, then the
  // containingBlocks's height changing can cause this node to change it's
  // preferred width because it maintains aspect ratio.
  return HasRelativeLogicalHeight() && Style()->LogicalWidth().IsAuto();
}

static inline bool LayoutObjectHasAspectRatio(
    const LayoutObject* layout_object) {
  DCHECK(layout_object);
  return layout_object->IsImage() || layout_object->IsCanvas() ||
         layout_object->IsVideo();
}

void LayoutReplaced::ComputeIntrinsicSizingInfoForReplacedContent(
    LayoutReplaced* content_layout_object,
    IntrinsicSizingInfo& intrinsic_sizing_info) const {
  if (content_layout_object) {
    content_layout_object->ComputeIntrinsicSizingInfo(intrinsic_sizing_info);

    // Handle zoom & vertical writing modes here, as the embedded document
    // doesn't know about them.
    intrinsic_sizing_info.size.Scale(Style()->EffectiveZoom());
    if (IsLayoutImage())
      intrinsic_sizing_info.size.Scale(
          ToLayoutImage(this)->ImageDevicePixelRatio());

    // Update our intrinsic size to match what the content layoutObject has
    // computed, so that when we constrain the size below, the correct intrinsic
    // size will be obtained for comparison against min and max widths.
    if (!intrinsic_sizing_info.aspect_ratio.IsEmpty() &&
        !intrinsic_sizing_info.size.IsEmpty())
      intrinsic_size_ = LayoutSize(intrinsic_sizing_info.size);

    if (!IsHorizontalWritingMode())
      intrinsic_sizing_info.Transpose();
  } else {
    ComputeIntrinsicSizingInfo(intrinsic_sizing_info);
    if (!intrinsic_sizing_info.aspect_ratio.IsEmpty() &&
        !intrinsic_sizing_info.size.IsEmpty())
      intrinsic_size_ =
          LayoutSize(IsHorizontalWritingMode()
                         ? intrinsic_sizing_info.size
                         : intrinsic_sizing_info.size.TransposedSize());
  }
}

FloatSize LayoutReplaced::ConstrainIntrinsicSizeToMinMax(
    const IntrinsicSizingInfo& intrinsic_sizing_info) const {
  // Constrain the intrinsic size along each axis according to minimum and
  // maximum width/heights along the opposite axis. So for example a maximum
  // width that shrinks our width will result in the height we compute here
  // having to shrink in order to preserve the aspect ratio. Because we compute
  // these values independently along each axis, the final returned size may in
  // fact not preserve the aspect ratio.
  // TODO(davve): Investigate using only the intrinsic aspect ratio here.
  FloatSize constrained_size = intrinsic_sizing_info.size;
  if (!intrinsic_sizing_info.aspect_ratio.IsEmpty() &&
      !intrinsic_sizing_info.size.IsEmpty() &&
      Style()->LogicalWidth().IsAuto() && Style()->LogicalHeight().IsAuto()) {
    // We can't multiply or divide by 'intrinsicSizingInfo.aspectRatio' here, it
    // breaks tests, like images/zoomed-img-size.html, which
    // can only be fixed once subpixel precision is available for things like
    // intrinsicWidth/Height - which include zoom!
    constrained_size.SetWidth(LayoutBox::ComputeReplacedLogicalHeight() *
                              intrinsic_sizing_info.size.Width() /
                              intrinsic_sizing_info.size.Height());
    constrained_size.SetHeight(LayoutBox::ComputeReplacedLogicalWidth() *
                               intrinsic_sizing_info.size.Height() /
                               intrinsic_sizing_info.size.Width());
  }
  return constrained_size;
}

void LayoutReplaced::ComputePositionedLogicalWidth(
    LogicalExtentComputedValues& computed_values) const {
  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.3.8 "Absolutely positioned, replaced elements"
  // <http://www.w3.org/TR/2005/WD-CSS21-20050613/visudet.html#abs-replaced-width>
  // (block-style-comments in this function correspond to text from the spec and
  // the numbers correspond to numbers in spec).

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relative positioned inline.
  const LayoutBoxModelObject* container_block =
      ToLayoutBoxModelObject(Container());

  const LayoutUnit container_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block);
  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  // To match WinIE, in quirks mode use the parent's 'direction' property
  // instead of the the container block's.
  TextDirection container_direction = container_block->Style()->Direction();

  // Variables to solve.
  bool is_horizontal = IsHorizontalWritingMode();
  Length logical_left = Style()->LogicalLeft();
  Length logical_right = Style()->LogicalRight();
  Length margin_logical_left =
      is_horizontal ? Style()->MarginLeft() : Style()->MarginTop();
  Length margin_logical_right =
      is_horizontal ? Style()->MarginRight() : Style()->MarginBottom();
  LayoutUnit& margin_logical_left_alias = Style()->IsLeftToRightDirection()
                                              ? computed_values.margins_.start_
                                              : computed_values.margins_.end_;
  LayoutUnit& margin_logical_right_alias =
      Style()->IsLeftToRightDirection() ? computed_values.margins_.end_
                                        : computed_values.margins_.start_;

  // ---------------------------------------------------------------------------
  // 1. The used value of 'width' is determined as for inline replaced
  //  elements.
  // ---------------------------------------------------------------------------
  // NOTE: This value of width is final in that the min/max width calculations
  // are dealt with in computeReplacedWidth().  This means that the steps to
  // produce correct max/min in the non-replaced version, are not necessary.
  computed_values.extent_ =
      ComputeReplacedLogicalWidth() + BorderAndPaddingLogicalWidth();

  const LayoutUnit available_space =
      container_logical_width - computed_values.extent_;

  // ---------------------------------------------------------------------------
  // 2. If both 'left' and 'right' have the value 'auto', then if 'direction'
  //    of the containing block is 'ltr', set 'left' to the static position;
  //    else if 'direction' is 'rtl', set 'right' to the static position.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  ComputeInlineStaticDistance(logical_left, logical_right, this,
                              container_block, container_logical_width);

  // ---------------------------------------------------------------------------
  // 3. If 'left' or 'right' are 'auto', replace any 'auto' on 'margin-left'
  //    or 'margin-right' with '0'.
  // ---------------------------------------------------------------------------
  if (logical_left.IsAuto() || logical_right.IsAuto()) {
    if (margin_logical_left.IsAuto())
      margin_logical_left.SetValue(kFixed, 0);
    if (margin_logical_right.IsAuto())
      margin_logical_right.SetValue(kFixed, 0);
  }

  // ---------------------------------------------------------------------------
  // 4. If at this point both 'margin-left' and 'margin-right' are still 'auto',
  //    solve the equation under the extra constraint that the two margins must
  //    get equal values, unless this would make them negative, in which case
  //    when the direction of the containing block is 'ltr' ('rtl'), set
  //    'margin-left' ('margin-right') to zero and solve for 'margin-right'
  //    ('margin-left').
  // ---------------------------------------------------------------------------
  LayoutUnit logical_left_value;
  LayoutUnit logical_right_value;

  if (margin_logical_left.IsAuto() && margin_logical_right.IsAuto()) {
    // 'left' and 'right' cannot be 'auto' due to step 3
    DCHECK(!(logical_left.IsAuto() && logical_right.IsAuto()));

    logical_left_value = ValueForLength(logical_left, container_logical_width);
    logical_right_value =
        ValueForLength(logical_right, container_logical_width);

    LayoutUnit difference =
        available_space - (logical_left_value + logical_right_value);
    if (difference > LayoutUnit()) {
      margin_logical_left_alias = difference / 2;  // split the difference
      margin_logical_right_alias =
          difference -
          margin_logical_left_alias;  // account for odd valued differences
    } else {
      // Use the containing block's direction rather than the parent block's
      // per CSS 2.1 reference test abspos-replaced-width-margin-000.
      if (container_direction == TextDirection::kLtr) {
        margin_logical_left_alias = LayoutUnit();
        margin_logical_right_alias = difference;  // will be negative
      } else {
        margin_logical_left_alias = difference;  // will be negative
        margin_logical_right_alias = LayoutUnit();
      }
    }

    // -------------------------------------------------------------------------
    // 5. If at this point there is an 'auto' left, solve the equation for that
    //    value.
    // -------------------------------------------------------------------------
  } else if (logical_left.IsAuto()) {
    margin_logical_left_alias =
        ValueForLength(margin_logical_left, container_relative_logical_width);
    margin_logical_right_alias =
        ValueForLength(margin_logical_right, container_relative_logical_width);
    logical_right_value =
        ValueForLength(logical_right, container_logical_width);

    // Solve for 'left'
    logical_left_value =
        available_space - (logical_right_value + margin_logical_left_alias +
                           margin_logical_right_alias);
  } else if (logical_right.IsAuto()) {
    margin_logical_left_alias =
        ValueForLength(margin_logical_left, container_relative_logical_width);
    margin_logical_right_alias =
        ValueForLength(margin_logical_right, container_relative_logical_width);
    logical_left_value = ValueForLength(logical_left, container_logical_width);

    // Solve for 'right'
    logical_right_value =
        available_space - (logical_left_value + margin_logical_left_alias +
                           margin_logical_right_alias);
  } else if (margin_logical_left.IsAuto()) {
    margin_logical_right_alias =
        ValueForLength(margin_logical_right, container_relative_logical_width);
    logical_left_value = ValueForLength(logical_left, container_logical_width);
    logical_right_value =
        ValueForLength(logical_right, container_logical_width);

    // Solve for 'margin-left'
    margin_logical_left_alias =
        available_space -
        (logical_left_value + logical_right_value + margin_logical_right_alias);
  } else if (margin_logical_right.IsAuto()) {
    margin_logical_left_alias =
        ValueForLength(margin_logical_left, container_relative_logical_width);
    logical_left_value = ValueForLength(logical_left, container_logical_width);
    logical_right_value =
        ValueForLength(logical_right, container_logical_width);

    // Solve for 'margin-right'
    margin_logical_right_alias =
        available_space -
        (logical_left_value + logical_right_value + margin_logical_left_alias);
  } else {
    // Nothing is 'auto', just calculate the values.
    margin_logical_left_alias =
        ValueForLength(margin_logical_left, container_relative_logical_width);
    margin_logical_right_alias =
        ValueForLength(margin_logical_right, container_relative_logical_width);
    logical_right_value =
        ValueForLength(logical_right, container_logical_width);
    logical_left_value = ValueForLength(logical_left, container_logical_width);
    // If the containing block is right-to-left, then push the left position as
    // far to the right as possible
    if (container_direction == TextDirection::kRtl) {
      int total_logical_width =
          (computed_values.extent_ + logical_left_value + logical_right_value +
           margin_logical_left_alias + margin_logical_right_alias)
              .ToInt();
      logical_left_value =
          container_logical_width - (total_logical_width - logical_left_value);
    }
  }

  // ---------------------------------------------------------------------------
  // 6. If at this point the values are over-constrained, ignore the value for
  //    either 'left' (in case the 'direction' property of the containing block
  //    is 'rtl') or 'right' (in case 'direction' is 'ltr') and solve for that
  //    value.
  // ---------------------------------------------------------------------------
  // NOTE: Constraints imposed by the width of the containing block and its
  // content have already been accounted for above.
  //
  // FIXME: Deal with differing writing modes here.  Our offset needs to be in
  // the containing block's coordinate space, so that
  // can make the result here rather complicated to compute.
  //
  // Use computed values to calculate the horizontal position.
  //
  // FIXME: This hack is needed to calculate the logical left position for a
  // 'rtl' relatively positioned, inline containing block because right now, it
  // is using the logical left position of the first line box when really it
  // should use the last line box. When this is fixed elsewhere, this block
  // should be removed.
  if (container_block->IsLayoutInline() &&
      !container_block->Style()->IsLeftToRightDirection()) {
    const LayoutInline* flow = ToLayoutInline(container_block);
    InlineFlowBox* first_line = flow->FirstLineBox();
    InlineFlowBox* last_line = flow->LastLineBox();
    if (first_line && last_line && first_line != last_line) {
      computed_values.position_ =
          logical_left_value + margin_logical_left_alias +
          last_line->BorderLogicalLeft() +
          (last_line->LogicalLeft() - first_line->LogicalLeft());
      return;
    }
  }

  LayoutUnit logical_left_pos = logical_left_value + margin_logical_left_alias;
  ComputeLogicalLeftPositionedOffset(logical_left_pos, this,
                                     computed_values.extent_, container_block,
                                     container_logical_width);
  computed_values.position_ = logical_left_pos;
}

void LayoutReplaced::ComputePositionedLogicalHeight(
    LogicalExtentComputedValues& computed_values) const {
  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.6.5 "Absolutely positioned, replaced elements"
  // <http://www.w3.org/TR/2005/WD-CSS21-20050613/visudet.html#abs-replaced-height>
  // (block-style-comments in this function correspond to text from the spec and
  // the numbers correspond to numbers in spec)

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relpositioned inline.
  const LayoutBoxModelObject* container_block =
      ToLayoutBoxModelObject(Container());

  const LayoutUnit container_logical_height =
      ContainingBlockLogicalHeightForPositioned(container_block);
  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  // Variables to solve.
  Length margin_before = Style()->MarginBefore();
  Length margin_after = Style()->MarginAfter();
  LayoutUnit& margin_before_alias = computed_values.margins_.before_;
  LayoutUnit& margin_after_alias = computed_values.margins_.after_;

  Length logical_top = Style()->LogicalTop();
  Length logical_bottom = Style()->LogicalBottom();

  // ---------------------------------------------------------------------------
  // 1. The used value of 'height' is determined as for inline replaced
  //    elements.
  // ---------------------------------------------------------------------------
  // NOTE: This value of height is final in that the min/max height calculations
  // are dealt with in computeReplacedHeight().  This means that the steps to
  // produce correct max/min in the non-replaced version, are not necessary.
  computed_values.extent_ =
      ComputeReplacedLogicalHeight() + BorderAndPaddingLogicalHeight();
  const LayoutUnit available_space =
      container_logical_height - computed_values.extent_;

  // ---------------------------------------------------------------------------
  // 2. If both 'top' and 'bottom' have the value 'auto', replace 'top' with the
  //    element's static position.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  ComputeBlockStaticDistance(logical_top, logical_bottom, this,
                             container_block);

  // ---------------------------------------------------------------------------
  // 3. If 'bottom' is 'auto', replace any 'auto' on 'margin-top' or
  //    'margin-bottom' with '0'.
  // ---------------------------------------------------------------------------
  // FIXME: The spec. says that this step should only be taken when bottom is
  // auto, but if only top is auto, this makes step 4 impossible.
  if (logical_top.IsAuto() || logical_bottom.IsAuto()) {
    if (margin_before.IsAuto())
      margin_before.SetValue(kFixed, 0);
    if (margin_after.IsAuto())
      margin_after.SetValue(kFixed, 0);
  }

  // ---------------------------------------------------------------------------
  // 4. If at this point both 'margin-top' and 'margin-bottom' are still 'auto',
  //    solve the equation under the extra constraint that the two margins must
  //    get equal values.
  // ---------------------------------------------------------------------------
  LayoutUnit logical_top_value;
  LayoutUnit logical_bottom_value;

  if (margin_before.IsAuto() && margin_after.IsAuto()) {
    // 'top' and 'bottom' cannot be 'auto' due to step 2 and 3 combined.
    DCHECK(!(logical_top.IsAuto() || logical_bottom.IsAuto()));

    logical_top_value = ValueForLength(logical_top, container_logical_height);
    logical_bottom_value =
        ValueForLength(logical_bottom, container_logical_height);

    LayoutUnit difference =
        available_space - (logical_top_value + logical_bottom_value);
    // NOTE: This may result in negative values.
    margin_before_alias = difference / 2;  // split the difference
    margin_after_alias =
        difference - margin_before_alias;  // account for odd valued differences

    // -------------------------------------------------------------------------
    // 5. If at this point there is only one 'auto' left, solve the equation
    //    for that value.
    // -------------------------------------------------------------------------
  } else if (logical_top.IsAuto()) {
    margin_before_alias =
        ValueForLength(margin_before, container_relative_logical_width);
    margin_after_alias =
        ValueForLength(margin_after, container_relative_logical_width);
    logical_bottom_value =
        ValueForLength(logical_bottom, container_logical_height);

    // Solve for 'top'
    logical_top_value =
        available_space -
        (logical_bottom_value + margin_before_alias + margin_after_alias);
  } else if (logical_bottom.IsAuto()) {
    margin_before_alias =
        ValueForLength(margin_before, container_relative_logical_width);
    margin_after_alias =
        ValueForLength(margin_after, container_relative_logical_width);
    logical_top_value = ValueForLength(logical_top, container_logical_height);

    // Solve for 'bottom'
    // NOTE: It is not necessary to solve for 'bottom' because we don't ever
    // use the value.
  } else if (margin_before.IsAuto()) {
    margin_after_alias =
        ValueForLength(margin_after, container_relative_logical_width);
    logical_top_value = ValueForLength(logical_top, container_logical_height);
    logical_bottom_value =
        ValueForLength(logical_bottom, container_logical_height);

    // Solve for 'margin-top'
    margin_before_alias =
        available_space -
        (logical_top_value + logical_bottom_value + margin_after_alias);
  } else if (margin_after.IsAuto()) {
    margin_before_alias =
        ValueForLength(margin_before, container_relative_logical_width);
    logical_top_value = ValueForLength(logical_top, container_logical_height);
    logical_bottom_value =
        ValueForLength(logical_bottom, container_logical_height);

    // Solve for 'margin-bottom'
    margin_after_alias =
        available_space -
        (logical_top_value + logical_bottom_value + margin_before_alias);
  } else {
    // Nothing is 'auto', just calculate the values.
    margin_before_alias =
        ValueForLength(margin_before, container_relative_logical_width);
    margin_after_alias =
        ValueForLength(margin_after, container_relative_logical_width);
    logical_top_value = ValueForLength(logical_top, container_logical_height);
    // NOTE: It is not necessary to solve for 'bottom' because we don't ever
    // use the value.
  }

  // ---------------------------------------------------------------------------
  // 6. If at this point the values are over-constrained, ignore the value for
  //    'bottom' and solve for that value.
  // ---------------------------------------------------------------------------
  // NOTE: It is not necessary to do this step because we don't end up using the
  // value of 'bottom' regardless of whether the values are over-constrained or
  // not.

  // Use computed values to calculate the vertical position.
  LayoutUnit logical_top_pos = logical_top_value + margin_before_alias;
  ComputeLogicalTopPositionedOffset(logical_top_pos, this,
                                    computed_values.extent_, container_block,
                                    container_logical_height);
  computed_values.position_ = logical_top_pos;
}

LayoutRect LayoutReplaced::ComputeObjectFit(
    const LayoutSize* overridden_intrinsic_size) const {
  LayoutRect content_rect = ContentBoxRect();
  EObjectFit object_fit = Style()->GetObjectFit();

  if (object_fit == EObjectFit::kFill &&
      Style()->ObjectPosition() == ComputedStyle::InitialObjectPosition()) {
    return content_rect;
  }

  // TODO(davve): intrinsicSize doubles as both intrinsic size and intrinsic
  // ratio. In the case of SVG images this isn't correct since they can have
  // intrinsic ratio but no intrinsic size. In order to maintain aspect ratio,
  // the intrinsic size for SVG might be faked from the aspect ratio,
  // see SVGImage::containerSize().
  LayoutSize intrinsic_size = overridden_intrinsic_size
                                  ? *overridden_intrinsic_size
                                  : this->IntrinsicSize();
  if (!intrinsic_size.Width() || !intrinsic_size.Height())
    return content_rect;

  LayoutRect final_rect = content_rect;
  switch (object_fit) {
    case EObjectFit::kContain:
    case EObjectFit::kScaleDown:
    case EObjectFit::kCover:
      final_rect.SetSize(final_rect.Size().FitToAspectRatio(
          intrinsic_size, object_fit == EObjectFit::kCover
                              ? kAspectRatioFitGrow
                              : kAspectRatioFitShrink));
      if (object_fit != EObjectFit::kScaleDown ||
          final_rect.Width() <= intrinsic_size.Width())
        break;
    // fall through
    case EObjectFit::kNone:
      final_rect.SetSize(intrinsic_size);
      break;
    case EObjectFit::kFill:
      break;
    default:
      NOTREACHED();
  }

  LayoutUnit x_offset = MinimumValueForLength(
      Style()->ObjectPosition().X(), content_rect.Width() - final_rect.Width());
  LayoutUnit y_offset =
      MinimumValueForLength(Style()->ObjectPosition().Y(),
                            content_rect.Height() - final_rect.Height());
  final_rect.Move(x_offset, y_offset);

  return final_rect;
}

LayoutRect LayoutReplaced::ReplacedContentRect() const {
  return ComputeObjectFit();
}

void LayoutReplaced::ComputeIntrinsicSizingInfo(
    IntrinsicSizingInfo& intrinsic_sizing_info) const {
  // If there's an embeddedReplacedContent() of a remote, referenced document
  // available, this code-path should never be used.
  DCHECK(!EmbeddedReplacedContent());
  intrinsic_sizing_info.size = FloatSize(IntrinsicLogicalWidth().ToFloat(),
                                         IntrinsicLogicalHeight().ToFloat());

  // Figure out if we need to compute an intrinsic ratio.
  if (intrinsic_sizing_info.size.IsEmpty() || !LayoutObjectHasAspectRatio(this))
    return;

  intrinsic_sizing_info.aspect_ratio = intrinsic_sizing_info.size;
}

static inline LayoutUnit ResolveWidthForRatio(LayoutUnit height,
                                              const FloatSize& aspect_ratio) {
  return LayoutUnit(height * aspect_ratio.Width() / aspect_ratio.Height());
}

static inline LayoutUnit ResolveHeightForRatio(LayoutUnit width,
                                               const FloatSize& aspect_ratio) {
  return LayoutUnit(width * aspect_ratio.Height() / aspect_ratio.Width());
}

LayoutUnit LayoutReplaced::ComputeConstrainedLogicalWidth(
    ShouldComputePreferred should_compute_preferred) const {
  if (should_compute_preferred == kComputePreferred)
    return ComputeReplacedLogicalWidthRespectingMinMaxWidth(LayoutUnit(),
                                                            kComputePreferred);
  // The aforementioned 'constraint equation' used for block-level, non-replaced
  // elements in normal flow:
  // 'margin-left' + 'border-left-width' + 'padding-left' + 'width' +
  // 'padding-right' + 'border-right-width' + 'margin-right' = width of
  // containing block
  LayoutUnit logical_width = ContainingBlock()->AvailableLogicalWidth();

  // This solves above equation for 'width' (== logicalWidth).
  LayoutUnit margin_start =
      MinimumValueForLength(Style()->MarginStart(), logical_width);
  LayoutUnit margin_end =
      MinimumValueForLength(Style()->MarginEnd(), logical_width);
  logical_width = (logical_width - (margin_start + margin_end +
                                    (Size().Width() - ClientWidth())))
                      .ClampNegativeToZero();
  return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
      logical_width, should_compute_preferred);
}

LayoutUnit LayoutReplaced::ComputeReplacedLogicalWidth(
    ShouldComputePreferred should_compute_preferred) const {
  if (Style()->LogicalWidth().IsSpecified() ||
      Style()->LogicalWidth().IsIntrinsic())
    return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
        ComputeReplacedLogicalWidthUsing(kMainOrPreferredSize,
                                         Style()->LogicalWidth()),
        should_compute_preferred);

  LayoutReplaced* content_layout_object = EmbeddedReplacedContent();

  // 10.3.2 Inline, replaced elements:
  // http://www.w3.org/TR/CSS21/visudet.html#inline-replaced-width
  IntrinsicSizingInfo intrinsic_sizing_info;
  ComputeIntrinsicSizingInfoForReplacedContent(content_layout_object,
                                               intrinsic_sizing_info);
  FloatSize constrained_size =
      ConstrainIntrinsicSizeToMinMax(intrinsic_sizing_info);

  if (Style()->LogicalWidth().IsAuto()) {
    bool computed_height_is_auto = Style()->LogicalHeight().IsAuto();

    // If 'height' and 'width' both have computed values of 'auto' and the
    // element also has an intrinsic width, then that intrinsic width is the
    // used value of 'width'.
    if (computed_height_is_auto && intrinsic_sizing_info.has_width)
      return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
          LayoutUnit(constrained_size.Width()), should_compute_preferred);

    if (!intrinsic_sizing_info.aspect_ratio.IsEmpty()) {
      // If 'height' and 'width' both have computed values of 'auto' and the
      // element has no intrinsic width, but does have an intrinsic height and
      // intrinsic ratio; or if 'width' has a computed value of 'auto', 'height'
      // has some other computed value, and the element does have an intrinsic
      // ratio; then the used value of 'width' is: (used height) * (intrinsic
      // ratio).
      if ((computed_height_is_auto && !intrinsic_sizing_info.has_width &&
           intrinsic_sizing_info.has_height) ||
          !computed_height_is_auto) {
        LayoutUnit estimated_used_width =
            intrinsic_sizing_info.has_width
                ? LayoutUnit(constrained_size.Width())
                : ComputeConstrainedLogicalWidth(should_compute_preferred);
        LayoutUnit logical_height =
            ComputeReplacedLogicalHeight(estimated_used_width);
        return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
            ResolveWidthForRatio(logical_height,
                                 intrinsic_sizing_info.aspect_ratio),
            should_compute_preferred);
      }

      // If 'height' and 'width' both have computed values of 'auto' and the
      // element has an intrinsic ratio but no intrinsic height or width, then
      // the used value of 'width' is undefined in CSS 2.1. However, it is
      // suggested that, if the containing block's width does not itself depend
      // on the replaced element's width, then the used value of 'width' is
      // calculated from the constraint equation used for block-level,
      // non-replaced elements in normal flow.
      if (computed_height_is_auto && !intrinsic_sizing_info.has_width &&
          !intrinsic_sizing_info.has_height)
        return ComputeConstrainedLogicalWidth(should_compute_preferred);
    }

    // Otherwise, if 'width' has a computed value of 'auto', and the element has
    // an intrinsic width, then that intrinsic width is the used value of
    // 'width'.
    if (intrinsic_sizing_info.has_width)
      return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
          LayoutUnit(constrained_size.Width()), should_compute_preferred);

    // Otherwise, if 'width' has a computed value of 'auto', but none of the
    // conditions above are met, then the used value of 'width' becomes 300px.
    // If 300px is too wide to fit the device, UAs should use the width of the
    // largest rectangle that has a 2:1 ratio and fits the device instead.
    // Note: We fall through and instead return intrinsicLogicalWidth() here -
    // to preserve existing WebKit behavior, which might or might not be
    // correct, or desired.
    // Changing this to return cDefaultWidth, will affect lots of test results.
    // Eg. some tests assume that a blank <img> tag (which implies
    // width/height=auto) has no intrinsic size, which is wrong per CSS 2.1, but
    // matches our behavior since a long time.
  }

  return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
      IntrinsicLogicalWidth(), should_compute_preferred);
}

LayoutUnit LayoutReplaced::ComputeReplacedLogicalHeight(
    LayoutUnit estimated_used_width) const {
  // 10.5 Content height: the 'height' property:
  // http://www.w3.org/TR/CSS21/visudet.html#propdef-height
  if (HasReplacedLogicalHeight())
    return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
        ComputeReplacedLogicalHeightUsing(kMainOrPreferredSize,
                                          Style()->LogicalHeight()));

  LayoutReplaced* content_layout_object = EmbeddedReplacedContent();

  // 10.6.2 Inline, replaced elements:
  // http://www.w3.org/TR/CSS21/visudet.html#inline-replaced-height
  IntrinsicSizingInfo intrinsic_sizing_info;
  ComputeIntrinsicSizingInfoForReplacedContent(content_layout_object,
                                               intrinsic_sizing_info);
  FloatSize constrained_size =
      ConstrainIntrinsicSizeToMinMax(intrinsic_sizing_info);

  bool width_is_auto = Style()->LogicalWidth().IsAuto();

  // If 'height' and 'width' both have computed values of 'auto' and the element
  // also has an intrinsic height, then that intrinsic height is the used value
  // of 'height'.
  if (width_is_auto && intrinsic_sizing_info.has_height)
    return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
        LayoutUnit(constrained_size.Height()));

  // Otherwise, if 'height' has a computed value of 'auto', and the element has
  // an intrinsic ratio then the used value of 'height' is:
  // (used width) / (intrinsic ratio)
  if (!intrinsic_sizing_info.aspect_ratio.IsEmpty()) {
    LayoutUnit used_width =
        estimated_used_width ? estimated_used_width : AvailableLogicalWidth();
    return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
        ResolveHeightForRatio(used_width, intrinsic_sizing_info.aspect_ratio));
  }

  // Otherwise, if 'height' has a computed value of 'auto', and the element has
  // an intrinsic height, then that intrinsic height is the used value of
  // 'height'.
  if (intrinsic_sizing_info.has_height)
    return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
        LayoutUnit(constrained_size.Height()));

  // Otherwise, if 'height' has a computed value of 'auto', but none of the
  // conditions above are met, then the used value of 'height' must be set to
  // the height of the largest rectangle that has a 2:1 ratio, has a height not
  // greater than 150px, and has a width not greater than the device width.
  return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
      IntrinsicLogicalHeight());
}

void LayoutReplaced::ComputeIntrinsicLogicalWidths(
    LayoutUnit& min_logical_width,
    LayoutUnit& max_logical_width) const {
  min_logical_width = max_logical_width = IntrinsicLogicalWidth();
}

void LayoutReplaced::ComputePreferredLogicalWidths() {
  DCHECK(PreferredLogicalWidthsDirty());

  // We cannot resolve some logical width here (i.e. percent, fill-available or
  // fit-content) as the available logical width may not be set on our
  // containing block.
  const Length& logical_width = Style()->LogicalWidth();
  if (logical_width.IsPercentOrCalc() || logical_width.IsFillAvailable() ||
      logical_width.IsFitContent())
    ComputeIntrinsicLogicalWidths(min_preferred_logical_width_,
                                  max_preferred_logical_width_);
  else
    min_preferred_logical_width_ = max_preferred_logical_width_ =
        ComputeReplacedLogicalWidth(kComputePreferred);

  const ComputedStyle& style_to_use = StyleRef();
  if (style_to_use.LogicalWidth().IsPercentOrCalc() ||
      style_to_use.LogicalMaxWidth().IsPercentOrCalc())
    min_preferred_logical_width_ = LayoutUnit();

  if (style_to_use.LogicalMinWidth().IsFixed() &&
      style_to_use.LogicalMinWidth().Value() > 0) {
    max_preferred_logical_width_ =
        std::max(max_preferred_logical_width_,
                 AdjustContentBoxLogicalWidthForBoxSizing(
                     style_to_use.LogicalMinWidth().Value()));
    min_preferred_logical_width_ =
        std::max(min_preferred_logical_width_,
                 AdjustContentBoxLogicalWidthForBoxSizing(
                     style_to_use.LogicalMinWidth().Value()));
  }

  if (style_to_use.LogicalMaxWidth().IsFixed()) {
    max_preferred_logical_width_ =
        std::min(max_preferred_logical_width_,
                 AdjustContentBoxLogicalWidthForBoxSizing(
                     style_to_use.LogicalMaxWidth().Value()));
    min_preferred_logical_width_ =
        std::min(min_preferred_logical_width_,
                 AdjustContentBoxLogicalWidthForBoxSizing(
                     style_to_use.LogicalMaxWidth().Value()));
  }

  LayoutUnit border_and_padding = BorderAndPaddingLogicalWidth();
  min_preferred_logical_width_ += border_and_padding;
  max_preferred_logical_width_ += border_and_padding;

  ClearPreferredLogicalWidthsDirty();
}

PositionWithAffinity LayoutReplaced::PositionForPoint(
    const LayoutPoint& point) {
  // FIXME: This code is buggy if the replaced element is relative positioned.
  InlineBox* box = InlineBoxWrapper();
  RootInlineBox* root_box = box ? &box->Root() : 0;

  LayoutUnit top = root_box ? root_box->SelectionTop() : LogicalTop();
  LayoutUnit bottom = root_box ? root_box->SelectionBottom() : LogicalBottom();

  LayoutUnit block_direction_position = IsHorizontalWritingMode()
                                            ? point.Y() + Location().Y()
                                            : point.X() + Location().X();
  LayoutUnit line_direction_position = IsHorizontalWritingMode()
                                           ? point.X() + Location().X()
                                           : point.Y() + Location().Y();

  if (block_direction_position < top)
    return CreatePositionWithAffinity(
        CaretMinOffset());  // coordinates are above

  if (block_direction_position >= bottom)
    return CreatePositionWithAffinity(
        CaretMaxOffset());  // coordinates are below

  if (GetNode()) {
    if (line_direction_position <= LogicalLeft() + (LogicalWidth() / 2))
      return CreatePositionWithAffinity(0);
    return CreatePositionWithAffinity(1);
  }

  return LayoutBox::PositionForPoint(point);
}

LayoutRect LayoutReplaced::LocalSelectionRect() const {
  if (GetSelectionState() == SelectionState::kNone)
    return LayoutRect();

  if (!InlineBoxWrapper()) {
    // We're a block-level replaced element.  Just return our own dimensions.
    return LayoutRect(LayoutPoint(), Size());
  }

  RootInlineBox& root = InlineBoxWrapper()->Root();
  LayoutUnit new_logical_top =
      root.Block().Style()->IsFlippedBlocksWritingMode()
          ? InlineBoxWrapper()->LogicalBottom() - root.SelectionBottom()
          : root.SelectionTop() - InlineBoxWrapper()->LogicalTop();
  if (root.Block().Style()->IsHorizontalWritingMode())
    return LayoutRect(LayoutUnit(), new_logical_top, Size().Width(),
                      root.SelectionHeight());
  return LayoutRect(new_logical_top, LayoutUnit(), root.SelectionHeight(),
                    Size().Height());
}

void LayoutReplaced::SetSelectionState(SelectionState state) {
  // The selection state for our containing block hierarchy is updated by the
  // base class call.
  LayoutBox::SetSelectionState(state);
}

void LayoutReplaced::IntrinsicSizingInfo::Transpose() {
  size = size.TransposedSize();
  aspect_ratio = aspect_ratio.TransposedSize();
  std::swap(has_width, has_height);
}

}  // namespace blink
