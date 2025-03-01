// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_floats_utils.h"

#include "core/layout/MinMaxSize.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_layout_opportunity_iterator.h"
#include "core/layout/ng/ng_layout_result.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_space_utils.h"

namespace blink {
namespace {

// Adjusts the provided offset to the top edge alignment rule.
// Top edge alignment rule: the outer top of a floating box may not be higher
// than the outer top of any block or floated box generated by an element
// earlier in the source document.
NGLogicalOffset AdjustToTopEdgeAlignmentRule(const NGConstraintSpace& space,
                                             const NGLogicalOffset& offset) {
  NGLogicalOffset adjusted_offset = offset;
  LayoutUnit& adjusted_block_offset = adjusted_offset.block_offset;
  if (space.Exclusions()->last_left_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_left_float->rect.BlockStartOffset());
  if (space.Exclusions()->last_right_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_right_float->rect.BlockStartOffset());
  return adjusted_offset;
}

NGLayoutOpportunity FindLayoutOpportunityForFloat(
    const NGLogicalOffset& origin_offset,
    const NGConstraintSpace& space,
    const NGUnpositionedFloat& unpositioned_float,
    LayoutUnit inline_size) {
  NGLogicalOffset adjusted_origin_point =
      AdjustToTopEdgeAlignmentRule(space, origin_offset);
  WTF::Optional<LayoutUnit> clearance_offset =
      GetClearanceOffset(space.Exclusions(), unpositioned_float.ClearType());

  AdjustToClearance(clearance_offset, &adjusted_origin_point);

  // TODO(ikilpatrick): Don't include the block-start margin of a float which
  // has fragmented.
  return FindLayoutOpportunityForFragment(
      space.Exclusions().get(), unpositioned_float.available_size,
      adjusted_origin_point, unpositioned_float.margins,
      {inline_size, LayoutUnit()});
}

// Calculates the logical offset for opportunity.
NGLogicalOffset CalculateLogicalOffsetForOpportunity(
    const NGLayoutOpportunity& opportunity,
    const LayoutUnit float_offset,
    const LayoutUnit parent_bfc_block_offset,
    const NGUnpositionedFloat* unpositioned_float) {
  DCHECK(unpositioned_float);
  auto margins = unpositioned_float->margins;
  // Adjust to child's margin.
  NGLogicalOffset result = margins.InlineBlockStartOffset();

  // Offset from the opportunity's block/inline start.
  result += opportunity.offset;

  // Adjust to float: right offset if needed.
  result.inline_offset += float_offset;

  result -= {unpositioned_float->bfc_inline_offset, parent_bfc_block_offset};

  return result;
}

// Creates an exclusion from the fragment that will be placed in the provided
// layout opportunity.
NGExclusion CreateExclusion(const NGFragment& fragment,
                            const NGLayoutOpportunity& opportunity,
                            const LayoutUnit float_offset,
                            const NGBoxStrut& margins,
                            NGExclusion::Type exclusion_type) {
  NGExclusion exclusion;
  exclusion.type = exclusion_type;
  NGLogicalRect& rect = exclusion.rect;
  rect.offset = opportunity.offset;
  rect.offset.inline_offset += float_offset;

  // TODO(ikilpatrick): Don't include the block-start margin of a float which
  // has fragmented.
  rect.size.inline_size = fragment.InlineSize() + margins.InlineSum();
  rect.size.block_size = fragment.BlockSize() + margins.BlockSum();
  return exclusion;
}

// TODO(ikilpatrick): origin_block_offset looks wrong for fragmentation here.
WTF::Optional<LayoutUnit> CalculateFragmentationOffset(
    const LayoutUnit origin_block_offset,
    const NGUnpositionedFloat& unpositioned_float,
    const NGConstraintSpace& parent_space) {
  const ComputedStyle& style = unpositioned_float.node.Style();
  DCHECK(FromPlatformWritingMode(style.GetWritingMode()) ==
         parent_space.WritingMode());

  if (parent_space.HasBlockFragmentation()) {
    return parent_space.FragmentainerSpaceAvailable() - origin_block_offset;
  }

  return WTF::nullopt;
}

// Creates a constraint space for an unpositioned float.
RefPtr<NGConstraintSpace> CreateConstraintSpaceForFloat(
    const NGUnpositionedFloat& unpositioned_float,
    NGConstraintSpace* parent_space,
    WTF::Optional<LayoutUnit> fragmentation_offset = WTF::nullopt) {
  const ComputedStyle& style = unpositioned_float.node.Style();

  NGConstraintSpaceBuilder builder(parent_space);

  if (fragmentation_offset) {
    builder.SetFragmentainerSpaceAvailable(fragmentation_offset.value())
        .SetFragmentationType(parent_space->BlockFragmentationType());
  } else {
    builder.SetFragmentationType(NGFragmentationType::kFragmentNone);
  }

  return builder.SetPercentageResolutionSize(unpositioned_float.percentage_size)
      .SetAvailableSize(unpositioned_float.available_size)
      .SetIsNewFormattingContext(true)
      .SetIsShrinkToFit(true)
      .SetTextDirection(style.Direction())
      .ToConstraintSpace(FromPlatformWritingMode(style.GetWritingMode()));
}

}  // namespace

LayoutUnit ComputeInlineSizeForUnpositionedFloat(
    NGConstraintSpace* parent_space,
    NGUnpositionedFloat* unpositioned_float) {
  DCHECK(unpositioned_float);

  const ComputedStyle& style = unpositioned_float->node.Style();

  bool is_same_writing_mode = FromPlatformWritingMode(style.GetWritingMode()) ==
                              parent_space->WritingMode();

  // If we've already performed layout on the unpositioned float, just return
  // the cached value.
  if (unpositioned_float->layout_result) {
    DCHECK(!is_same_writing_mode);
    return NGFragment(parent_space->WritingMode(),
                      unpositioned_float->layout_result.value()
                          ->PhysicalFragment()
                          .Get())
        .InlineSize();
  }

  const RefPtr<NGConstraintSpace> space =
      CreateConstraintSpaceForFloat(*unpositioned_float, parent_space);

  // If the float has the same writing mode as the block formatting context we
  // shouldn't perform a full layout just yet. Our position may determine where
  // we fragment.
  if (is_same_writing_mode) {
    WTF::Optional<MinMaxSize> min_max_size;
    if (NeedMinMaxSize(*space.Get(), style))
      min_max_size = unpositioned_float->node.ComputeMinMaxSize();
    return ComputeInlineSizeForFragment(*space.Get(), style, min_max_size);
  }

  // If we are performing layout on a float to determine its inline size it
  // should never have fragmented.
  DCHECK(!unpositioned_float->token);

  // A float which has a different writing mode can't fragment, and we
  // (probably) need to perform a full layout in order to correctly determine
  // its inline size. We are able to cache this result on the
  // unpositioned_float at this stage.
  unpositioned_float->layout_result =
      unpositioned_float->node.Layout(space.Get());

  const NGPhysicalFragment* fragment =
      unpositioned_float->layout_result.value()->PhysicalFragment().Get();

  DCHECK(fragment->BreakToken()->IsFinished());

  return NGFragment(parent_space->WritingMode(), fragment).InlineSize();
}

NGPositionedFloat PositionFloat(LayoutUnit origin_block_offset,
                                LayoutUnit parent_bfc_block_offset,
                                NGUnpositionedFloat* unpositioned_float,
                                NGConstraintSpace* new_parent_space) {
  DCHECK(unpositioned_float);
  LayoutUnit inline_size = ComputeInlineSizeForUnpositionedFloat(
      new_parent_space, unpositioned_float);

  NGLogicalOffset origin_offset = {unpositioned_float->origin_bfc_inline_offset,
                                   origin_block_offset};

  // Find a layout opportunity that will fit our float.
  NGLayoutOpportunity opportunity = FindLayoutOpportunityForFloat(
      origin_offset, *new_parent_space, *unpositioned_float, inline_size);

#if DCHECK_IS_ON()
  bool is_same_writing_mode =
      FromPlatformWritingMode(
          unpositioned_float->node.Style().GetWritingMode()) ==
      new_parent_space->WritingMode();
#endif

  RefPtr<NGLayoutResult> layout_result;
  // We should only have a fragment if its writing mode is different, i.e. it
  // can't fragment.
  if (unpositioned_float->layout_result) {
#if DCHECK_IS_ON()
    DCHECK(!is_same_writing_mode);
#endif
    layout_result = unpositioned_float->layout_result.value();
  } else {
#if DCHECK_IS_ON()
    DCHECK(is_same_writing_mode);
#endif
    WTF::Optional<LayoutUnit> fragmentation_offset =
        CalculateFragmentationOffset(origin_block_offset, *unpositioned_float,
                                     *new_parent_space);

    RefPtr<NGConstraintSpace> space = CreateConstraintSpaceForFloat(
        *unpositioned_float, new_parent_space, fragmentation_offset);
    layout_result = unpositioned_float->node.Layout(
        space.Get(), unpositioned_float->token.Get());
  }

  NGBoxFragment float_fragment(
      new_parent_space->WritingMode(),
      ToNGPhysicalBoxFragment(layout_result.Get()->PhysicalFragment().Get()));

  // TODO(glebl): This should check for infinite opportunity instead.
  if (opportunity.IsEmpty()) {
    // Because of the implementation specific of the layout opportunity iterator
    // an empty opportunity can mean 2 things:
    // - search for layout opportunities is exhausted.
    // - opportunity has an infinite size. That's because CS is infinite.
    opportunity = NGLayoutOpportunity(
        NGLogicalOffset(),
        NGLogicalSize(float_fragment.InlineSize(), float_fragment.BlockSize()));
  }

  // Calculate the float offset if needed.
  LayoutUnit float_offset;
  if (unpositioned_float->IsRight()) {
    LayoutUnit float_margin_box_inline_size =
        float_fragment.InlineSize() + unpositioned_float->margins.InlineSum();
    float_offset = opportunity.size.inline_size - float_margin_box_inline_size;
  }

  // Add the float as an exclusion.
  const NGExclusion exclusion = CreateExclusion(
      float_fragment, opportunity, float_offset, unpositioned_float->margins,
      unpositioned_float->IsRight() ? NGExclusion::Type::kFloatRight
                                    : NGExclusion::Type::kFloatLeft);
  new_parent_space->AddExclusion(exclusion);

  NGLogicalOffset logical_offset = CalculateLogicalOffsetForOpportunity(
      opportunity, float_offset, parent_bfc_block_offset, unpositioned_float);

  return NGPositionedFloat(std::move(layout_result), logical_offset);
}

const Vector<NGPositionedFloat> PositionFloats(
    LayoutUnit origin_block_offset,
    LayoutUnit parent_bfc_block_offset,
    const Vector<RefPtr<NGUnpositionedFloat>>& unpositioned_floats,
    NGConstraintSpace* space) {
  Vector<NGPositionedFloat> positioned_floats;
  positioned_floats.ReserveCapacity(unpositioned_floats.size());

  for (auto& unpositioned_float : unpositioned_floats) {
    positioned_floats.push_back(PositionFloat(origin_block_offset,
                                              parent_bfc_block_offset,
                                              unpositioned_float.Get(), space));
  }

  return positioned_floats;
}

}  // namespace blink
