// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/inline/ng_inline_node.h"

#include "core/layout/BidiRun.h"
#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutText.h"
#include "core/layout/LayoutTextFragment.h"
#include "core/layout/api/LineLayoutAPIShim.h"
#include "core/layout/line/LineInfo.h"
#include "core/layout/line/RootInlineBox.h"
#include "core/layout/ng/inline/ng_bidi_paragraph.h"
#include "core/layout/ng/inline/ng_inline_break_token.h"
#include "core/layout/ng/inline/ng_inline_item.h"
#include "core/layout/ng/inline/ng_inline_items_builder.h"
#include "core/layout/ng/inline/ng_inline_layout_algorithm.h"
#include "core/layout/ng/inline/ng_line_box_fragment.h"
#include "core/layout/ng/inline/ng_line_breaker.h"
#include "core/layout/ng/inline/ng_offset_mapping_result.h"
#include "core/layout/ng/inline/ng_physical_line_box_fragment.h"
#include "core/layout/ng/inline/ng_physical_text_fragment.h"
#include "core/layout/ng/inline/ng_text_fragment.h"
#include "core/layout/ng/layout_ng_block_flow.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_layout_result.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_physical_box_fragment.h"
#include "core/style/ComputedStyle.h"
#include "platform/fonts/shaping/HarfBuzzShaper.h"
#include "platform/fonts/shaping/ShapeResultSpacing.h"
#include "platform/wtf/text/CharacterNames.h"

namespace blink {

namespace {

struct FragmentPosition {
  NGLogicalOffset offset;
  LayoutUnit inline_size;
  NGBorderEdges border_edges;

  void operator+=(const NGBoxStrut& strut) {
    offset.inline_offset += strut.inline_start;
    offset.block_offset += strut.block_start;
  }
};

// Create BidiRuns from a list of NGPhysicalFragment.
// Produce a FragmentPosition map to place InlineBoxes.
void CreateBidiRuns(BidiRunList<BidiRun>* bidi_runs,
                    const Vector<RefPtr<NGPhysicalFragment>>& children,
                    const NGConstraintSpace& constraint_space,
                    NGLogicalOffset parent_offset,
                    const Vector<NGInlineItem>& items,
                    const Vector<unsigned, 32>& text_offsets,
                    Vector<FragmentPosition, 32>* positions_for_bidi_runs_out,
                    HashMap<LineLayoutItem, FragmentPosition>* positions_out) {
  for (const auto& child : children) {
    if (child->Type() == NGPhysicalFragment::kFragmentText) {
      const auto* physical_fragment = ToNGPhysicalTextFragment(child.Get());
      const NGInlineItem& item = items[physical_fragment->ItemIndex()];
      BidiRun* run;
      if (item.Type() == NGInlineItem::kText ||
          item.Type() == NGInlineItem::kControl) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsText());
        unsigned text_offset = text_offsets[physical_fragment->ItemIndex()];
        run = new BidiRun(physical_fragment->StartOffset() - text_offset,
                          physical_fragment->EndOffset() - text_offset,
                          item.BidiLevel(), LineLayoutItem(layout_object));
        layout_object->ClearNeedsLayout();
      } else if (item.Type() == NGInlineItem::kAtomicInline) {
        LayoutObject* layout_object = item.GetLayoutObject();
        DCHECK(layout_object->IsAtomicInlineLevel());
        run =
            new BidiRun(0, 1, item.BidiLevel(), LineLayoutItem(layout_object));
      } else {
        continue;
      }
      bidi_runs->AddRun(run);
      NGTextFragment fragment(constraint_space.WritingMode(),
                              physical_fragment);
      // Store text fragments in a vector in the same order as BidiRunList.
      // One LayoutText may produce multiple text fragments that they can't
      // be set to a map.
      positions_for_bidi_runs_out->push_back(FragmentPosition{
          fragment.Offset() + parent_offset, fragment.InlineSize()});
    } else {
      DCHECK_EQ(child->Type(), NGPhysicalFragment::kFragmentBox);
      NGPhysicalBoxFragment* physical_fragment =
          ToNGPhysicalBoxFragment(child.Get());
      NGBoxFragment fragment(constraint_space.WritingMode(), physical_fragment);
      NGLogicalOffset child_offset = fragment.Offset() + parent_offset;
      if (physical_fragment->Children().size()) {
        CreateBidiRuns(bidi_runs, physical_fragment->Children(),
                       constraint_space, child_offset, items, text_offsets,
                       positions_for_bidi_runs_out, positions_out);
      } else {
        // An empty inline needs a BidiRun for itself.
        LayoutObject* layout_object = physical_fragment->GetLayoutObject();
        BidiRun* run = new BidiRun(0, 1, 0, LineLayoutItem(layout_object));
        bidi_runs->AddRun(run);
      }
      // Store box fragments in a map by LineLayoutItem.
      positions_out->Set(LineLayoutItem(child->GetLayoutObject()),
                         FragmentPosition{child_offset, fragment.InlineSize(),
                                          fragment.BorderEdges()});
    }
  }
}

// Set the geometry to InlineBoxes by using the FragmentPosition map.
// When the map doesn't provide positions; i.e., when InlineFlowBox doesn't have
// corresponding box fragment, compute the union of children.
unsigned PlaceInlineBoxChildren(
    InlineFlowBox* parent,
    const Vector<FragmentPosition, 32>& positions_for_bidi_runs,
    const HashMap<LineLayoutItem, FragmentPosition>& positions,
    unsigned text_index = 0,
    bool set_parent_position_from_children = false) {
  LayoutUnit logical_left = LayoutUnit::Max();
  LayoutUnit logical_right = LayoutUnit::Min();
  LayoutUnit logical_top = LayoutUnit::Max();
  for (InlineBox* inline_box = parent->FirstChild(); inline_box;
       inline_box = inline_box->NextOnLine()) {
    if (inline_box->IsInlineFlowBox()) {
      InlineFlowBox* flow_box = ToInlineFlowBox(inline_box);
      const auto& iter = positions.find(inline_box->GetLineLayoutItem());
      if (iter != positions.end()) {
        const FragmentPosition& position = iter->value;
        inline_box->SetLogicalLeft(position.offset.inline_offset);
        inline_box->SetLogicalTop(position.offset.block_offset);
        inline_box->SetLogicalWidth(position.inline_size);
        flow_box->SetEdges(position.border_edges.line_left,
                           position.border_edges.line_right);
      }

      text_index =
          PlaceInlineBoxChildren(flow_box, positions_for_bidi_runs, positions,
                                 text_index, iter == positions.end());
    } else {
      const FragmentPosition& position = positions_for_bidi_runs[text_index++];
      inline_box->SetLogicalLeft(position.offset.inline_offset);
      inline_box->SetLogicalTop(position.offset.block_offset);
      inline_box->SetLogicalWidth(position.inline_size);
      if (inline_box->GetLineLayoutItem().IsBox()) {
        LineLayoutBox box(inline_box->GetLineLayoutItem());
        box.SetLocation(inline_box->Location());

        LayoutObject* layout_object = LineLayoutAPIShim::LayoutObjectFrom(box);
        if (layout_object->IsAtomicInlineLevel())
          ToLayoutBox(layout_object)->SetInlineBoxWrapper(inline_box);
      }
    }

    if (set_parent_position_from_children) {
      logical_left = std::min(inline_box->LogicalLeft(), logical_left);
      logical_right = std::max(inline_box->LogicalRight(), logical_right);
      logical_top = std::min(inline_box->LogicalTop(), logical_top);
    }
  }

  if (set_parent_position_from_children && logical_left != LayoutUnit::Max()) {
    logical_left -= parent->MarginBorderPaddingLogicalLeft();
    logical_right += parent->MarginBorderPaddingLogicalRight();
    parent->SetLogicalLeft(logical_left);
    parent->SetLogicalWidth(logical_right - logical_left);
    parent->SetLogicalTop(logical_top);
  }

  return text_index;
}

// Templated helper function for CollectInlinesInternal().
template <typename OffsetMappingBuilder>
void ClearNeedsLayoutIfUpdatingLayout(LayoutObject* node) {
  node->ClearNeedsLayout();
}

template <>
void ClearNeedsLayoutIfUpdatingLayout<NGOffsetMappingBuilder>(LayoutObject*) {}

// Templated helper function for CollectInlinesInternal().
template <typename OffsetMappingBuilder>
String GetTextForInlineCollection(const LayoutText& node) {
  return node.GetText();
}

// This function is a workaround of writing the whitespace-collapsed string back
// to LayoutText after inline collection, so that we can still recover the
// original text for building offset mapping.
// TODO(xiaochengh): Remove this function once we can:
// - paint inlines directly from the fragment tree, or
// - perform inline collection directly from DOM instead of LayoutText
template <>
String GetTextForInlineCollection<NGOffsetMappingBuilder>(
    const LayoutText& layout_text) {
  if (layout_text.Style()->TextSecurity() != ETextSecurity::kNone)
    return layout_text.GetText();

  // TODO(xiaochengh): Return the text-transformed string instead of DOM data
  // string.

  // Special handling for first-letter.
  if (layout_text.IsTextFragment()) {
    const LayoutTextFragment& text_fragment = ToLayoutTextFragment(layout_text);
    Text* node = text_fragment.AssociatedTextNode();
    if (!node) {
      // Reaches here if the LayoutTextFragment is due to a LayoutQuote.
      return layout_text.GetText();
    }
    unsigned first_letter_length = node->GetLayoutObject()->TextStartOffset();
    if (text_fragment.IsRemainingTextLayoutObject())
      return node->data().Substring(first_letter_length);
    return node->data().Substring(0, first_letter_length);
  }

  Node* node = layout_text.GetNode();
  if (!node || !node->IsTextNode())
    return layout_text.GetText();
  return ToText(node)->data();
}

// Templated helper function for CollectInlinesInternal().
template <typename OffsetMappingBuilder>
void AppendTextTransformedOffsetMapping(OffsetMappingBuilder*,
                                        const LayoutText*,
                                        const String&) {}

template <>
void AppendTextTransformedOffsetMapping<NGOffsetMappingBuilder>(
    NGOffsetMappingBuilder* concatenated_mapping_builder,
    const LayoutText* node,
    const String& text_transformed_string) {
  // TODO(xiaochengh): We are assuming that DOM data string and text-transformed
  // strings have the same length, which is incorrect.
  NGOffsetMappingBuilder text_transformed_mapping_builder;
  text_transformed_mapping_builder.AppendIdentityMapping(
      text_transformed_string.length());
  text_transformed_mapping_builder.Annotate(node);
  concatenated_mapping_builder->Concatenate(text_transformed_mapping_builder);
}

// The function is templated to indicate the purpose of collected inlines:
// - With EmptyOffsetMappingBuilder: updating layout;
// - With NGOffsetMappingBuilder: building offset mapping on clean layout.
//
// This allows code sharing between the two purposes with slightly different
// behaviors. For example, we clear a LayoutObject's need layout flags when
// updating layout, but don't do that when building offset mapping.
//
// There are also performance considerations, since template saves the overhead
// for condition checking and branching.
template <typename OffsetMappingBuilder>
LayoutBox* CollectInlinesInternal(
    LayoutBlockFlow* block,
    NGInlineItemsBuilderTemplate<OffsetMappingBuilder>* builder) {
  builder->EnterBlock(block->Style());
  LayoutObject* node = block->FirstChild();
  LayoutBox* next_box = nullptr;
  while (node) {
    if (node->IsText()) {
      builder->SetIsSVGText(node->IsSVGInlineText());

      LayoutText* layout_text = ToLayoutText(node);
      const String& text =
          GetTextForInlineCollection<OffsetMappingBuilder>(*layout_text);
      builder->Append(text, node->Style(), layout_text);
      ClearNeedsLayoutIfUpdatingLayout<OffsetMappingBuilder>(layout_text);

      AppendTextTransformedOffsetMapping(
          &builder->GetConcatenatedOffsetMappingBuilder(), layout_text, text);

    } else if (node->IsFloating()) {
      // Add floats and positioned objects in the same way as atomic inlines.
      // Because these objects need positions, they will be handled in
      // NGInlineLayoutAlgorithm.
      builder->AppendOpaque(NGInlineItem::kFloating,
                            kObjectReplacementCharacter, nullptr, node);

    } else if (node->IsOutOfFlowPositioned()) {
      builder->AppendOpaque(NGInlineItem::kOutOfFlowPositioned, nullptr, node);

    } else if (node->IsAtomicInlineLevel()) {
      // For atomic inlines add a unicode "object replacement character" to
      // signal the presence of a non-text object to the unicode bidi algorithm.
      builder->Append(NGInlineItem::kAtomicInline, kObjectReplacementCharacter,
                      node->Style(), node);

    } else if (!node->IsInline()) {
      // A block box found. End inline and transit to block layout.
      next_box = ToLayoutBox(node);
      break;

    } else {
      builder->EnterInline(node);

      // Traverse to children if they exist.
      if (LayoutObject* child = node->SlowFirstChild()) {
        node = child;
        continue;

      } else {
        // An empty inline node.
        ClearNeedsLayoutIfUpdatingLayout<OffsetMappingBuilder>(node);
      }

      builder->ExitInline(node);
    }

    // Find the next sibling, or parent, until we reach |block|.
    while (true) {
      if (LayoutObject* next = node->NextSibling()) {
        node = next;
        break;
      }
      node = node->Parent();
      if (node == block) {
        // Set |node| to |nullptr| to break out of the outer loop.
        node = nullptr;
        break;
      }
      DCHECK(node->IsInline());
      builder->ExitInline(node);
      ClearNeedsLayoutIfUpdatingLayout<OffsetMappingBuilder>(node);
    }
  }
  builder->ExitBlock();
  return next_box;
}

}  // namespace

NGInlineNode::NGInlineNode(LayoutNGBlockFlow* block)
    : NGLayoutInputNode(block, kInline) {
  DCHECK(block);
  if (!block->HasNGInlineNodeData())
    block->ResetNGInlineNodeData();
}

NGInlineItemRange NGInlineNode::Items(unsigned start, unsigned end) {
  return NGInlineItemRange(&MutableData().items_, start, end);
}

void NGInlineNode::InvalidatePrepareLayout() {
  ToLayoutNGBlockFlow(GetLayoutBlockFlow())->ResetNGInlineNodeData();
  MutableData().text_content_ = String();
  MutableData().items_.clear();
}

void NGInlineNode::PrepareLayout() {
  // Scan list of siblings collecting all in-flow non-atomic inlines. A single
  // NGInlineNode represent a collection of adjacent non-atomic inlines.
  CollectInlines();
  SegmentText();
  ShapeText();
}

const NGOffsetMappingResult& NGInlineNode::ComputeOffsetMappingIfNeeded() {
  DCHECK(!GetLayoutBlockFlow()->GetDocument().NeedsLayoutTreeUpdate());

  if (!Data().offset_mapping_) {
    // TODO(xiaochengh): ComputeOffsetMappingIfNeeded() discards the
    // NGInlineItems and text content built by |builder|, because they are
    // already there in NGInlineNodeData. For efficiency, we should make
    // |builder| not construct items and text content.
    Vector<NGInlineItem> items;
    NGInlineItemsBuilderForOffsetMapping builder(&items);
    CollectInlinesInternal(GetLayoutBlockFlow(), &builder);
    builder.ToString();

    NGOffsetMappingBuilder& mapping_builder =
        builder.GetConcatenatedOffsetMappingBuilder();
    mapping_builder.Composite(builder.GetOffsetMappingBuilder());

    MutableData().offset_mapping_ =
        WTF::MakeUnique<NGOffsetMappingResult>(mapping_builder.Build());
  }

  return *Data().offset_mapping_;
}

// Depth-first-scan of all LayoutInline and LayoutText nodes that make up this
// NGInlineNode object. Collects LayoutText items, merging them up into the
// parent LayoutInline where possible, and joining all text content in a single
// string to allow bidi resolution and shaping of the entire block.
void NGInlineNode::CollectInlines() {
  DCHECK(Data().text_content_.IsNull());
  DCHECK(Data().items_.IsEmpty());
  NGInlineItemsBuilder builder(&MutableData().items_);
  MutableData().next_sibling_ =
      CollectInlinesInternal(GetLayoutBlockFlow(), &builder);
  MutableData().text_content_ = builder.ToString();
  MutableData().is_bidi_enabled_ =
      !Data().text_content_.IsEmpty() &&
      !(Data().text_content_.Is8Bit() && !builder.HasBidiControls());
  MutableData().is_empty_inline_ = builder.IsEmptyInline();
}

void NGInlineNode::SegmentText() {
  NGInlineNodeData& data = MutableData();
  if (!data.is_bidi_enabled_) {
    data.SetBaseDirection(TextDirection::kLtr);
    return;
  }

  NGBidiParagraph bidi;
  data.text_content_.Ensure16Bit();
  if (!bidi.SetParagraph(data.text_content_, Style())) {
    // On failure, give up bidi resolving and reordering.
    data.is_bidi_enabled_ = false;
    data.SetBaseDirection(TextDirection::kLtr);
    return;
  }

  data.SetBaseDirection(bidi.BaseDirection());

  if (bidi.IsUnidirectional() && IsLtr(bidi.BaseDirection())) {
    // All runs are LTR, no need to reorder.
    data.is_bidi_enabled_ = false;
    return;
  }

  Vector<NGInlineItem>& items = data.items_;
  unsigned item_index = 0;
  for (unsigned start = 0; start < Data().text_content_.length();) {
    UBiDiLevel level;
    unsigned end = bidi.GetLogicalRun(start, &level);
    DCHECK_EQ(items[item_index].start_offset_, start);
    item_index = NGInlineItem::SetBidiLevel(items, item_index, end, level);
    start = end;
  }
  DCHECK_EQ(item_index, items.size());
}

void NGInlineNode::ShapeText() {
  // TODO(eae): Add support for shaping latin-1 text?
  MutableData().text_content_.Ensure16Bit();
  const String& text_content = Data().text_content_;

  // Shape each item with the full context of the entire node.
  HarfBuzzShaper shaper(text_content.Characters16(), text_content.length());
  ShapeResultSpacing<String> spacing(text_content);
  for (auto& item : MutableData().items_) {
    if (item.Type() != NGInlineItem::kText)
      continue;

    const Font& font = item.Style()->GetFont();
    RefPtr<ShapeResult> shape_result = shaper.Shape(
        &font, item.Direction(), item.StartOffset(), item.EndOffset());

    if (UNLIKELY(spacing.SetSpacing(font.GetFontDescription())))
      shape_result->ApplySpacing(spacing, item.Direction());

    item.shape_result_ = std::move(shape_result);
  }
}

RefPtr<NGLayoutResult> NGInlineNode::Layout(NGConstraintSpace* constraint_space,
                                            NGBreakToken* break_token) {
  // TODO(kojii): Invalidate PrepareLayout() more efficiently.
  InvalidatePrepareLayout();
  PrepareLayout();

  NGInlineLayoutAlgorithm algorithm(*this, constraint_space,
                                    ToNGInlineBreakToken(break_token));
  RefPtr<NGLayoutResult> result = algorithm.Layout();

  if (result->Status() == NGLayoutResult::kSuccess &&
      result->UnpositionedFloats().IsEmpty())
    CopyFragmentDataToLayoutBox(*constraint_space, result.Get());

  return result;
}

static LayoutUnit ComputeContentSize(NGInlineNode node,
                                     LayoutUnit available_inline_size) {
  const ComputedStyle& style = node.Style();
  NGWritingMode writing_mode = FromPlatformWritingMode(style.GetWritingMode());

  RefPtr<NGConstraintSpace> space =
      NGConstraintSpaceBuilder(writing_mode)
          .SetTextDirection(style.Direction())
          .SetAvailableSize({available_inline_size, NGSizeIndefinite})
          .ToConstraintSpace(writing_mode);

  NGFragmentBuilder container_builder(
      NGPhysicalFragment::NGFragmentType::kFragmentBox, node);
  container_builder.SetBfcOffset(NGLogicalOffset{LayoutUnit(), LayoutUnit()});

  Vector<RefPtr<NGUnpositionedFloat>> unpositioned_floats;
  NGLineBreaker line_breaker(node, space.Get(), &container_builder,
                             &unpositioned_floats);

  NGLineInfo line_info;
  LayoutUnit result;
  while (line_breaker.NextLine(&line_info, NGLogicalOffset())) {
    LayoutUnit inline_size = line_info.TextIndent();
    for (const NGInlineItemResult item_result : line_info.Results())
      inline_size += item_result.inline_size;
    result = std::max(inline_size, result);
  }
  return result;
}

MinMaxSize NGInlineNode::ComputeMinMaxSize() {
  // TODO(kojii): Invalidate PrepareLayout() more efficiently.
  InvalidatePrepareLayout();
  PrepareLayout();

  // Run line breaking with 0 and indefinite available width.

  // TODO(kojii): There are several ways to make this more efficient and faster
  // than runnning two line breaking.

  // Compute the max of inline sizes of all line boxes with 0 available inline
  // size. This gives the min-content, the width where lines wrap at every
  // break opportunity.
  MinMaxSize sizes;
  sizes.min_size = ComputeContentSize(*this, LayoutUnit());

  // Compute the sum of inline sizes of all inline boxes with no line breaks.
  // TODO(kojii): NGConstraintSpaceBuilder does not allow NGSizeIndefinite
  // inline available size. We can allow it, or make this more efficient
  // without using NGLineBreaker.
  sizes.max_size = ComputeContentSize(*this, LayoutUnit::Max());

  // Negative text-indent can make min > max. Ensure min is the minimum size.
  sizes.min_size = std::min(sizes.min_size, sizes.max_size);

  return sizes;
}

NGLayoutInputNode NGInlineNode::NextSibling() {
  // TODO(kojii): Invalidate PrepareLayout() more efficiently.
  InvalidatePrepareLayout();
  PrepareLayout();
  return NGBlockNode(Data().next_sibling_);
}

void NGInlineNode::CopyFragmentDataToLayoutBox(
    const NGConstraintSpace& constraint_space,
    NGLayoutResult* layout_result) {
  LayoutBlockFlow* block_flow = GetLayoutBlockFlow();
  block_flow->DeleteLineBoxTree();

  const Vector<NGInlineItem>& items = Data().items_;
  Vector<unsigned, 32> text_offsets(items.size());
  GetLayoutTextOffsets(&text_offsets);

  NGBoxStrut border_padding = ComputeBorders(constraint_space, Style()) +
                              ComputePadding(constraint_space, Style());

  FontBaseline baseline_type =
      IsHorizontalWritingMode(constraint_space.WritingMode())
          ? FontBaseline::kAlphabeticBaseline
          : FontBaseline::kIdeographicBaseline;

  Vector<FragmentPosition, 32> positions_for_bidi_runs;
  HashMap<LineLayoutItem, FragmentPosition> positions;
  BidiRunList<BidiRun> bidi_runs;
  LineInfo line_info;
  NGPhysicalBoxFragment* box_fragment =
      ToNGPhysicalBoxFragment(layout_result->PhysicalFragment().Get());
  for (const auto& container_child : box_fragment->Children()) {
    // Skip any float children we might have, these are handled by the wrapping
    // parent NGBlockNode.
    if (!container_child.Get()->IsLineBox())
      continue;

    NGPhysicalLineBoxFragment* physical_line_box =
        ToNGPhysicalLineBoxFragment(container_child.Get());
    NGLineBoxFragment line_box(constraint_space.WritingMode(),
                               physical_line_box);

    // Create a BidiRunList for this line.
    CreateBidiRuns(&bidi_runs, physical_line_box->Children(), constraint_space,
                   {line_box.InlineOffset(), LayoutUnit(0)}, items,
                   text_offsets, &positions_for_bidi_runs, &positions);
    // TODO(kojii): bidi needs to find the logical last run.
    bidi_runs.SetLogicallyLastRun(bidi_runs.LastRun());

    // Add border and padding to all positions.
    // Line box fragments are relative to this anonymous wrapper box fragment,
    // and the parent NGBlockLayoutAlgorithm offsets this wrapper by border and
    // padding, but inline boxes should be placed relative to the
    // LayoutBlockFlow.
    for (auto& position : positions_for_bidi_runs)
      position += border_padding;
    for (auto& position : positions.Values())
      position += border_padding;

    // Create a RootInlineBox from BidiRunList. InlineBoxes created for the
    // RootInlineBox are set to Bidirun::m_box.
    line_info.SetEmpty(false);
    // TODO(kojii): Implement setFirstLine, LastLine, etc.
    RootInlineBox* root_line_box =
        block_flow->ConstructLine(bidi_runs, line_info);

    // Copy fragments data to InlineBoxes.
    PlaceInlineBoxChildren(root_line_box, positions_for_bidi_runs, positions);

    // Copy to RootInlineBox.
    root_line_box->SetLogicalLeft(line_box.InlineOffset() +
                                  border_padding.inline_start);
    root_line_box->SetLogicalWidth(line_box.InlineSize());
    LayoutUnit line_top = line_box.BlockOffset() + border_padding.block_start;
    NGLineHeightMetrics line_metrics(Style(), baseline_type);
    const NGLineHeightMetrics& max_with_leading = physical_line_box->Metrics();
    LayoutUnit baseline = line_top + max_with_leading.ascent;
    root_line_box->SetLogicalTop(baseline - line_metrics.ascent);
    root_line_box->SetLineTopBottomPositions(
        baseline - line_metrics.ascent, baseline + line_metrics.descent,
        line_top, baseline + max_with_leading.descent);

    bidi_runs.DeleteRuns();
    positions_for_bidi_runs.clear();
    positions.clear();
  }
}

// Compute the delta of text offsets between NGInlineNode and LayoutText.
// This map is needed to produce InlineTextBox since its offsets are to
// LayoutText.
// TODO(kojii): Since NGInlineNode has text after whitespace collapsed, the
// length may not match with LayoutText. This function updates LayoutText to
// match, but this needs more careful coding, if we keep copying to layoutobject
// tree.
void NGInlineNode::GetLayoutTextOffsets(
    Vector<unsigned, 32>* text_offsets_out) {
  LayoutText* current_text = nullptr;
  unsigned current_offset = 0;
  const Vector<NGInlineItem>& items = Data().items_;

  for (unsigned i = 0; i < items.size(); i++) {
    const NGInlineItem& item = items[i];
    LayoutObject* next_object = item.GetLayoutObject();
    LayoutText* next_text = next_object && next_object->IsText()
                                ? ToLayoutText(next_object)
                                : nullptr;
    if (next_text != current_text) {
      if (current_text &&
          current_text->TextLength() != item.StartOffset() - current_offset) {
        current_text->SetTextInternal(
            Text(current_offset, item.StartOffset()).ToString().Impl());
      }
      current_text = next_text;
      current_offset = item.StartOffset();
    }
    (*text_offsets_out)[i] = current_offset;
  }
  if (current_text && current_text->TextLength() !=
                          Data().text_content_.length() - current_offset) {
    current_text->SetTextInternal(
        Text(current_offset, Data().text_content_.length()).ToString().Impl());
  }
}

void NGInlineNode::CheckConsistency() const {
#if DCHECK_IS_ON()
  const Vector<NGInlineItem>& items = Data().items_;
  for (const NGInlineItem& item : items) {
    DCHECK(!item.GetLayoutObject() || !item.Style() ||
           item.Style() == item.GetLayoutObject()->Style());
  }
#endif
}

String NGInlineNode::ToString() const {
  return String::Format("NGInlineNode");
}

// static
Optional<NGInlineNode> GetNGInlineNodeFor(const Node& node) {
  LayoutObject* layout_object = node.GetLayoutObject();
  if (!layout_object || !layout_object->IsInline())
    return WTF::nullopt;
  LayoutBox* box = layout_object->EnclosingBox();
  if (!box->IsLayoutNGBlockFlow())
    return WTF::nullopt;
  DCHECK(box);
  DCHECK(box->ChildrenInline());
  return NGInlineNode(ToLayoutNGBlockFlow(box));
}

const NGOffsetMappingUnit* NGInlineNode::GetMappingUnitForDOMOffset(
    const Node& node,
    unsigned offset) {
  // TODO(xiaochengh): Move/Reimplement AssociatedLayoutObjectOf in core/layout.
  LayoutObject* layout_object = AssociatedLayoutObjectOf(node, offset);
  if (!layout_object || !layout_object->IsText())
    return nullptr;

  DCHECK_EQ(layout_object->EnclosingBox(), GetLayoutBlockFlow());
  const auto& result = ComputeOffsetMappingIfNeeded();
  return result.GetMappingUnitForDOMOffset(ToLayoutText(layout_object), offset);
}

size_t NGInlineNode::GetTextContentOffset(const Node& node, unsigned offset) {
  const NGOffsetMappingUnit* unit = GetMappingUnitForDOMOffset(node, offset);
  if (!unit)
    return kNotFound;
  return unit->ConvertDOMOffsetToTextContent(offset);
}

}  // namespace blink
