/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2012 Digia Plc. and/or its subsidiary(-ies)
 * Copyright (C) 2015 Google Inc. All rights reserved.
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

#include "core/editing/SelectionController.h"

#include "core/HTMLNames.h"
#include "core/dom/Document.h"
#include "core/editing/EditingBoundary.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/RenderedPosition.h"
#include "core/editing/SetSelectionData.h"
#include "core/editing/VisibleSelection.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/suggestion/TextSuggestionController.h"
#include "core/events/Event.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameView.h"
#include "core/frame/Settings.h"
#include "core/input/EventHandler.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/wtf/AutoReset.h"
#include "public/platform/WebMenuSourceType.h"

namespace blink {
SelectionController* SelectionController::Create(LocalFrame& frame) {
  return new SelectionController(frame);
}

SelectionController::SelectionController(LocalFrame& frame)
    : frame_(&frame),
      mouse_down_may_start_select_(false),
      mouse_down_was_single_click_in_selection_(false),
      mouse_down_allows_multi_click_(false),
      selection_state_(SelectionState::kHaveNotStartedSelection) {}

DEFINE_TRACE(SelectionController) {
  visitor->Trace(frame_);
  visitor->Trace(original_base_in_flat_tree_);
  SynchronousMutationObserver::Trace(visitor);
}

namespace {

SelectionInDOMTree ConvertToSelectionInDOMTree(
    const SelectionInFlatTree& selection_in_flat_tree) {
  return SelectionInDOMTree::Builder()
      .SetAffinity(selection_in_flat_tree.Affinity())
      .SetBaseAndExtent(ToPositionInDOMTree(selection_in_flat_tree.Base()),
                        ToPositionInDOMTree(selection_in_flat_tree.Extent()))
      .SetIsDirectional(selection_in_flat_tree.IsDirectional())
      .SetIsHandleVisible(selection_in_flat_tree.IsHandleVisible())
      .Build();
}

DispatchEventResult DispatchSelectStart(Node* node) {
  if (!node || !node->GetLayoutObject())
    return DispatchEventResult::kNotCanceled;

  return node->DispatchEvent(
      Event::CreateCancelableBubble(EventTypeNames::selectstart));
}

SelectionInFlatTree ExpandSelectionToRespectUserSelectAll(
    Node* target_node,
    const VisibleSelectionInFlatTree& selection) {
  if (selection.IsNone())
    return SelectionInFlatTree();
  Node* const root_user_select_all =
      EditingInFlatTreeStrategy::RootUserSelectAllForNode(target_node);
  if (!root_user_select_all) {
    SelectionInFlatTree::Builder builder;
    if (selection.IsBaseFirst())
      builder.SetBaseAndExtent(selection.Start(), selection.End());
    else
      builder.SetBaseAndExtent(selection.End(), selection.Start());
    builder.SetAffinity(selection.Affinity());
    return builder.Build();
  }

  return SelectionInFlatTree::Builder(selection.AsSelection())
      .Collapse(MostBackwardCaretPosition(
          PositionInFlatTree::BeforeNode(*root_user_select_all),
          kCanCrossEditingBoundary))
      .Extend(MostForwardCaretPosition(
          PositionInFlatTree::AfterNode(*root_user_select_all),
          kCanCrossEditingBoundary))
      .Build();
}

static int TextDistance(const PositionInFlatTree& start,
                        const PositionInFlatTree& end) {
  return TextIteratorInFlatTree::RangeLength(
      start, end,
      TextIteratorBehavior::AllVisiblePositionsRangeLengthBehavior());
}

bool CanMouseDownStartSelect(Node* node) {
  if (!node || !node->GetLayoutObject())
    return true;

  if (!node->CanStartSelection())
    return false;

  return true;
}

VisiblePositionInFlatTree VisiblePositionOfHitTestResult(
    const HitTestResult& hit_test_result) {
  return CreateVisiblePosition(FromPositionInDOMTree<EditingInFlatTreeStrategy>(
      hit_test_result.InnerNode()->GetLayoutObject()->PositionForPoint(
          hit_test_result.LocalPoint())));
}

}  // namespace

SelectionController::~SelectionController() = default;

Document& SelectionController::GetDocument() const {
  DCHECK(frame_->GetDocument());
  return *frame_->GetDocument();
}

void SelectionController::ContextDestroyed(Document*) {
  original_base_in_flat_tree_ = PositionInFlatTreeWithAffinity();
}

static PositionInFlatTree AdjustPositionRespectUserSelectAll(
    Node* inner_node,
    const PositionInFlatTree& selection_start,
    const PositionInFlatTree& selection_end,
    const PositionInFlatTree& position) {
  const VisibleSelectionInFlatTree& selection_in_user_select_all =
      CreateVisibleSelection(ExpandSelectionToRespectUserSelectAll(
          inner_node, position.IsNull() ? VisibleSelectionInFlatTree()
                                        : CreateVisibleSelection(
                                              SelectionInFlatTree::Builder()
                                                  .Collapse(position)
                                                  .Build())));
  if (!selection_in_user_select_all.IsRange())
    return position;
  if (selection_in_user_select_all.Start().CompareTo(selection_start) < 0)
    return selection_in_user_select_all.Start();
  if (selection_end.CompareTo(selection_in_user_select_all.End()) < 0)
    return selection_in_user_select_all.End();
  return position;
}

static PositionInFlatTree ComputeStartFromEndForExtendForward(
    const PositionInFlatTree& end,
    TextGranularity granularity) {
  if (granularity == TextGranularity::kCharacter)
    return end;
  // |ComputeStartRespectingGranularity()| returns next word/paragraph for
  // end of word/paragraph position. To get start of word/paragraph at |end|,
  // we pass previous position of |end|.
  return ComputeStartRespectingGranularity(
      PreviousPositionOf(CreateVisiblePosition(end),
                         kCannotCrossEditingBoundary)
          .DeepEquivalent(),
      granularity);
}

static SelectionInFlatTree ExtendSelectionAsDirectional(
    const PositionInFlatTree& position,
    const VisibleSelectionInFlatTree& selection,
    TextGranularity granularity) {
  DCHECK(!selection.IsNone());
  DCHECK(position.IsNotNull());
  const PositionInFlatTree& start = selection.Start();
  const PositionInFlatTree& end = selection.End();
  const PositionInFlatTree& base = selection.IsBaseFirst() ? start : end;
  if (position < base) {
    // Extend backward yields backward selection
    //  - forward selection:  *abc ^def ghi| => |abc def^ ghi
    //  - backward selection: *abc |def ghi^ => |abc def ghi^
    const PositionInFlatTree& new_start = ComputeStartRespectingGranularity(
        PositionInFlatTreeWithAffinity(position), granularity);
    const PositionInFlatTree& new_end =
        selection.IsBaseFirst()
            ? ComputeEndRespectingGranularity(
                  new_start, PositionInFlatTreeWithAffinity(start), granularity)
            : end;
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(new_end, new_start)
        .Build();
  }

  // Extend forward yields forward selection
  //  - forward selection:  ^abc def| ghi* => ^abc def ghi|
  //  - backward selection: |abc def^ ghi* => abc ^def ghi|
  const PositionInFlatTree& new_start =
      selection.IsBaseFirst()
          ? start
          : ComputeStartFromEndForExtendForward(end, granularity);
  const PositionInFlatTree& new_end = ComputeEndRespectingGranularity(
      new_start, PositionInFlatTreeWithAffinity(position), granularity);
  return SelectionInFlatTree::Builder()
      .SetBaseAndExtent(new_start, new_end)
      .Build();
}

static SelectionInFlatTree ExtendSelectionAsNonDirectional(
    const PositionInFlatTree& position,
    const VisibleSelectionInFlatTree& selection,
    TextGranularity granularity) {
  DCHECK(!selection.IsNone());
  DCHECK(position.IsNotNull());
  // Shift+Click deselects when selection was created right-to-left
  const PositionInFlatTree& start = selection.Start();
  const PositionInFlatTree& end = selection.End();
  if (position < start) {
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(
            end, ComputeStartRespectingGranularity(
                     PositionInFlatTreeWithAffinity(position), granularity))
        .Build();
  }
  if (end < position) {
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(
            start,
            ComputeEndRespectingGranularity(
                start, PositionInFlatTreeWithAffinity(position), granularity))
        .Build();
  }
  const int distance_to_start = TextDistance(start, position);
  const int distance_to_end = TextDistance(position, end);
  if (distance_to_start <= distance_to_end) {
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(
            end, ComputeStartRespectingGranularity(
                     PositionInFlatTreeWithAffinity(position), granularity))
        .Build();
  }
  return SelectionInFlatTree::Builder()
      .SetBaseAndExtent(
          start,
          ComputeEndRespectingGranularity(
              start, PositionInFlatTreeWithAffinity(position), granularity))
      .Build();
}

// Updating the selection is considered side-effect of the event and so it
// doesn't impact the handled state.
bool SelectionController::HandleSingleClick(
    const MouseEventWithHitTestResults& event) {
  TRACE_EVENT0("blink",
               "SelectionController::handleMousePressEventSingleClick");

  DCHECK(!frame_->GetDocument()->NeedsLayoutTreeUpdate());
  Node* inner_node = event.InnerNode();
  if (!(inner_node && inner_node->GetLayoutObject() &&
        mouse_down_may_start_select_))
    return false;

  // Extend the selection if the Shift key is down, unless the click is in a
  // link or image.
  bool extend_selection = IsExtendingSelection(event);

  const VisiblePositionInFlatTree& visible_hit_pos =
      VisiblePositionOfHitTestResult(event.GetHitTestResult());
  const VisiblePositionInFlatTree& visible_pos =
      visible_hit_pos.IsNull()
          ? CreateVisiblePosition(
                PositionInFlatTree::FirstPositionInOrBeforeNode(inner_node))
          : visible_hit_pos;
  const VisibleSelectionInFlatTree& selection =
      this->Selection().ComputeVisibleSelectionInFlatTree();

  // Don't restart the selection when the mouse is pressed on an
  // existing selection so we can allow for text dragging.
  if (LocalFrameView* view = frame_->View()) {
    const LayoutPoint v_point = view->RootFrameToContents(
        FlooredIntPoint(event.Event().PositionInRootFrame()));
    if (!extend_selection && this->Selection().Contains(v_point)) {
      mouse_down_was_single_click_in_selection_ = true;
      if (!event.Event().FromTouch())
        return false;

      if (!this->Selection().IsHandleVisible()) {
        const bool did_select =
            UpdateSelectionForMouseDownDispatchingSelectStart(
                inner_node, selection.AsSelection(),
                TextGranularity::kCharacter, HandleVisibility::kVisible);
        if (did_select) {
          frame_->GetEventHandler().ShowNonLocatedContextMenu(nullptr,
                                                              kMenuSourceTouch);
        }
        return false;
      }
    }
  }

  if (extend_selection && !selection.IsNone()) {
    // Note: "fast/events/shift-click-user-select-none.html" makes
    // |pos.isNull()| true.
    const PositionInFlatTree& pos = AdjustPositionRespectUserSelectAll(
        inner_node, selection.Start(), selection.End(),
        visible_pos.DeepEquivalent());
    const TextGranularity granularity = Selection().Granularity();
    if (pos.IsNull()) {
      UpdateSelectionForMouseDownDispatchingSelectStart(
          inner_node, selection.AsSelection(), granularity,
          HandleVisibility::kNotVisible);
      return false;
    }
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node,
        frame_->GetEditor().Behavior().ShouldConsiderSelectionAsDirectional()
            ? ExtendSelectionAsDirectional(pos, selection, granularity)
            : ExtendSelectionAsNonDirectional(pos, selection, granularity),
        granularity, HandleVisibility::kNotVisible);
    return false;
  }

  if (selection_state_ == SelectionState::kExtendedSelection) {
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node, selection.AsSelection(), TextGranularity::kCharacter,
        HandleVisibility::kNotVisible);
    return false;
  }

  if (visible_pos.IsNull()) {
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node, SelectionInFlatTree(), TextGranularity::kCharacter,
        HandleVisibility::kNotVisible);
    return false;
  }

  bool is_handle_visible = false;
  const bool has_editable_style = HasEditableStyle(*inner_node);
  if (has_editable_style) {
    const bool is_text_box_empty =
        !RootEditableElement(*inner_node)->HasChildren();
    const bool not_left_click =
        event.Event().button != WebPointerProperties::Button::kLeft;
    if (!is_text_box_empty || not_left_click)
      is_handle_visible = event.Event().FromTouch();
  }

  UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(
          inner_node, CreateVisibleSelection(
                          SelectionInFlatTree::Builder()
                              .Collapse(visible_pos.ToPositionWithAffinity())
                              .Build())),
      TextGranularity::kCharacter,
      is_handle_visible ? HandleVisibility::kVisible
                        : HandleVisibility::kNotVisible);

  if (has_editable_style && event.Event().FromTouch()) {
    frame_->GetTextSuggestionController().HandlePotentialMisspelledWordTap(
        visible_pos.DeepEquivalent());
  }

  return false;
}

// Returns true if selection starts from |SVGText| node and |target_node| is
// not the containing block of |SVGText| node.
// See https://bugs.webkit.org/show_bug.cgi?id=12334 for details.
static bool ShouldRespectSVGTextBoundaries(
    const Node& target_node,
    const FrameSelection& frame_selection) {
  const PositionInFlatTree& base =
      frame_selection.ComputeVisibleSelectionInFlatTree().Base();
  // TODO(editing-dev): We should use |ComputeContainerNode()|.
  const Node* const base_node = base.AnchorNode();
  if (!base_node)
    return false;
  LayoutObject* const base_layout_object = base_node->GetLayoutObject();
  if (!base_layout_object || !base_layout_object->IsSVGText())
    return false;
  return target_node.GetLayoutObject()->ContainingBlock() !=
         base_layout_object->ContainingBlock();
}

void SelectionController::UpdateSelectionForMouseDrag(
    const HitTestResult& hit_test_result,
    Node* mouse_press_node,
    const LayoutPoint& drag_start_pos,
    const IntPoint& last_known_mouse_position) {
  if (!mouse_down_may_start_select_)
    return;

  Node* target = hit_test_result.InnerNode();
  if (!target)
    return;

  // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame_->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  const PositionWithAffinity& raw_target_position =
      Selection().SelectionHasFocus()
          ? PositionRespectingEditingBoundary(
                Selection().ComputeVisibleSelectionInDOMTree().Start(),
                hit_test_result.LocalPoint(), target)
          : PositionWithAffinity();
  VisiblePositionInFlatTree target_position = CreateVisiblePosition(
      FromPositionInDOMTree<EditingInFlatTreeStrategy>(raw_target_position));
  // Don't modify the selection if we're not on a node.
  if (target_position.IsNull())
    return;

  // Restart the selection if this is the first mouse move. This work is usually
  // done in handleMousePressEvent, but not if the mouse press was on an
  // existing selection.

  // Special case to limit selection to the containing block for SVG text.
  // TODO(editing_dev): Isn't there a better non-SVG-specific way to do this?
  if (ShouldRespectSVGTextBoundaries(*target, Selection()))
    return;

  if (selection_state_ == SelectionState::kHaveNotStartedSelection &&
      DispatchSelectStart(target) != DispatchEventResult::kNotCanceled)
    return;

  // TODO(yosin) We should check |mousePressNode|, |targetPosition|, and
  // |newSelection| are valid for |m_frame->document()|.
  // |dispatchSelectStart()| can change them by "selectstart" event handler.

  const bool should_extend_selection =
      selection_state_ == SelectionState::kExtendedSelection;
  // Always extend selection here because it's caused by a mouse drag
  selection_state_ = SelectionState::kExtendedSelection;

  const VisibleSelectionInFlatTree& visible_selection =
      Selection().ComputeVisibleSelectionInFlatTree();
  if (visible_selection.IsNone()) {
    // TODO(editing-dev): This is an urgent fix to crbug.com/745501. We should
    // find the root cause and replace this by a proper fix.
    return;
  }

  const PositionInFlatTree& adjusted_position =
      AdjustPositionRespectUserSelectAll(target, visible_selection.Start(),
                                         visible_selection.End(),
                                         target_position.DeepEquivalent());
  const SelectionInFlatTree& adjusted_selection =
      should_extend_selection
          ? ExtendSelectionAsDirectional(adjusted_position, visible_selection,
                                         Selection().Granularity())
          : SelectionInFlatTree::Builder().Collapse(adjusted_position).Build();

  SetNonDirectionalSelectionIfNeeded(
      adjusted_selection, Selection().Granularity(),
      kAdjustEndpointsAtBidiBoundary, HandleVisibility::kNotVisible);
}

bool SelectionController::UpdateSelectionForMouseDownDispatchingSelectStart(
    Node* target_node,
    const SelectionInFlatTree& selection,
    TextGranularity granularity,
    HandleVisibility handle_visibility) {
  if (target_node && target_node->GetLayoutObject() &&
      !target_node->GetLayoutObject()->IsSelectable())
    return false;

  // TODO(editing-dev): We should compute visible selection after dispatching
  // "selectstart", once we have |SelectionInFlatTree::IsValidFor()|.
  const VisibleSelectionInFlatTree& visible_selection =
      CreateVisibleSelection(selection);

  if (DispatchSelectStart(target_node) != DispatchEventResult::kNotCanceled)
    return false;

  // |dispatchSelectStart()| can change document hosted by |m_frame|.
  if (!this->Selection().IsAvailable())
    return false;

  if (!visible_selection.IsValidFor(this->Selection().GetDocument()))
    return false;

  if (visible_selection.IsRange()) {
    selection_state_ = SelectionState::kExtendedSelection;
    SetNonDirectionalSelectionIfNeeded(
        selection, granularity, kDoNotAdjustEndpoints, handle_visibility);

    return true;
  }

  selection_state_ = SelectionState::kPlacedCaret;
  SetNonDirectionalSelectionIfNeeded(selection, TextGranularity::kCharacter,
                                     kDoNotAdjustEndpoints, handle_visibility);
  return true;
}

bool SelectionController::SelectClosestWordFromHitTestResult(
    const HitTestResult& result,
    AppendTrailingWhitespace append_trailing_whitespace,
    SelectInputEventType select_input_event_type) {
  Node* const inner_node = result.InnerNode();

  if (!inner_node || !inner_node->GetLayoutObject() ||
      !inner_node->GetLayoutObject()->IsSelectable())
    return false;

  // Special-case image local offset to always be zero, to avoid triggering
  // LayoutReplaced::positionFromPoint's advancement of the position at the
  // mid-point of the the image (which was intended for mouse-drag selection
  // and isn't desirable for touch).
  HitTestResult adjusted_hit_test_result = result;
  if (select_input_event_type == SelectInputEventType::kTouch &&
      result.GetImage())
    adjusted_hit_test_result.SetNodeAndPosition(result.InnerNode(),
                                                LayoutPoint(0, 0));

  const VisiblePositionInFlatTree& pos =
      VisiblePositionOfHitTestResult(adjusted_hit_test_result);
  const VisibleSelectionInFlatTree& new_selection =
      pos.IsNotNull() ? CreateVisibleSelectionWithGranularity(
                            SelectionInFlatTree::Builder()
                                .Collapse(pos.ToPositionWithAffinity())
                                .Build(),
                            TextGranularity::kWord)
                      : VisibleSelectionInFlatTree();

  HandleVisibility visibility = HandleVisibility::kNotVisible;
  if (select_input_event_type == SelectInputEventType::kTouch) {
    // If node doesn't have text except space, tab or line break, do not
    // select that 'empty' area.
    EphemeralRangeInFlatTree range(new_selection.Start(), new_selection.End());
    const String& str = PlainText(
        range,
        TextIteratorBehavior::Builder()
            .SetEmitsObjectReplacementCharacter(HasEditableStyle(*inner_node))
            .Build());
    if (str.IsEmpty() || str.SimplifyWhiteSpace().ContainsOnlyWhitespace())
      return false;

    if (new_selection.RootEditableElement() &&
        pos.DeepEquivalent() == VisiblePositionInFlatTree::LastPositionInNode(
                                    *new_selection.RootEditableElement())
                                    .DeepEquivalent())
      return false;

    visibility = HandleVisibility::kVisible;
  }

  const VisibleSelectionInFlatTree& adjusted_selection =
      append_trailing_whitespace == AppendTrailingWhitespace::kShouldAppend
          ? new_selection.AppendTrailingWhitespace()
          : new_selection;

  return UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(inner_node, adjusted_selection),
      TextGranularity::kWord, visibility);
}

void SelectionController::SelectClosestMisspellingFromHitTestResult(
    const HitTestResult& result,
    AppendTrailingWhitespace append_trailing_whitespace) {
  Node* inner_node = result.InnerNode();

  if (!inner_node || !inner_node->GetLayoutObject())
    return;

  const VisiblePositionInFlatTree& pos = VisiblePositionOfHitTestResult(result);
  if (pos.IsNull()) {
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node, SelectionInFlatTree(), TextGranularity::kWord,
        HandleVisibility::kNotVisible);
    return;
  }

  const PositionInFlatTree& marker_position =
      pos.DeepEquivalent().ParentAnchoredEquivalent();
  const DocumentMarker* const marker =
      inner_node->GetDocument().Markers().MarkerAtPosition(
          ToPositionInDOMTree(marker_position),
          DocumentMarker::MisspellingMarkers());
  if (!marker) {
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node, SelectionInFlatTree(), TextGranularity::kWord,
        HandleVisibility::kNotVisible);
    return;
  }

  Node* const container_node = marker_position.ComputeContainerNode();
  const PositionInFlatTree start(container_node, marker->StartOffset());
  const PositionInFlatTree end(container_node, marker->EndOffset());
  const VisibleSelectionInFlatTree& new_selection = CreateVisibleSelection(
      SelectionInFlatTree::Builder().Collapse(start).Extend(end).Build());
  const VisibleSelectionInFlatTree& adjusted_selection =
      append_trailing_whitespace == AppendTrailingWhitespace::kShouldAppend
          ? new_selection.AppendTrailingWhitespace()
          : new_selection;
  UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(inner_node, adjusted_selection),
      TextGranularity::kWord, HandleVisibility::kNotVisible);
}

bool SelectionController::SelectClosestWordFromMouseEvent(
    const MouseEventWithHitTestResults& result) {
  if (!mouse_down_may_start_select_)
    return false;

  AppendTrailingWhitespace append_trailing_whitespace =
      (result.Event().click_count == 2 &&
       frame_->GetEditor().IsSelectTrailingWhitespaceEnabled())
          ? AppendTrailingWhitespace::kShouldAppend
          : AppendTrailingWhitespace::kDontAppend;

  DCHECK(!frame_->GetDocument()->NeedsLayoutTreeUpdate());

  return SelectClosestWordFromHitTestResult(
      result.GetHitTestResult(), append_trailing_whitespace,
      result.Event().FromTouch() ? SelectInputEventType::kTouch
                                 : SelectInputEventType::kMouse);
}

void SelectionController::SelectClosestMisspellingFromMouseEvent(
    const MouseEventWithHitTestResults& result) {
  if (!mouse_down_may_start_select_)
    return;

  SelectClosestMisspellingFromHitTestResult(
      result.GetHitTestResult(),
      (result.Event().click_count == 2 &&
       frame_->GetEditor().IsSelectTrailingWhitespaceEnabled())
          ? AppendTrailingWhitespace::kShouldAppend
          : AppendTrailingWhitespace::kDontAppend);
}

void SelectionController::SelectClosestWordOrLinkFromMouseEvent(
    const MouseEventWithHitTestResults& result) {
  if (!result.GetHitTestResult().IsLiveLink()) {
    SelectClosestWordFromMouseEvent(result);
    return;
  }

  Node* const inner_node = result.InnerNode();

  if (!inner_node || !inner_node->GetLayoutObject() ||
      !mouse_down_may_start_select_)
    return;

  Element* url_element = result.GetHitTestResult().URLElement();
  const VisiblePositionInFlatTree pos =
      VisiblePositionOfHitTestResult(result.GetHitTestResult());
  const VisibleSelectionInFlatTree& new_selection =
      pos.IsNotNull() &&
              pos.DeepEquivalent().AnchorNode()->IsDescendantOf(url_element)
          ? CreateVisibleSelection(SelectionInFlatTree::Builder()
                                       .SelectAllChildren(*url_element)
                                       .Build())
          : VisibleSelectionInFlatTree();

  UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(inner_node, new_selection),
      TextGranularity::kWord, HandleVisibility::kNotVisible);
}

static SelectionInFlatTree AdjustEndpointsAtBidiBoundary(
    const VisiblePositionInFlatTree& visible_base,
    const VisiblePositionInFlatTree& visible_extent) {
  DCHECK(visible_base.IsValid());
  DCHECK(visible_extent.IsValid());

  RenderedPosition base(visible_base);
  RenderedPosition extent(visible_extent);

  const SelectionInFlatTree& unchanged_selection =
      SelectionInFlatTree::Builder()
          .SetBaseAndExtent(visible_base.DeepEquivalent(),
                            visible_extent.DeepEquivalent())
          .Build();

  if (base.IsNull() || extent.IsNull() || base.IsEquivalent(extent))
    return unchanged_selection;

  if (base.AtLeftBoundaryOfBidiRun()) {
    if (!extent.AtRightBoundaryOfBidiRun(base.BidiLevelOnRight()) &&
        base.IsEquivalent(
            extent.LeftBoundaryOfBidiRun(base.BidiLevelOnRight()))) {
      return SelectionInFlatTree::Builder()
          .SetBaseAndExtent(
              CreateVisiblePosition(
                  ToPositionInFlatTree(base.PositionAtLeftBoundaryOfBiDiRun()))
                  .DeepEquivalent(),
              visible_extent.DeepEquivalent())
          .Build();
    }
    return unchanged_selection;
  }

  if (base.AtRightBoundaryOfBidiRun()) {
    if (!extent.AtLeftBoundaryOfBidiRun(base.BidiLevelOnLeft()) &&
        base.IsEquivalent(
            extent.RightBoundaryOfBidiRun(base.BidiLevelOnLeft()))) {
      return SelectionInFlatTree::Builder()
          .SetBaseAndExtent(
              CreateVisiblePosition(
                  ToPositionInFlatTree(base.PositionAtRightBoundaryOfBiDiRun()))
                  .DeepEquivalent(),
              visible_extent.DeepEquivalent())
          .Build();
    }
    return unchanged_selection;
  }

  if (extent.AtLeftBoundaryOfBidiRun() &&
      extent.IsEquivalent(
          base.LeftBoundaryOfBidiRun(extent.BidiLevelOnRight()))) {
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(
            visible_base.DeepEquivalent(),
            CreateVisiblePosition(
                ToPositionInFlatTree(extent.PositionAtLeftBoundaryOfBiDiRun()))
                .DeepEquivalent())
        .Build();
  }

  if (extent.AtRightBoundaryOfBidiRun() &&
      extent.IsEquivalent(
          base.RightBoundaryOfBidiRun(extent.BidiLevelOnLeft()))) {
    return SelectionInFlatTree::Builder()
        .SetBaseAndExtent(
            visible_base.DeepEquivalent(),
            CreateVisiblePosition(
                ToPositionInFlatTree(extent.PositionAtRightBoundaryOfBiDiRun()))
                .DeepEquivalent())
        .Build();
  }
  return unchanged_selection;
}

// TODO(yosin): We should take |granularity| and |handleVisibility| from
// |newSelection|.
void SelectionController::SetNonDirectionalSelectionIfNeeded(
    const SelectionInFlatTree& passed_selection,
    TextGranularity granularity,
    EndPointsAdjustmentMode endpoints_adjustment_mode,
    HandleVisibility handle_visibility) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  const VisibleSelectionInFlatTree& new_selection =
      CreateVisibleSelection(passed_selection);
  // TODO(editing-dev): We should use |PositionWithAffinity| to pass affinity
  // to |CreateVisiblePosition()| for |original_base|.
  const PositionInFlatTree& base_position =
      original_base_in_flat_tree_.GetPosition();
  const VisiblePositionInFlatTree& original_base =
      base_position.IsConnected() ? CreateVisiblePosition(base_position)
                                  : VisiblePositionInFlatTree();
  const VisiblePositionInFlatTree& base =
      original_base.IsNotNull() ? original_base
                                : CreateVisiblePosition(new_selection.Base());
  const VisiblePositionInFlatTree& extent =
      CreateVisiblePosition(new_selection.Extent());
  const SelectionInFlatTree& adjusted_selection =
      endpoints_adjustment_mode == kAdjustEndpointsAtBidiBoundary
          ? AdjustEndpointsAtBidiBoundary(base, extent)
          : SelectionInFlatTree::Builder()
                .SetBaseAndExtent(base.DeepEquivalent(),
                                  extent.DeepEquivalent())
                .Build();

  SelectionInFlatTree::Builder builder(new_selection.AsSelection());
  if (adjusted_selection.Base() != base.DeepEquivalent() ||
      adjusted_selection.Extent() != extent.DeepEquivalent()) {
    original_base_in_flat_tree_ = base.ToPositionWithAffinity();
    SetContext(&GetDocument());
    builder.SetBaseAndExtent(adjusted_selection.Base(),
                             adjusted_selection.Extent());
  } else if (original_base.IsNotNull()) {
    if (CreateVisiblePosition(
            Selection().ComputeVisibleSelectionInFlatTree().Base())
            .DeepEquivalent() ==
        CreateVisiblePosition(new_selection.Base()).DeepEquivalent()) {
      builder.SetBaseAndExtent(original_base.DeepEquivalent(),
                               new_selection.Extent());
    }
    original_base_in_flat_tree_ = PositionInFlatTreeWithAffinity();
  }

  builder.SetIsHandleVisible(handle_visibility == HandleVisibility::kVisible)
      .SetIsDirectional(frame_->GetEditor()
                            .Behavior()
                            .ShouldConsiderSelectionAsDirectional() ||
                        new_selection.IsDirectional());
  const SelectionInFlatTree& selection_in_flat_tree = builder.Build();
  if (Selection().ComputeVisibleSelectionInFlatTree() ==
          CreateVisibleSelection(selection_in_flat_tree) &&
      Selection().IsHandleVisible() == selection_in_flat_tree.IsHandleVisible())
    return;
  Selection().SetSelection(
      ConvertToSelectionInDOMTree(selection_in_flat_tree),
      SetSelectionData::Builder()
          .SetShouldCloseTyping(true)
          .SetShouldClearTypingStyle(true)
          .SetCursorAlignOnScroll(CursorAlignOnScroll::kIfNeeded)
          .SetGranularity(granularity)
          .Build());
}

void SelectionController::SetCaretAtHitTestResult(
    const HitTestResult& hit_test_result) {
  Node* inner_node = hit_test_result.InnerNode();
  const VisiblePositionInFlatTree& visible_hit_pos =
      VisiblePositionOfHitTestResult(hit_test_result);
  const VisiblePositionInFlatTree& visible_pos =
      visible_hit_pos.IsNull()
          ? CreateVisiblePosition(
                PositionInFlatTree::FirstPositionInOrBeforeNode(inner_node))
          : visible_hit_pos;

  if (visible_pos.IsNull()) {
    UpdateSelectionForMouseDownDispatchingSelectStart(
        inner_node, SelectionInFlatTree(), TextGranularity::kCharacter,
        HandleVisibility::kVisible);
    return;
  }
  UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(
          inner_node, CreateVisibleSelection(
                          SelectionInFlatTree::Builder()
                              .Collapse(visible_pos.ToPositionWithAffinity())
                              .Build())),
      TextGranularity::kCharacter, HandleVisibility::kVisible);
}

bool SelectionController::HandleDoubleClick(
    const MouseEventWithHitTestResults& event) {
  TRACE_EVENT0("blink",
               "SelectionController::handleMousePressEventDoubleClick");

  if (!Selection().IsAvailable())
    return false;

  if (!mouse_down_allows_multi_click_)
    return HandleSingleClick(event);

  if (event.Event().button != WebPointerProperties::Button::kLeft)
    return false;

  if (Selection().ComputeVisibleSelectionInDOMTreeDeprecated().IsRange()) {
    // A double-click when range is already selected
    // should not change the selection.  So, do not call
    // selectClosestWordFromMouseEvent, but do set
    // m_beganSelectingText to prevent handleMouseReleaseEvent
    // from setting caret selection.
    selection_state_ = SelectionState::kExtendedSelection;
    return true;
  }
  if (!SelectClosestWordFromMouseEvent(event))
    return true;
  if (!Selection().IsHandleVisible())
    return true;
  frame_->GetEventHandler().ShowNonLocatedContextMenu(nullptr,
                                                      kMenuSourceTouch);
  return true;
}

bool SelectionController::HandleTripleClick(
    const MouseEventWithHitTestResults& event) {
  TRACE_EVENT0("blink",
               "SelectionController::handleMousePressEventTripleClick");

  if (!Selection().IsAvailable()) {
    // editing/shadow/doubleclick-on-meter-in-shadow-crash.html reach here.
    return false;
  }

  if (!mouse_down_allows_multi_click_)
    return HandleSingleClick(event);

  if (event.Event().button != WebPointerProperties::Button::kLeft)
    return false;

  Node* const inner_node = event.InnerNode();
  if (!(inner_node && inner_node->GetLayoutObject() &&
        mouse_down_may_start_select_))
    return false;

  const VisiblePositionInFlatTree& pos =
      VisiblePositionOfHitTestResult(event.GetHitTestResult());
  const VisibleSelectionInFlatTree new_selection =
      pos.IsNotNull() ? CreateVisibleSelectionWithGranularity(
                            SelectionInFlatTree::Builder()
                                .Collapse(pos.ToPositionWithAffinity())
                                .Build(),
                            TextGranularity::kParagraph)
                      : VisibleSelectionInFlatTree();

  const bool is_handle_visible =
      event.Event().FromTouch() && new_selection.IsRange();

  const bool did_select = UpdateSelectionForMouseDownDispatchingSelectStart(
      inner_node,
      ExpandSelectionToRespectUserSelectAll(inner_node, new_selection),
      TextGranularity::kParagraph,
      is_handle_visible ? HandleVisibility::kVisible
                        : HandleVisibility::kNotVisible);
  if (!did_select)
    return false;

  if (!Selection().IsHandleVisible())
    return true;
  frame_->GetEventHandler().ShowNonLocatedContextMenu(nullptr,
                                                      kMenuSourceTouch);
  return true;
}

bool SelectionController::HandleMousePressEvent(
    const MouseEventWithHitTestResults& event) {
  TRACE_EVENT0("blink", "SelectionController::handleMousePressEvent");

  // If we got the event back, that must mean it wasn't prevented,
  // so it's allowed to start a drag or selection if it wasn't in a scrollbar.
  mouse_down_may_start_select_ =
      (CanMouseDownStartSelect(event.InnerNode()) || IsLinkSelection(event)) &&
      !event.GetScrollbar();
  mouse_down_was_single_click_in_selection_ = false;
  if (!Selection().IsAvailable()) {
    // "gesture-tap-frame-removed.html" reaches here.
    mouse_down_allows_multi_click_ = !event.Event().FromTouch();
  } else {
    // Avoid double-tap touch gesture confusion by restricting multi-click side
    // effects, e.g., word selection, to editable regions.
    mouse_down_allows_multi_click_ =
        !event.Event().FromTouch() ||
        IsEditablePosition(
            Selection().ComputeVisibleSelectionInDOMTreeDeprecated().Start());
  }

  if (event.Event().click_count >= 3)
    return HandleTripleClick(event);
  if (event.Event().click_count == 2)
    return HandleDoubleClick(event);
  return HandleSingleClick(event);
}

void SelectionController::HandleMouseDraggedEvent(
    const MouseEventWithHitTestResults& event,
    const IntPoint& mouse_down_pos,
    const LayoutPoint& drag_start_pos,
    Node* mouse_press_node,
    const IntPoint& last_known_mouse_position) {
  TRACE_EVENT0("blink", "SelectionController::handleMouseDraggedEvent");

  if (!Selection().IsAvailable())
    return;
  if (selection_state_ != SelectionState::kExtendedSelection) {
    HitTestRequest request(HitTestRequest::kReadOnly | HitTestRequest::kActive);
    HitTestResult result(request, mouse_down_pos);
    frame_->GetDocument()->GetLayoutViewItem().HitTest(result);

    UpdateSelectionForMouseDrag(result, mouse_press_node, drag_start_pos,
                                last_known_mouse_position);
  }
  UpdateSelectionForMouseDrag(event.GetHitTestResult(), mouse_press_node,
                              drag_start_pos, last_known_mouse_position);
}

void SelectionController::UpdateSelectionForMouseDrag(
    Node* mouse_press_node,
    const LayoutPoint& drag_start_pos,
    const IntPoint& last_known_mouse_position) {
  LocalFrameView* view = frame_->View();
  if (!view)
    return;
  LayoutViewItem layout_item = frame_->ContentLayoutItem();
  if (layout_item.IsNull())
    return;

  HitTestRequest request(HitTestRequest::kReadOnly | HitTestRequest::kActive |
                         HitTestRequest::kMove);
  HitTestResult result(request,
                       view->RootFrameToContents(last_known_mouse_position));
  layout_item.HitTest(result);
  UpdateSelectionForMouseDrag(result, mouse_press_node, drag_start_pos,
                              last_known_mouse_position);
}

bool SelectionController::HandleMouseReleaseEvent(
    const MouseEventWithHitTestResults& event,
    const LayoutPoint& drag_start_pos) {
  TRACE_EVENT0("blink", "SelectionController::handleMouseReleaseEvent");

  if (!Selection().IsAvailable())
    return false;

  bool handled = false;
  mouse_down_may_start_select_ = false;
  // Clear the selection if the mouse didn't move after the last mouse
  // press and it's not a context menu click.  We do this so when clicking
  // on the selection, the selection goes away.  However, if we are
  // editing, place the caret.
  if (mouse_down_was_single_click_in_selection_ &&
      selection_state_ != SelectionState::kExtendedSelection &&
      drag_start_pos == FlooredIntPoint(event.Event().PositionInRootFrame()) &&
      Selection().ComputeVisibleSelectionInDOMTreeDeprecated().IsRange() &&
      event.Event().button != WebPointerProperties::Button::kRight) {
    // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
    // needs to be audited.  See http://crbug.com/590369 for more details.
    frame_->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

    SelectionInFlatTree::Builder builder;
    Node* node = event.InnerNode();
    if (node && node->GetLayoutObject() && HasEditableStyle(*node)) {
      const VisiblePositionInFlatTree pos =
          VisiblePositionOfHitTestResult(event.GetHitTestResult());
      if (pos.IsNotNull())
        builder.Collapse(pos.ToPositionWithAffinity());
    }

    if (Selection().ComputeVisibleSelectionInFlatTree() !=
        CreateVisibleSelection(builder.Build())) {
      Selection().SetSelection(ConvertToSelectionInDOMTree(builder.Build()));
    }

    handled = true;
  }

  Selection().NotifyTextControlOfSelectionChange(SetSelectionBy::kUser);

  Selection().SelectFrameElementInParentIfFullySelected();

  if (event.Event().button == WebPointerProperties::Button::kMiddle &&
      !event.IsOverLink()) {
    // Ignore handled, since we want to paste to where the caret was placed
    // anyway.
    handled = HandlePasteGlobalSelection(event.Event()) || handled;
  }

  return handled;
}

bool SelectionController::HandlePasteGlobalSelection(
    const WebMouseEvent& mouse_event) {
  // If the event was a middle click, attempt to copy global selection in after
  // the newly set caret position.
  //
  // This code is called from either the mouse up or mouse down handling. There
  // is some debate about when the global selection is pasted:
  //   xterm: pastes on up.
  //   GTK: pastes on down.
  //   Qt: pastes on up.
  //   Firefox: pastes on up.
  //   Chromium: pastes on up.
  //
  // There is something of a webcompat angle to this well, as highlighted by
  // crbug.com/14608. Pages can clear text boxes 'onclick' and, if we paste on
  // down then the text is pasted just before the onclick handler runs and
  // clears the text box. So it's important this happens after the event
  // handlers have been fired.
  if (mouse_event.GetType() != WebInputEvent::kMouseUp)
    return false;

  if (!frame_->GetPage())
    return false;
  Frame* focus_frame =
      frame_->GetPage()->GetFocusController().FocusedOrMainFrame();
  // Do not paste here if the focus was moved somewhere else.
  if (frame_ == focus_frame &&
      frame_->GetEditor().Behavior().SupportsGlobalSelection())
    return frame_->GetEditor().CreateCommand("PasteGlobalSelection").Execute();

  return false;
}

bool SelectionController::HandleGestureLongPress(
    const HitTestResult& hit_test_result) {
  TRACE_EVENT0("blink", "SelectionController::handleGestureLongPress");

  if (!Selection().IsAvailable())
    return false;
  if (hit_test_result.IsLiveLink())
    return false;

  Node* inner_node = hit_test_result.InnerNode();
  inner_node->GetDocument().UpdateStyleAndLayoutTree();
  bool inner_node_is_selectable = HasEditableStyle(*inner_node) ||
                                  inner_node->IsTextNode() ||
                                  inner_node->CanStartSelection();
  if (!inner_node_is_selectable)
    return false;

  if (SelectClosestWordFromHitTestResult(hit_test_result,
                                         AppendTrailingWhitespace::kDontAppend,
                                         SelectInputEventType::kTouch))
    return Selection().IsAvailable();

  if (!inner_node->isConnected() || !inner_node->GetLayoutObject())
    return false;
  SetCaretAtHitTestResult(hit_test_result);
  return false;
}

void SelectionController::HandleGestureTwoFingerTap(
    const GestureEventWithHitTestResults& targeted_event) {
  TRACE_EVENT0("blink", "SelectionController::handleGestureTwoFingerTap");

  SetCaretAtHitTestResult(targeted_event.GetHitTestResult());
}

void SelectionController::HandleGestureLongTap(
    const GestureEventWithHitTestResults& targeted_event) {
  TRACE_EVENT0("blink", "SelectionController::handleGestureLongTap");

  SetCaretAtHitTestResult(targeted_event.GetHitTestResult());
}

static bool HitTestResultIsMisspelled(const HitTestResult& result) {
  Node* inner_node = result.InnerNode();
  if (!inner_node || !inner_node->GetLayoutObject())
    return false;
  VisiblePosition pos = CreateVisiblePosition(
      inner_node->GetLayoutObject()->PositionForPoint(result.LocalPoint()));
  if (pos.IsNull())
    return false;
  const Position& marker_position =
      pos.DeepEquivalent().ParentAnchoredEquivalent();
  return inner_node->GetDocument().Markers().MarkerAtPosition(
      marker_position, DocumentMarker::MisspellingMarkers());
}

void SelectionController::SendContextMenuEvent(
    const MouseEventWithHitTestResults& mev,
    const LayoutPoint& position) {
  if (!Selection().IsAvailable())
    return;
  if (Selection().Contains(position) || mev.GetScrollbar() ||
      // FIXME: In the editable case, word selection sometimes selects content
      // that isn't underneath the mouse.
      // If the selection is non-editable, we do word selection to make it
      // easier to use the contextual menu items available for text selections.
      // But only if we're above text.
      !(Selection()
            .ComputeVisibleSelectionInDOMTreeDeprecated()
            .IsContentEditable() ||
        (mev.InnerNode() && mev.InnerNode()->IsTextNode())))
    return;

  // Context menu events are always allowed to perform a selection.
  AutoReset<bool> mouse_down_may_start_select_change(
      &mouse_down_may_start_select_, true);

  if (mev.Event().menu_source_type != kMenuSourceTouchHandle &&
      HitTestResultIsMisspelled(mev.GetHitTestResult()))
    return SelectClosestMisspellingFromMouseEvent(mev);

  if (!frame_->GetEditor().Behavior().ShouldSelectOnContextualMenuClick())
    return;

  SelectClosestWordOrLinkFromMouseEvent(mev);
}

void SelectionController::PassMousePressEventToSubframe(
    const MouseEventWithHitTestResults& mev) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame_->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // If we're clicking into a frame that is selected, the frame will appear
  // greyed out even though we're clicking on the selection.  This looks
  // really strange (having the whole frame be greyed out), so we deselect the
  // selection.
  IntPoint p = frame_->View()->RootFrameToContents(
      FlooredIntPoint(mev.Event().PositionInRootFrame()));
  if (!Selection().Contains(p))
    return;

  const VisiblePositionInFlatTree& visible_pos =
      VisiblePositionOfHitTestResult(mev.GetHitTestResult());
  if (visible_pos.IsNull()) {
    Selection().SetSelection(SelectionInDOMTree());
    return;
  }
  Selection().SetSelection(ConvertToSelectionInDOMTree(
      SelectionInFlatTree::Builder()
          .Collapse(visible_pos.ToPositionWithAffinity())
          .Build()));
}

void SelectionController::InitializeSelectionState() {
  selection_state_ = SelectionState::kHaveNotStartedSelection;
}

void SelectionController::SetMouseDownMayStartSelect(bool may_start_select) {
  mouse_down_may_start_select_ = may_start_select;
}

bool SelectionController::MouseDownMayStartSelect() const {
  return mouse_down_may_start_select_;
}

bool SelectionController::MouseDownWasSingleClickInSelection() const {
  return mouse_down_was_single_click_in_selection_;
}

void SelectionController::NotifySelectionChanged() {
  // To avoid regression on speedometer benchmark[1] test, we should not
  // update layout tree in this code block.
  // [1] http://browserbench.org/Speedometer/
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      frame_->GetDocument()->Lifecycle());

  const SelectionInDOMTree& selection =
      this->Selection().GetSelectionInDOMTree();
  switch (selection.Type()) {
    case kNoSelection:
      selection_state_ = SelectionState::kHaveNotStartedSelection;
      return;
    case kCaretSelection:
      selection_state_ = SelectionState::kPlacedCaret;
      return;
    case kRangeSelection:
      selection_state_ = SelectionState::kExtendedSelection;
      return;
  }
  NOTREACHED() << "We should handle all SelectionType" << selection;
}

FrameSelection& SelectionController::Selection() const {
  return frame_->Selection();
}

bool IsLinkSelection(const MouseEventWithHitTestResults& event) {
  return (event.Event().GetModifiers() & WebInputEvent::Modifiers::kAltKey) !=
             0 &&
         event.IsOverLink();
}

bool IsExtendingSelection(const MouseEventWithHitTestResults& event) {
  bool is_mouse_down_on_link_or_image =
      event.IsOverLink() || event.GetHitTestResult().GetImage();
  return (event.Event().GetModifiers() & WebInputEvent::Modifiers::kShiftKey) !=
             0 &&
         !is_mouse_down_on_link_or_image;
}

}  // namespace blink
