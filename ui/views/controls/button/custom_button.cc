// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/custom_button.h"

#include "ui/accessibility/ax_node_data.h"
#include "ui/base/class_property.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/color_palette.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/controls/button/blue_button.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/painter.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/aura/client/capture_client.h"
#include "ui/aura/window.h"
#endif

namespace views {

namespace {

DEFINE_LOCAL_UI_CLASS_PROPERTY_KEY(bool, kIsButtonProperty, false);

// How long the hover animation takes if uninterrupted.
const int kHoverFadeDurationMs = 150;

CustomButton::KeyClickAction GetKeyClickActionForEvent(
    const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_SPACE)
    return PlatformStyle::kKeyClickActionOnSpace;
  if (event.key_code() == ui::VKEY_RETURN &&
      PlatformStyle::kReturnClicksFocusedControl)
    return CustomButton::CLICK_ON_KEY_PRESS;
  return CustomButton::CLICK_NONE;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// CustomButton, public:

// static
const char CustomButton::kViewClassName[] = "CustomButton";

// static
const CustomButton* CustomButton::AsCustomButton(const views::View* view) {
  return AsCustomButton(const_cast<View*>(view));
}

// static
CustomButton* CustomButton::AsCustomButton(views::View* view) {
  if (view && view->GetProperty(kIsButtonProperty))
    return static_cast<CustomButton*>(view);
  return NULL;
}

CustomButton::~CustomButton() {}

void CustomButton::SetState(ButtonState state) {
  if (state == state_)
    return;

  if (animate_on_state_change_ &&
      (!is_throbbing_ || !hover_animation_.is_animating())) {
    is_throbbing_ = false;
    if ((state_ == STATE_HOVERED) && (state == STATE_NORMAL)) {
      // For HOVERED -> NORMAL, animate from hovered (1) to not hovered (0).
      hover_animation_.Hide();
    } else if (state != STATE_HOVERED) {
      // For HOVERED -> PRESSED/DISABLED, or any transition not involving
      // HOVERED at all, simply set the state to not hovered (0).
      hover_animation_.Reset();
    } else if (state_ == STATE_NORMAL) {
      // For NORMAL -> HOVERED, animate from not hovered (0) to hovered (1).
      hover_animation_.Show();
    } else {
      // For PRESSED/DISABLED -> HOVERED, simply set the state to hovered (1).
      hover_animation_.Reset(1);
    }
  }

  ButtonState old_state = state_;
  state_ = state;
  StateChanged(old_state);
  SchedulePaint();
}

void CustomButton::StartThrobbing(int cycles_til_stop) {
  if (!animate_on_state_change_)
    return;
  is_throbbing_ = true;
  hover_animation_.StartThrobbing(cycles_til_stop);
}

void CustomButton::StopThrobbing() {
  if (hover_animation_.is_animating()) {
    hover_animation_.Stop();
    SchedulePaint();
  }
}

void CustomButton::SetAnimationDuration(int duration) {
  hover_animation_.SetSlideDuration(duration);
}

void CustomButton::SetHotTracked(bool is_hot_tracked) {
  if (state_ != STATE_DISABLED)
    SetState(is_hot_tracked ? STATE_HOVERED : STATE_NORMAL);

  if (is_hot_tracked)
    NotifyAccessibilityEvent(ui::AX_EVENT_HOVER, true);
}

bool CustomButton::IsHotTracked() const {
  return state_ == STATE_HOVERED;
}

void CustomButton::SetFocusPainter(std::unique_ptr<Painter> focus_painter) {
  focus_painter_ = std::move(focus_painter);
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, View overrides:

void CustomButton::OnEnabledChanged() {
  // TODO(bruthig): Is there any reason we are not calling
  // Button::OnEnabledChanged() here?
  if (enabled() ? (state_ != STATE_DISABLED) : (state_ == STATE_DISABLED))
    return;

  if (enabled()) {
    bool should_enter_hover_state = ShouldEnterHoveredState();
    SetState(should_enter_hover_state ? STATE_HOVERED : STATE_NORMAL);
    GetInkDrop()->SetHovered(should_enter_hover_state);
  } else {
    SetState(STATE_DISABLED);
    GetInkDrop()->SetHovered(false);
  }
}

const char* CustomButton::GetClassName() const {
  return kViewClassName;
}

bool CustomButton::OnMousePressed(const ui::MouseEvent& event) {
  if (state_ == STATE_DISABLED)
    return true;
  if (state_ != STATE_PRESSED && ShouldEnterPushedState(event) &&
      HitTestPoint(event.location())) {
    SetState(STATE_PRESSED);
    AnimateInkDrop(views::InkDropState::ACTION_PENDING, &event);
  }
  if (request_focus_on_press_)
    RequestFocus();
  if (IsTriggerableEvent(event) && notify_action_ == NOTIFY_ON_PRESS) {
    NotifyClick(event);
    // NOTE: We may be deleted at this point (by the listener's notification
    // handler).
  }
  return true;
}

bool CustomButton::OnMouseDragged(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED) {
    const bool should_enter_pushed = ShouldEnterPushedState(event);
    const bool should_show_pending =
        should_enter_pushed && notify_action_ == NOTIFY_ON_RELEASE && !InDrag();
    if (HitTestPoint(event.location())) {
      SetState(should_enter_pushed ? STATE_PRESSED : STATE_HOVERED);
      if (should_show_pending &&
          GetInkDrop()->GetTargetInkDropState() ==
              views::InkDropState::HIDDEN) {
        AnimateInkDrop(views::InkDropState::ACTION_PENDING, &event);
      }
    } else {
      SetState(STATE_NORMAL);
      if (should_show_pending &&
          GetInkDrop()->GetTargetInkDropState() ==
              views::InkDropState::ACTION_PENDING) {
        AnimateInkDrop(views::InkDropState::HIDDEN, &event);
      }
    }
  }
  return true;
}

void CustomButton::OnMouseReleased(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED) {
    if (!HitTestPoint(event.location())) {
      SetState(STATE_NORMAL);
    } else {
      SetState(STATE_HOVERED);
      if (IsTriggerableEvent(event) && notify_action_ == NOTIFY_ON_RELEASE) {
        NotifyClick(event);
        // NOTE: We may be deleted at this point (by the listener's notification
        // handler).
        return;
      }
    }
  }
  if (notify_action_ == NOTIFY_ON_RELEASE)
    OnClickCanceled(event);
}

void CustomButton::OnMouseCaptureLost() {
  // Starting a drag results in a MouseCaptureLost. Reset button state.
  // TODO(varkha): Reset the state even while in drag. The same logic may
  // applies everywhere so gather any feedback and update.
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  AnimateInkDrop(views::InkDropState::HIDDEN, nullptr /* event */);
  GetInkDrop()->SetHovered(false);
  Button::OnMouseCaptureLost();
}

void CustomButton::OnMouseEntered(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED)
    SetState(STATE_HOVERED);
}

void CustomButton::OnMouseExited(const ui::MouseEvent& event) {
  // Starting a drag results in a MouseExited, we need to ignore it.
  if (state_ != STATE_DISABLED && !InDrag())
    SetState(STATE_NORMAL);
}

void CustomButton::OnMouseMoved(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED)
    SetState(HitTestPoint(event.location()) ? STATE_HOVERED : STATE_NORMAL);
}

bool CustomButton::OnKeyPressed(const ui::KeyEvent& event) {
  if (state_ == STATE_DISABLED)
    return false;

  switch (GetKeyClickActionForEvent(event)) {
    case KeyClickAction::CLICK_ON_KEY_RELEASE:
      SetState(STATE_PRESSED);
      if (GetInkDrop()->GetTargetInkDropState() !=
          InkDropState::ACTION_PENDING) {
        AnimateInkDrop(InkDropState::ACTION_PENDING, nullptr /* event */);
      }
      return true;
    case KeyClickAction::CLICK_ON_KEY_PRESS:
      SetState(STATE_NORMAL);
      NotifyClick(event);
      return true;
    case KeyClickAction::CLICK_NONE:
      return false;
  }

  NOTREACHED();
  return false;
}

bool CustomButton::OnKeyReleased(const ui::KeyEvent& event) {
  const bool click_button =
      state_ == STATE_PRESSED &&
      GetKeyClickActionForEvent(event) == KeyClickAction::CLICK_ON_KEY_RELEASE;
  if (!click_button)
    return false;

  SetState(STATE_NORMAL);
  NotifyClick(event);
  return true;
}

void CustomButton::OnGestureEvent(ui::GestureEvent* event) {
  if (state_ == STATE_DISABLED) {
    Button::OnGestureEvent(event);
    return;
  }

  if (event->type() == ui::ET_GESTURE_TAP && IsTriggerableEvent(*event)) {
    // Set the button state to hot and start the animation fully faded in. The
    // GESTURE_END event issued immediately after will set the state to
    // STATE_NORMAL beginning the fade out animation. See
    // http://crbug.com/131184.
    SetState(STATE_HOVERED);
    hover_animation_.Reset(1.0);
    NotifyClick(*event);
    event->StopPropagation();
  } else if (event->type() == ui::ET_GESTURE_TAP_DOWN &&
             ShouldEnterPushedState(*event)) {
    SetState(STATE_PRESSED);
    if (request_focus_on_press_)
      RequestFocus();
    event->StopPropagation();
  } else if (event->type() == ui::ET_GESTURE_TAP_CANCEL ||
             event->type() == ui::ET_GESTURE_END) {
    SetState(STATE_NORMAL);
  }
  if (!event->handled())
    Button::OnGestureEvent(event);
}

bool CustomButton::AcceleratorPressed(const ui::Accelerator& accelerator) {
  SetState(STATE_NORMAL);
  NotifyClick(accelerator.ToKeyEvent());
  return true;
}

bool CustomButton::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  // If this button is focused and the user presses space or enter, don't let
  // that be treated as an accelerator if there is a key click action
  // corresponding to it.
  return GetKeyClickActionForEvent(event) != KeyClickAction::CLICK_NONE;
}

void CustomButton::ShowContextMenu(const gfx::Point& p,
                                   ui::MenuSourceType source_type) {
  if (!context_menu_controller())
    return;

  // We're about to show the context menu. Showing the context menu likely means
  // we won't get a mouse exited and reset state. Reset it now to be sure.
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  if (hide_ink_drop_when_showing_context_menu_) {
    GetInkDrop()->SetHovered(false);
    AnimateInkDrop(InkDropState::HIDDEN, nullptr /* event */);
  }
  View::ShowContextMenu(p, source_type);
}

void CustomButton::OnDragDone() {
  // Only reset the state to normal if the button isn't currently disabled
  // (since disabled buttons may still be able to be dragged).
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  AnimateInkDrop(InkDropState::HIDDEN, nullptr /* event */);
}

void CustomButton::OnPaint(gfx::Canvas* canvas) {
  Button::OnPaint(canvas);
  PaintButtonContents(canvas);
  Painter::PaintFocusPainter(this, canvas, focus_painter_.get());
}

void CustomButton::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  Button::GetAccessibleNodeData(node_data);
  switch (state_) {
    case STATE_HOVERED:
      node_data->AddState(ui::AX_STATE_HOVERED);
      break;
    case STATE_PRESSED:
      node_data->AddIntAttribute(ui::AX_ATTR_CHECKED_STATE,
                                 ui::AX_CHECKED_STATE_TRUE);
      break;
    case STATE_DISABLED:
      node_data->AddIntAttribute(ui::AX_ATTR_RESTRICTION,
                                 ui::AX_RESTRICTION_DISABLED);
      break;
    case STATE_NORMAL:
    case STATE_COUNT:
      // No additional accessibility node_data set for this button node_data.
      break;
  }
  if (enabled()) {
    node_data->AddIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB,
                               ui::AX_DEFAULT_ACTION_VERB_PRESS);
  }
}

void CustomButton::VisibilityChanged(View* starting_from, bool visible) {
  Button::VisibilityChanged(starting_from, visible);
  if (state_ == STATE_DISABLED)
    return;
  SetState(visible && ShouldEnterHoveredState() ? STATE_HOVERED : STATE_NORMAL);
}

std::unique_ptr<InkDrop> CustomButton::CreateInkDrop() {
  std::unique_ptr<views::InkDropImpl> ink_drop = CreateDefaultInkDropImpl();
  ink_drop->SetShowHighlightOnFocus(true);
  return std::move(ink_drop);
}

SkColor CustomButton::GetInkDropBaseColor() const {
  return ink_drop_base_color_;
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, gfx::AnimationDelegate implementation:

void CustomButton::AnimationProgressed(const gfx::Animation* animation) {
  SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, View overrides (public):

void CustomButton::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (!details.is_add && state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
}

void CustomButton::OnFocus() {
  Button::OnFocus();
  if (focus_painter_)
    SchedulePaint();
}

void CustomButton::OnBlur() {
  Button::OnBlur();
  if (IsHotTracked() || state_ == STATE_PRESSED) {
    SetState(STATE_NORMAL);
    if (GetInkDrop()->GetTargetInkDropState() != views::InkDropState::HIDDEN)
      AnimateInkDrop(views::InkDropState::HIDDEN, nullptr /* event */);
    // TODO(bruthig) : Fix CustomButtons to work well when multiple input
    // methods are interacting with a button. e.g. By animating to HIDDEN here
    // it is possible for a Mouse Release to trigger an action however there
    // would be no visual cue to the user that this will occur.
  }
  if (focus_painter_)
    SchedulePaint();
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, protected:

CustomButton::CustomButton(ButtonListener* listener)
    : Button(listener),
      state_(STATE_NORMAL),
      hover_animation_(this),
      is_throbbing_(false),
      triggerable_event_flags_(ui::EF_LEFT_MOUSE_BUTTON),
      request_focus_on_press_(false),
      notify_action_(NOTIFY_ON_RELEASE),
      has_ink_drop_action_on_click_(false),
      hide_ink_drop_when_showing_context_menu_(true),
      ink_drop_base_color_(gfx::kPlaceholderColor) {
  SetProperty(kIsButtonProperty, true);
  hover_animation_.SetSlideDuration(kHoverFadeDurationMs);
}

void CustomButton::StateChanged(ButtonState old_state) {}

bool CustomButton::IsTriggerableEvent(const ui::Event& event) {
  return event.type() == ui::ET_GESTURE_TAP_DOWN ||
         event.type() == ui::ET_GESTURE_TAP ||
         (event.IsMouseEvent() &&
             (triggerable_event_flags_ & event.flags()) != 0);
}

bool CustomButton::ShouldUpdateInkDropOnClickCanceled() const {
  return true;
}

bool CustomButton::ShouldEnterPushedState(const ui::Event& event) {
  return IsTriggerableEvent(event);
}

void CustomButton::PaintButtonContents(gfx::Canvas* canvas) {}

bool CustomButton::ShouldEnterHoveredState() {
  if (!visible())
    return false;

  bool check_mouse_position = true;
#if defined(USE_AURA)
  // If another window has capture, we shouldn't check the current mouse
  // position because the button won't receive any mouse events - so if the
  // mouse was hovered, the button would be stuck in a hovered state (since it
  // would never receive OnMouseExited).
  const Widget* widget = GetWidget();
  if (widget && widget->GetNativeWindow()) {
    aura::Window* root_window = widget->GetNativeWindow()->GetRootWindow();
    aura::client::CaptureClient* capture_client =
        aura::client::GetCaptureClient(root_window);
    aura::Window* capture_window =
        capture_client ? capture_client->GetGlobalCaptureWindow() : nullptr;
    check_mouse_position = !capture_window || capture_window == root_window;
  }
#endif

  return check_mouse_position && IsMouseHovered();
}

////////////////////////////////////////////////////////////////////////////////
// CustomButton, Button overrides (protected):

void CustomButton::NotifyClick(const ui::Event& event) {
  if (has_ink_drop_action_on_click_) {
    AnimateInkDrop(InkDropState::ACTION_TRIGGERED,
                   ui::LocatedEvent::FromIfValid(&event));
  }
  Button::NotifyClick(event);
}

void CustomButton::OnClickCanceled(const ui::Event& event) {
  if (ShouldUpdateInkDropOnClickCanceled()) {
    if (GetInkDrop()->GetTargetInkDropState() ==
            views::InkDropState::ACTION_PENDING ||
        GetInkDrop()->GetTargetInkDropState() ==
            views::InkDropState::ALTERNATE_ACTION_PENDING) {
      AnimateInkDrop(views::InkDropState::HIDDEN,
                     ui::LocatedEvent::FromIfValid(&event));
    }
  }
  Button::OnClickCanceled(event);
}

}  // namespace views
