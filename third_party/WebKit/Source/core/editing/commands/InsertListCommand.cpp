/*
 * Copyright (C) 2006, 2010 Apple Inc. All rights reserved.
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

#include "core/editing/commands/InsertListCommand.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/ElementTraversal.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/html/HTMLBRElement.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLLIElement.h"
#include "core/html/HTMLUListElement.h"

namespace blink {

using namespace HTMLNames;

static Node* EnclosingListChild(Node* node, Node* list_node) {
  Node* list_child = EnclosingListChild(node);
  while (list_child && EnclosingList(list_child) != list_node)
    list_child = EnclosingListChild(list_child->parentNode());
  return list_child;
}

HTMLUListElement* InsertListCommand::FixOrphanedListChild(
    Node* node,
    EditingState* editing_state) {
  HTMLUListElement* list_element = HTMLUListElement::Create(GetDocument());
  InsertNodeBefore(list_element, node, editing_state);
  if (editing_state->IsAborted())
    return nullptr;
  RemoveNode(node, editing_state);
  if (editing_state->IsAborted())
    return nullptr;
  AppendNode(node, list_element, editing_state);
  if (editing_state->IsAborted())
    return nullptr;
  return list_element;
}

HTMLElement* InsertListCommand::MergeWithNeighboringLists(
    HTMLElement* passed_list,
    EditingState* editing_state) {
  HTMLElement* list = passed_list;
  Element* previous_list = ElementTraversal::PreviousSibling(*list);
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (CanMergeLists(previous_list, list)) {
    MergeIdenticalElements(previous_list, list, editing_state);
    if (editing_state->IsAborted())
      return nullptr;
  }

  if (!list)
    return nullptr;

  Element* next_sibling = ElementTraversal::NextSibling(*list);
  if (!next_sibling || !next_sibling->IsHTMLElement())
    return list;

  HTMLElement* next_list = ToHTMLElement(next_sibling);
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (CanMergeLists(list, next_list)) {
    MergeIdenticalElements(list, next_list, editing_state);
    if (editing_state->IsAborted())
      return nullptr;
    return next_list;
  }
  return list;
}

bool InsertListCommand::SelectionHasListOfType(
    const Position& selection_start,
    const Position& selection_end,
    const HTMLQualifiedName& list_tag) {
  DCHECK_LE(selection_start, selection_end);
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetDocument().Lifecycle());

  VisiblePosition start = CreateVisiblePosition(selection_start);

  if (!EnclosingList(start.DeepEquivalent().AnchorNode()))
    return false;

  VisiblePosition end = StartOfParagraph(CreateVisiblePosition(selection_end));
  while (start.IsNotNull() && start.DeepEquivalent() != end.DeepEquivalent()) {
    HTMLElement* list_element =
        EnclosingList(start.DeepEquivalent().AnchorNode());
    if (!list_element || !list_element->HasTagName(list_tag))
      return false;
    start = StartOfNextParagraph(start);
  }

  return true;
}

InsertListCommand::InsertListCommand(Document& document, Type type)
    : CompositeEditCommand(document), type_(type) {}

static bool InSameTreeAndOrdered(const Position& should_be_former,
                                 const Position& should_be_later) {
  // Input positions must be canonical positions.
  DCHECK_EQ(should_be_former, CanonicalPositionOf(should_be_former))
      << should_be_former;
  DCHECK_EQ(should_be_later, CanonicalPositionOf(should_be_later))
      << should_be_later;
  return Position::CommonAncestorTreeScope(should_be_former, should_be_later) &&
         ComparePositions(should_be_former, should_be_later) <= 0;
}

void InsertListCommand::DoApply(EditingState* editing_state) {
  // Only entry points are Editor::Command::execute and
  // IndentOutdentCommand::outdentParagraph, both of which ensure clean layout.
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());

  if (!EndingVisibleSelection().IsNonOrphanedCaretOrRange())
    return;

  if (!EndingVisibleSelection().RootEditableElement())
    return;

  VisiblePosition visible_end = EndingVisibleSelection().VisibleEnd();
  VisiblePosition visible_start = EndingVisibleSelection().VisibleStart();
  // When a selection ends at the start of a paragraph, we rarely paint
  // the selection gap before that paragraph, because there often is no gap.
  // In a case like this, it's not obvious to the user that the selection
  // ends "inside" that paragraph, so it would be confusing if
  // InsertUn{Ordered}List operated on that paragraph.
  // FIXME: We paint the gap before some paragraphs that are indented with left
  // margin/padding, but not others.  We should make the gap painting more
  // consistent and then use a left margin/padding rule here.
  if (visible_end.DeepEquivalent() != visible_start.DeepEquivalent() &&
      IsStartOfParagraph(visible_end, kCanSkipOverEditingBoundary)) {
    const VisiblePosition& new_end =
        PreviousPositionOf(visible_end, kCannotCrossEditingBoundary);
    SelectionInDOMTree::Builder builder;
    builder.SetIsDirectional(EndingVisibleSelection().IsDirectional());
    builder.Collapse(visible_start.ToPositionWithAffinity());
    if (new_end.IsNotNull())
      builder.Extend(new_end.DeepEquivalent());
    SetEndingSelection(builder.Build());
    if (!EndingVisibleSelection().RootEditableElement())
      return;
  }

  const HTMLQualifiedName& list_tag = (type_ == kOrderedList) ? olTag : ulTag;
  if (EndingVisibleSelection().IsRange()) {
    bool force_list_creation = false;
    VisibleSelection selection =
        SelectionForParagraphIteration(EndingVisibleSelection());
    DCHECK(selection.IsRange());

    VisiblePosition visible_start_of_selection = selection.VisibleStart();
    VisiblePosition visible_end_of_selection = selection.VisibleEnd();
    PositionWithAffinity start_of_selection =
        visible_start_of_selection.ToPositionWithAffinity();
    PositionWithAffinity end_of_selection =
        visible_end_of_selection.ToPositionWithAffinity();
    Position start_of_last_paragraph =
        StartOfParagraph(visible_end_of_selection, kCanSkipOverEditingBoundary)
            .DeepEquivalent();

    Range* current_selection =
        CreateRange(FirstEphemeralRangeOf(EndingVisibleSelection()));
    ContainerNode* scope_for_start_of_selection = nullptr;
    ContainerNode* scope_for_end_of_selection = nullptr;
    // FIXME: This is an inefficient way to keep selection alive because
    // indexForVisiblePosition walks from the beginning of the document to the
    // visibleEndOfSelection everytime this code is executed. But not using
    // index is hard because there are so many ways we can lose selection inside
    // doApplyForSingleParagraph.
    int index_for_start_of_selection = IndexForVisiblePosition(
        visible_start_of_selection, scope_for_start_of_selection);
    int index_for_end_of_selection = IndexForVisiblePosition(
        visible_end_of_selection, scope_for_end_of_selection);

    if (StartOfParagraph(visible_start_of_selection,
                         kCanSkipOverEditingBoundary)
            .DeepEquivalent() != start_of_last_paragraph) {
      force_list_creation =
          !SelectionHasListOfType(selection.Start(), selection.End(), list_tag);

      VisiblePosition start_of_current_paragraph = visible_start_of_selection;
      while (InSameTreeAndOrdered(start_of_current_paragraph.DeepEquivalent(),
                                  start_of_last_paragraph) &&
             !InSameParagraph(start_of_current_paragraph,
                              CreateVisiblePosition(start_of_last_paragraph),
                              kCanCrossEditingBoundary)) {
        // doApply() may operate on and remove the last paragraph of the
        // selection from the document if it's in the same list item as
        // startOfCurrentParagraph. Return early to avoid an infinite loop and
        // because there is no more work to be done.
        // FIXME(<rdar://problem/5983974>): The endingSelection() may be
        // incorrect here.  Compute the new location of visibleEndOfSelection
        // and use it as the end of the new selection.
        if (!start_of_last_paragraph.IsConnected())
          return;
        SetEndingSelection(
            SelectionInDOMTree::Builder()
                .Collapse(start_of_current_paragraph.DeepEquivalent())
                .Build());

        // Save and restore visibleEndOfSelection and startOfLastParagraph when
        // necessary since moveParagraph and movePragraphWithClones can remove
        // nodes.
        bool single_paragraph_result = DoApplyForSingleParagraph(
            force_list_creation, list_tag, *current_selection, editing_state);
        if (editing_state->IsAborted())
          return;
        if (!single_paragraph_result)
          break;

        GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

        // Make |visibleEndOfSelection| valid again.
        if (!end_of_selection.IsConnected() ||
            !start_of_last_paragraph.IsConnected()) {
          visible_end_of_selection = VisiblePositionForIndex(
              index_for_end_of_selection, scope_for_end_of_selection);
          end_of_selection = visible_end_of_selection.ToPositionWithAffinity();
          // If visibleEndOfSelection is null, then some contents have been
          // deleted from the document. This should never happen and if it did,
          // exit early immediately because we've lost the loop invariant.
          DCHECK(visible_end_of_selection.IsNotNull());
          if (visible_end_of_selection.IsNull() ||
              !RootEditableElementOf(visible_end_of_selection))
            return;
          start_of_last_paragraph =
              StartOfParagraph(visible_end_of_selection,
                               kCanSkipOverEditingBoundary)
                  .DeepEquivalent();
        } else {
          visible_end_of_selection = CreateVisiblePosition(end_of_selection);
        }

        start_of_current_paragraph =
            StartOfNextParagraph(EndingVisibleSelection().VisibleStart());
      }
      SetEndingSelection(
          SelectionInDOMTree::Builder()
              .Collapse(visible_end_of_selection.DeepEquivalent())
              .Build());
    }
    DoApplyForSingleParagraph(force_list_creation, list_tag, *current_selection,
                              editing_state);
    if (editing_state->IsAborted())
      return;

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

    // Fetch the end of the selection, for the reason mentioned above.
    if (!end_of_selection.IsConnected()) {
      visible_end_of_selection = VisiblePositionForIndex(
          index_for_end_of_selection, scope_for_end_of_selection);
      if (visible_end_of_selection.IsNull())
        return;
    } else {
      visible_end_of_selection = CreateVisiblePosition(end_of_selection);
    }

    if (!start_of_selection.IsConnected()) {
      visible_start_of_selection = VisiblePositionForIndex(
          index_for_start_of_selection, scope_for_start_of_selection);
      if (visible_start_of_selection.IsNull())
        return;
    } else {
      visible_start_of_selection = CreateVisiblePosition(start_of_selection);
    }

    SetEndingSelection(
        SelectionInDOMTree::Builder()
            .SetAffinity(visible_start_of_selection.Affinity())
            .SetBaseAndExtentDeprecated(
                visible_start_of_selection.DeepEquivalent(),
                visible_end_of_selection.DeepEquivalent())
            .SetIsDirectional(EndingVisibleSelection().IsDirectional())
            .Build());
    return;
  }

  Range* const range =
      CreateRange(FirstEphemeralRangeOf(EndingVisibleSelection()));
  DCHECK(range);
  DoApplyForSingleParagraph(false, list_tag, *range, editing_state);
}

InputEvent::InputType InsertListCommand::GetInputType() const {
  return type_ == kOrderedList ? InputEvent::InputType::kInsertOrderedList
                               : InputEvent::InputType::kInsertUnorderedList;
}

bool InsertListCommand::DoApplyForSingleParagraph(
    bool force_create_list,
    const HTMLQualifiedName& list_tag,
    Range& current_selection,
    EditingState* editing_state) {
  // FIXME: This will produce unexpected results for a selection that starts
  // just before a table and ends inside the first cell,
  // selectionForParagraphIteration should probably be renamed and deployed
  // inside setEndingSelection().
  Node* selection_node = EndingVisibleSelection().Start().AnchorNode();
  Node* list_child_node = EnclosingListChild(selection_node);
  bool switch_list_type = false;
  if (list_child_node) {
    if (!HasEditableStyle(*list_child_node->parentNode()))
      return false;
    // Remove the list child.
    HTMLElement* list_element = EnclosingList(list_child_node);
    if (list_element) {
      if (!HasEditableStyle(*list_element)) {
        // Since, |listElement| is uneditable, we can't move |listChild|
        // out from |listElement|.
        return false;
      }
      if (!HasEditableStyle(*list_element->parentNode())) {
        // Since parent of |listElement| is uneditable, we can not remove
        // |listElement| for switching list type neither unlistify.
        return false;
      }
    }
    if (!list_element) {
      list_element = FixOrphanedListChild(list_child_node, editing_state);
      if (editing_state->IsAborted())
        return false;
      list_element = MergeWithNeighboringLists(list_element, editing_state);
      if (editing_state->IsAborted())
        return false;
      GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    }
    DCHECK(HasEditableStyle(*list_element));
    DCHECK(HasEditableStyle(*list_element->parentNode()));
    if (!list_element->HasTagName(list_tag)) {
      // |listChildNode| will be removed from the list and a list of type
      // |m_type| will be created.
      switch_list_type = true;
    }

    // If the list is of the desired type, and we are not removing the list,
    // then exit early.
    if (!switch_list_type && force_create_list)
      return true;

    // If the entire list is selected, then convert the whole list.
    if (switch_list_type &&
        IsNodeVisiblyContainedWithin(*list_element, current_selection)) {
      bool range_start_is_in_list =
          VisiblePositionBeforeNode(*list_element).DeepEquivalent() ==
          CreateVisiblePosition(current_selection.StartPosition())
              .DeepEquivalent();
      bool range_end_is_in_list =
          VisiblePositionAfterNode(*list_element).DeepEquivalent() ==
          CreateVisiblePosition(current_selection.EndPosition())
              .DeepEquivalent();

      HTMLElement* new_list = CreateHTMLElement(GetDocument(), list_tag);
      InsertNodeBefore(new_list, list_element, editing_state);
      if (editing_state->IsAborted())
        return false;

      GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
      Node* first_child_in_list =
          EnclosingListChild(VisiblePosition::FirstPositionInNode(*list_element)
                                 .DeepEquivalent()
                                 .AnchorNode(),
                             list_element);
      Element* outer_block =
          first_child_in_list && IsBlockFlowElement(*first_child_in_list)
              ? ToElement(first_child_in_list)
              : list_element;

      MoveParagraphWithClones(
          VisiblePosition::FirstPositionInNode(*list_element),
          VisiblePosition::LastPositionInNode(*list_element), new_list,
          outer_block, editing_state);
      if (editing_state->IsAborted())
        return false;

      // Manually remove listNode because moveParagraphWithClones sometimes
      // leaves it behind in the document. See the bug 33668 and
      // editing/execCommand/insert-list-orphaned-item-with-nested-lists.html.
      // FIXME: This might be a bug in moveParagraphWithClones or
      // deleteSelection.
      if (list_element && list_element->isConnected()) {
        RemoveNode(list_element, editing_state);
        if (editing_state->IsAborted())
          return false;
      }

      new_list = MergeWithNeighboringLists(new_list, editing_state);
      if (editing_state->IsAborted())
        return false;

      // Restore the start and the end of current selection if they started
      // inside listNode because moveParagraphWithClones could have removed
      // them.
      if (range_start_is_in_list && new_list)
        current_selection.setStart(new_list, 0, IGNORE_EXCEPTION_FOR_TESTING);
      if (range_end_is_in_list && new_list) {
        current_selection.setEnd(new_list,
                                 Position::LastOffsetInNode(*new_list),
                                 IGNORE_EXCEPTION_FOR_TESTING);
      }

      SetEndingSelection(SelectionInDOMTree::Builder()
                             .Collapse(Position::FirstPositionInNode(*new_list))
                             .Build());

      return true;
    }

    UnlistifyParagraph(EndingVisibleSelection().VisibleStart(), list_element,
                       list_child_node, editing_state);
    if (editing_state->IsAborted())
      return false;
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  }

  if (!list_child_node || switch_list_type || force_create_list) {
    ListifyParagraph(EndingVisibleSelection().VisibleStart(), list_tag,
                     editing_state);
  }

  return true;
}

void InsertListCommand::UnlistifyParagraph(
    const VisiblePosition& original_start,
    HTMLElement* list_element,
    Node* list_child_node,
    EditingState* editing_state) {
  // Since, unlistify paragraph inserts nodes into parent and removes node
  // from parent, if parent of |listElement| should be editable.
  DCHECK(HasEditableStyle(*list_element->parentNode()));
  Node* next_list_child;
  Node* previous_list_child;
  VisiblePosition start;
  VisiblePosition end;
  DCHECK(list_child_node);
  if (isHTMLLIElement(*list_child_node)) {
    start = VisiblePosition::FirstPositionInNode(*list_child_node);
    end = VisiblePosition::LastPositionInNode(*list_child_node);
    next_list_child = list_child_node->nextSibling();
    previous_list_child = list_child_node->previousSibling();
  } else {
    // A paragraph is visually a list item minus a list marker.  The paragraph
    // will be moved.
    start = StartOfParagraph(original_start, kCanSkipOverEditingBoundary);
    end = EndOfParagraph(start, kCanSkipOverEditingBoundary);
    next_list_child = EnclosingListChild(
        NextPositionOf(end).DeepEquivalent().AnchorNode(), list_element);
    DCHECK_NE(next_list_child, list_child_node);
    previous_list_child = EnclosingListChild(
        PreviousPositionOf(start).DeepEquivalent().AnchorNode(), list_element);
    DCHECK_NE(previous_list_child, list_child_node);
  }

  // Helpers for making |start| and |end| valid again after DOM changes.
  PositionWithAffinity start_position = start.ToPositionWithAffinity();
  PositionWithAffinity end_position = end.ToPositionWithAffinity();

  // When removing a list, we must always create a placeholder to act as a point
  // of insertion for the list content being removed.
  HTMLBRElement* placeholder = HTMLBRElement::Create(GetDocument());
  HTMLElement* element_to_insert = placeholder;
  // If the content of the list item will be moved into another list, put it in
  // a list item so that we don't create an orphaned list child.
  if (EnclosingList(list_element)) {
    element_to_insert = HTMLLIElement::Create(GetDocument());
    AppendNode(placeholder, element_to_insert, editing_state);
    if (editing_state->IsAborted())
      return;
  }

  if (next_list_child && previous_list_child) {
    // We want to pull listChildNode out of listNode, and place it before
    // nextListChild and after previousListChild, so we split listNode and
    // insert it between the two lists.
    // But to split listNode, we must first split ancestors of listChildNode
    // between it and listNode, if any exist.
    // FIXME: We appear to split at nextListChild as opposed to listChildNode so
    // that when we remove listChildNode below in moveParagraphs,
    // previousListChild will be removed along with it if it is unrendered. But
    // we ought to remove nextListChild too, if it is unrendered.
    SplitElement(list_element, SplitTreeToNode(next_list_child, list_element));
    InsertNodeBefore(element_to_insert, list_element, editing_state);
  } else if (next_list_child || list_child_node->parentNode() != list_element) {
    // Just because listChildNode has no previousListChild doesn't mean there
    // isn't any content in listNode that comes before listChildNode, as
    // listChildNode could have ancestors between it and listNode. So, we split
    // up to listNode before inserting the placeholder where we're about to move
    // listChildNode to.
    if (list_child_node->parentNode() != list_element)
      SplitElement(list_element,
                   SplitTreeToNode(list_child_node, list_element));
    InsertNodeBefore(element_to_insert, list_element, editing_state);
  } else {
    InsertNodeAfter(element_to_insert, list_element, editing_state);
  }
  if (editing_state->IsAborted())
    return;

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // Make |start| and |end| valid again.
  start = CreateVisiblePosition(start_position);
  end = CreateVisiblePosition(end_position);

  VisiblePosition insertion_point = VisiblePosition::BeforeNode(*placeholder);
  MoveParagraphs(start, end, insertion_point, editing_state, kPreserveSelection,
                 kPreserveStyle, list_child_node);
}

static HTMLElement* AdjacentEnclosingList(const VisiblePosition& pos,
                                          const VisiblePosition& adjacent_pos,
                                          const HTMLQualifiedName& list_tag) {
  HTMLElement* list_element =
      OutermostEnclosingList(adjacent_pos.DeepEquivalent().AnchorNode());

  if (!list_element)
    return 0;

  Element* previous_cell = EnclosingTableCell(pos.DeepEquivalent());
  Element* current_cell = EnclosingTableCell(adjacent_pos.DeepEquivalent());

  if (!list_element->HasTagName(list_tag) ||
      list_element->contains(pos.DeepEquivalent().AnchorNode()) ||
      previous_cell != current_cell ||
      EnclosingList(list_element) !=
          EnclosingList(pos.DeepEquivalent().AnchorNode()))
    return 0;

  return list_element;
}

void InsertListCommand::ListifyParagraph(const VisiblePosition& original_start,
                                         const HTMLQualifiedName& list_tag,
                                         EditingState* editing_state) {
  const VisiblePosition& start =
      StartOfParagraph(original_start, kCanSkipOverEditingBoundary);
  const VisiblePosition& end =
      EndOfParagraph(start, kCanSkipOverEditingBoundary);

  if (start.IsNull() || end.IsNull())
    return;

  // Check for adjoining lists.
  HTMLElement* const previous_list = AdjacentEnclosingList(
      start, PreviousPositionOf(start, kCannotCrossEditingBoundary), list_tag);
  HTMLElement* const next_list = AdjacentEnclosingList(
      start, NextPositionOf(end, kCannotCrossEditingBoundary), list_tag);
  if (previous_list || next_list) {
    // Place list item into adjoining lists.
    HTMLLIElement* list_item_element = HTMLLIElement::Create(GetDocument());
    if (previous_list)
      AppendNode(list_item_element, previous_list, editing_state);
    else
      InsertNodeAt(list_item_element, Position::BeforeNode(*next_list),
                   editing_state);
    if (editing_state->IsAborted())
      return;

    MoveParagraphOverPositionIntoEmptyListItem(start, list_item_element,
                                               editing_state);
    if (editing_state->IsAborted())
      return;

    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    if (CanMergeLists(previous_list, next_list))
      MergeIdenticalElements(previous_list, next_list, editing_state);

    return;
  }

  // Create new list element.

  // Inserting the list into an empty paragraph that isn't held open
  // by a br or a '\n', will invalidate start and end.  Insert
  // a placeholder and then recompute start and end.
  Position start_pos = start.DeepEquivalent();
  if (start.DeepEquivalent() == end.DeepEquivalent() &&
      IsEnclosingBlock(start.DeepEquivalent().AnchorNode())) {
    HTMLBRElement* placeholder =
        InsertBlockPlaceholder(start_pos, editing_state);
    if (editing_state->IsAborted())
      return;
    start_pos = Position::BeforeNode(*placeholder);
  }

  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  // Insert the list at a position visually equivalent to start of the
  // paragraph that is being moved into the list.
  // Try to avoid inserting it somewhere where it will be surrounded by
  // inline ancestors of start, since it is easier for editing to produce
  // clean markup when inline elements are pushed down as far as possible.
  Position insertion_pos(MostBackwardCaretPosition(start_pos));
  // Also avoid the temporary <span> element created by 'unlistifyParagraph'.
  // This element can be selected by mostBackwardCaretPosition when startPor
  // points to a element with previous siblings or ancestors with siblings.
  // |-A
  // | |-B
  // | +-C (insertion point)
  // |   |-D (*)
  if (isHTMLSpanElement(insertion_pos.AnchorNode())) {
    insertion_pos =
        Position::InParentBeforeNode(*insertion_pos.ComputeContainerNode());
  }
  // Also avoid the containing list item.
  Node* const list_child = EnclosingListChild(insertion_pos.AnchorNode());
  if (isHTMLLIElement(list_child))
    insertion_pos = Position::InParentBeforeNode(*list_child);

  HTMLElement* list_element = CreateHTMLElement(GetDocument(), list_tag);
  InsertNodeAt(list_element, insertion_pos, editing_state);
  if (editing_state->IsAborted())
    return;
  HTMLLIElement* list_item_element = HTMLLIElement::Create(GetDocument());
  AppendNode(list_item_element, list_element, editing_state);
  if (editing_state->IsAborted())
    return;

  // We inserted the list at the start of the content we're about to move.
  // https://bugs.webkit.org/show_bug.cgi?id=19066: Update the start of content,
  // so we don't try to move the list into itself.
  // Layout is necessary since start's node's inline layoutObjects may have been
  // destroyed by the insertion The end of the content may have changed after
  // the insertion and layout so update it as well.
  if (insertion_pos == start_pos) {
    MoveParagraphOverPositionIntoEmptyListItem(
        original_start, list_item_element, editing_state);
  } else {
    GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
    MoveParagraphOverPositionIntoEmptyListItem(
        CreateVisiblePosition(start_pos), list_item_element, editing_state);
  }
  if (editing_state->IsAborted())
    return;

  MergeWithNeighboringLists(list_element, editing_state);
}

// TODO(xiaochengh): Stop storing VisiblePositions through mutations.
void InsertListCommand::MoveParagraphOverPositionIntoEmptyListItem(
    const VisiblePosition& pos,
    HTMLLIElement* list_item_element,
    EditingState* editing_state) {
  DCHECK(!list_item_element->HasChildren());
  HTMLBRElement* placeholder = HTMLBRElement::Create(GetDocument());
  AppendNode(placeholder, list_item_element, editing_state);
  if (editing_state->IsAborted())
    return;
  // Inserting list element and list item list may change start of pargraph
  // to move. We calculate start of paragraph again.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  const VisiblePosition& valid_pos =
      CreateVisiblePosition(pos.ToPositionWithAffinity());
  const VisiblePosition& start =
      StartOfParagraph(valid_pos, kCanSkipOverEditingBoundary);
  const VisiblePosition& end =
      EndOfParagraph(valid_pos, kCanSkipOverEditingBoundary);
  MoveParagraph(start, end, VisiblePosition::BeforeNode(*placeholder),
                editing_state, kPreserveSelection);
}

DEFINE_TRACE(InsertListCommand) {
  CompositeEditCommand::Trace(visitor);
}

}  // namespace blink
