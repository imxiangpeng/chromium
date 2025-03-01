/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/commands/IndentOutdentCommand.h"

#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/ElementTraversal.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/commands/InsertListCommand.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/HTMLElement.h"
#include "core/layout/LayoutObject.h"

namespace blink {

using namespace HTMLNames;

// Returns true if |node| is UL, OL, or BLOCKQUOTE with "display:block".
// "Outdent" command considers <BLOCKQUOTE style="display:inline"> makes
// indentation.
static bool IsHTMLListOrBlockquoteElement(const Node* node) {
  if (!node || !node->IsHTMLElement())
    return false;
  if (!node->GetLayoutObject() || !node->GetLayoutObject()->IsLayoutBlock())
    return false;
  const HTMLElement& element = ToHTMLElement(*node);
  // TODO(yosin): We should check OL/UL element has "list-style-type" CSS
  // property to make sure they layout contents as list.
  return isHTMLUListElement(element) || isHTMLOListElement(element) ||
         element.HasTagName(blockquoteTag);
}

IndentOutdentCommand::IndentOutdentCommand(Document& document,
                                           EIndentType type_of_action)
    : ApplyBlockElementCommand(
          document,
          blockquoteTag,
          "margin: 0 0 0 40px; border: none; padding: 0px;"),
      type_of_action_(type_of_action) {}

bool IndentOutdentCommand::TryIndentingAsListItem(const Position& start,
                                                  const Position& end,
                                                  EditingState* editing_state) {
  // If our selection is not inside a list, bail out.
  Node* last_node_in_selected_paragraph = start.AnchorNode();
  HTMLElement* list_element = EnclosingList(last_node_in_selected_paragraph);
  if (!list_element)
    return false;

  // Find the block that we want to indent.  If it's not a list item (e.g., a
  // div inside a list item), we bail out.
  Element* selected_list_item = EnclosingBlock(last_node_in_selected_paragraph);

  // FIXME: we need to deal with the case where there is no li (malformed HTML)
  if (!isHTMLLIElement(selected_list_item))
    return false;

  // FIXME: previousElementSibling does not ignore non-rendered content like
  // <span></span>.  Should we?
  Element* previous_list =
      ElementTraversal::PreviousSibling(*selected_list_item);
  Element* next_list = ElementTraversal::NextSibling(*selected_list_item);

  // We should calculate visible range in list item because inserting new
  // list element will change visibility of list item, e.g. :first-child
  // CSS selector.
  HTMLElement* new_list = ToHTMLElement(GetDocument().createElement(
      list_element->TagQName(), kCreatedByCloneNode));
  InsertNodeBefore(new_list, selected_list_item, editing_state);
  if (editing_state->IsAborted())
    return false;

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // We should clone all the children of the list item for indenting purposes.
  // However, in case the current selection does not encompass all its children,
  // we need to explicitally handle the same. The original list item too would
  // require proper deletion in that case.
  const bool should_keep_selected_list =
      end.AnchorNode() == selected_list_item ||
      end.AnchorNode()->IsDescendantOf(selected_list_item->lastChild());

  const VisiblePosition& start_of_paragraph_to_move =
      CreateVisiblePosition(start);
  const VisiblePosition& end_of_paragraph_to_move =
      should_keep_selected_list
          ? CreateVisiblePosition(end)
          : VisiblePosition::AfterNode(*selected_list_item->lastChild());

  // The insertion of |newList| may change the computed style of other
  // elements, resulting in failure in visible canonicalization.
  if (start_of_paragraph_to_move.IsNull() ||
      end_of_paragraph_to_move.IsNull()) {
    editing_state->Abort();
    return false;
  }

  MoveParagraphWithClones(start_of_paragraph_to_move, end_of_paragraph_to_move,
                          new_list, selected_list_item, editing_state);
  if (editing_state->IsAborted())
    return false;

  if (!should_keep_selected_list) {
    RemoveNode(selected_list_item, editing_state);
    if (editing_state->IsAborted())
      return false;
  }

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (CanMergeLists(previous_list, new_list)) {
    MergeIdenticalElements(previous_list, new_list, editing_state);
    if (editing_state->IsAborted())
      return false;
  }

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (CanMergeLists(new_list, next_list)) {
    MergeIdenticalElements(new_list, next_list, editing_state);
    if (editing_state->IsAborted())
      return false;
  }

  return true;
}

void IndentOutdentCommand::IndentIntoBlockquote(const Position& start,
                                                const Position& end,
                                                HTMLElement*& target_blockquote,
                                                EditingState* editing_state) {
  Element* enclosing_cell = ToElement(EnclosingNodeOfType(start, &IsTableCell));
  Element* element_to_split_to;
  if (enclosing_cell)
    element_to_split_to = enclosing_cell;
  else if (EnclosingList(start.ComputeContainerNode()))
    element_to_split_to = EnclosingBlock(start.ComputeContainerNode());
  else
    element_to_split_to = RootEditableElementOf(start);

  if (!element_to_split_to)
    return;

  Node* outer_block =
      (start.ComputeContainerNode() == element_to_split_to)
          ? start.ComputeContainerNode()
          : SplitTreeToNode(start.ComputeContainerNode(), element_to_split_to);

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  VisiblePosition start_of_contents = CreateVisiblePosition(start);
  if (!target_blockquote) {
    // Create a new blockquote and insert it as a child of the root editable
    // element. We accomplish this by splitting all parents of the current
    // paragraph up to that point.
    target_blockquote = CreateBlockElement();
    if (outer_block == start.ComputeContainerNode()) {
      // When we apply indent to an empty <blockquote>, we should call
      // insertNodeAfter(). See http://crbug.com/625802 for more details.
      if (outer_block->HasTagName(blockquoteTag))
        InsertNodeAfter(target_blockquote, outer_block, editing_state);
      else
        InsertNodeAt(target_blockquote, start, editing_state);
    } else
      InsertNodeBefore(target_blockquote, outer_block, editing_state);
    if (editing_state->IsAborted())
      return;
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    start_of_contents = VisiblePosition::InParentAfterNode(*target_blockquote);
  }

  VisiblePosition end_of_contents = CreateVisiblePosition(end);
  if (start_of_contents.IsNull() || end_of_contents.IsNull())
    return;
  MoveParagraphWithClones(start_of_contents, end_of_contents, target_blockquote,
                          outer_block, editing_state);
}

void IndentOutdentCommand::OutdentParagraph(EditingState* editing_state) {
  VisiblePosition visible_start_of_paragraph =
      StartOfParagraph(EndingVisibleSelection().VisibleStart());
  VisiblePosition visible_end_of_paragraph =
      EndOfParagraph(visible_start_of_paragraph);

  HTMLElement* enclosing_element = ToHTMLElement(
      EnclosingNodeOfType(visible_start_of_paragraph.DeepEquivalent(),
                          &IsHTMLListOrBlockquoteElement));
  // We can't outdent if there is no place to go!
  if (!enclosing_element || !HasEditableStyle(*enclosing_element->parentNode()))
    return;

  // Use InsertListCommand to remove the selection from the list
  if (isHTMLOListElement(*enclosing_element)) {
    ApplyCommandToComposite(InsertListCommand::Create(
                                GetDocument(), InsertListCommand::kOrderedList),
                            editing_state);
    return;
  }
  if (isHTMLUListElement(*enclosing_element)) {
    ApplyCommandToComposite(
        InsertListCommand::Create(GetDocument(),
                                  InsertListCommand::kUnorderedList),
        editing_state);
    return;
  }

  // The selection is inside a blockquote i.e. enclosingNode is a blockquote
  VisiblePosition position_in_enclosing_block =
      VisiblePosition::FirstPositionInNode(*enclosing_element);
  // If the blockquote is inline, the start of the enclosing block coincides
  // with positionInEnclosingBlock.
  VisiblePosition start_of_enclosing_block =
      (enclosing_element->GetLayoutObject() &&
       enclosing_element->GetLayoutObject()->IsInline())
          ? position_in_enclosing_block
          : StartOfBlock(position_in_enclosing_block);
  VisiblePosition last_position_in_enclosing_block =
      VisiblePosition::LastPositionInNode(*enclosing_element);
  VisiblePosition end_of_enclosing_block =
      EndOfBlock(last_position_in_enclosing_block);
  if (visible_start_of_paragraph.DeepEquivalent() ==
          start_of_enclosing_block.DeepEquivalent() &&
      visible_end_of_paragraph.DeepEquivalent() ==
          end_of_enclosing_block.DeepEquivalent()) {
    // The blockquote doesn't contain anything outside the paragraph, so it can
    // be totally removed.
    Node* split_point = enclosing_element->nextSibling();
    RemoveNodePreservingChildren(enclosing_element, editing_state);
    if (editing_state->IsAborted())
      return;
    // outdentRegion() assumes it is operating on the first paragraph of an
    // enclosing blockquote, but if there are multiply nested blockquotes and
    // we've just removed one, then this assumption isn't true. By splitting the
    // next containing blockquote after this node, we keep this assumption true
    if (split_point) {
      if (Element* split_point_parent = split_point->parentElement()) {
        // We can't outdent if there is no place to go!
        if (split_point_parent->HasTagName(blockquoteTag) &&
            !split_point->HasTagName(blockquoteTag) &&
            HasEditableStyle(*split_point_parent->parentNode()))
          SplitElement(split_point_parent, split_point);
      }
    }

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    visible_start_of_paragraph =
        CreateVisiblePosition(visible_start_of_paragraph.DeepEquivalent());
    if (visible_start_of_paragraph.IsNotNull() &&
        !IsStartOfParagraph(visible_start_of_paragraph)) {
      InsertNodeAt(HTMLBRElement::Create(GetDocument()),
                   visible_start_of_paragraph.DeepEquivalent(), editing_state);
      if (editing_state->IsAborted())
        return;
    }

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    visible_end_of_paragraph =
        CreateVisiblePosition(visible_end_of_paragraph.DeepEquivalent());
    if (visible_end_of_paragraph.IsNotNull() &&
        !IsEndOfParagraph(visible_end_of_paragraph))
      InsertNodeAt(HTMLBRElement::Create(GetDocument()),
                   visible_end_of_paragraph.DeepEquivalent(), editing_state);
    return;
  }

  Node* split_blockquote_node = enclosing_element;
  if (Element* enclosing_block_flow = EnclosingBlock(
          visible_start_of_paragraph.DeepEquivalent().AnchorNode())) {
    if (enclosing_block_flow != enclosing_element) {
      split_blockquote_node =
          SplitTreeToNode(enclosing_block_flow, enclosing_element, true);
    } else {
      // We split the blockquote at where we start outdenting.
      Node* highest_inline_node = HighestEnclosingNodeOfType(
          visible_start_of_paragraph.DeepEquivalent(), IsInline,
          kCannotCrossEditingBoundary, enclosing_block_flow);
      SplitElement(
          enclosing_element,
          highest_inline_node
              ? highest_inline_node
              : visible_start_of_paragraph.DeepEquivalent().AnchorNode());
    }

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

    // Re-canonicalize visible{Start,End}OfParagraph, make them valid again
    // after DOM change.
    // TODO(xiaochengh): We should not store a VisiblePosition and later inspect
    // its properties when it is already invalidated.
    visible_start_of_paragraph = CreateVisiblePosition(
        visible_start_of_paragraph.ToPositionWithAffinity());
    visible_end_of_paragraph = CreateVisiblePosition(
        visible_end_of_paragraph.ToPositionWithAffinity());
  }

  // TODO(xiaochengh): We should not store a VisiblePosition and later inspect
  // its properties when it is already invalidated.
  VisiblePosition start_of_paragraph_to_move =
      StartOfParagraph(visible_start_of_paragraph);
  VisiblePosition end_of_paragraph_to_move =
      EndOfParagraph(visible_end_of_paragraph);
  if (start_of_paragraph_to_move.IsNull() || end_of_paragraph_to_move.IsNull())
    return;
  HTMLBRElement* placeholder = HTMLBRElement::Create(GetDocument());
  InsertNodeBefore(placeholder, split_blockquote_node, editing_state);
  if (editing_state->IsAborted())
    return;

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  start_of_paragraph_to_move = CreateVisiblePosition(
      start_of_paragraph_to_move.ToPositionWithAffinity());
  end_of_paragraph_to_move =
      CreateVisiblePosition(end_of_paragraph_to_move.ToPositionWithAffinity());
  MoveParagraph(start_of_paragraph_to_move, end_of_paragraph_to_move,
                VisiblePosition::BeforeNode(*placeholder), editing_state,
                kPreserveSelection);
}

// FIXME: We should merge this function with
// ApplyBlockElementCommand::formatSelection
void IndentOutdentCommand::OutdentRegion(
    const VisiblePosition& start_of_selection,
    const VisiblePosition& end_of_selection,
    EditingState* editing_state) {
  VisiblePosition end_of_current_paragraph = EndOfParagraph(start_of_selection);
  VisiblePosition end_of_last_paragraph = EndOfParagraph(end_of_selection);

  if (end_of_current_paragraph.DeepEquivalent() ==
      end_of_last_paragraph.DeepEquivalent()) {
    OutdentParagraph(editing_state);
    return;
  }

  Position original_selection_end = EndingVisibleSelection().End();
  Position end_after_selection =
      EndOfParagraph(NextPositionOf(end_of_last_paragraph)).DeepEquivalent();

  while (end_of_current_paragraph.DeepEquivalent() != end_after_selection) {
    PositionWithAffinity end_of_next_paragraph =
        EndOfParagraph(NextPositionOf(end_of_current_paragraph))
            .ToPositionWithAffinity();
    if (end_of_current_paragraph.DeepEquivalent() ==
        end_of_last_paragraph.DeepEquivalent()) {
      SelectionInDOMTree::Builder builder;
      if (original_selection_end.IsNotNull())
        builder.Collapse(original_selection_end);
      SetEndingSelection(builder.Build());
    } else {
      SetEndingSelection(
          SelectionInDOMTree::Builder()
              .Collapse(end_of_current_paragraph.DeepEquivalent())
              .Build());
    }

    OutdentParagraph(editing_state);
    if (editing_state->IsAborted())
      return;

    // outdentParagraph could move more than one paragraph if the paragraph
    // is in a list item. As a result, endAfterSelection and endOfNextParagraph
    // could refer to positions no longer in the document.
    if (end_after_selection.IsNotNull() && !end_after_selection.IsConnected())
      break;

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    if (end_of_next_paragraph.IsNotNull() &&
        !end_of_next_paragraph.IsConnected()) {
      end_of_current_paragraph =
          CreateVisiblePosition(EndingVisibleSelection().End());
      end_of_next_paragraph =
          EndOfParagraph(NextPositionOf(end_of_current_paragraph))
              .ToPositionWithAffinity();
    }
    end_of_current_paragraph = CreateVisiblePosition(end_of_next_paragraph);
  }
}

void IndentOutdentCommand::FormatSelection(
    const VisiblePosition& start_of_selection,
    const VisiblePosition& end_of_selection,
    EditingState* editing_state) {
  if (type_of_action_ == kIndent)
    ApplyBlockElementCommand::FormatSelection(start_of_selection,
                                              end_of_selection, editing_state);
  else
    OutdentRegion(start_of_selection, end_of_selection, editing_state);
}

void IndentOutdentCommand::FormatRange(const Position& start,
                                       const Position& end,
                                       const Position&,
                                       HTMLElement*& blockquote_for_next_indent,
                                       EditingState* editing_state) {
  bool indenting_as_list_item_result =
      TryIndentingAsListItem(start, end, editing_state);
  if (editing_state->IsAborted())
    return;
  if (indenting_as_list_item_result)
    blockquote_for_next_indent = nullptr;
  else
    IndentIntoBlockquote(start, end, blockquote_for_next_indent, editing_state);
}

InputEvent::InputType IndentOutdentCommand::GetInputType() const {
  return type_of_action_ == kIndent ? InputEvent::InputType::kFormatIndent
                                    : InputEvent::InputType::kFormatOutdent;
}

}  // namespace blink
