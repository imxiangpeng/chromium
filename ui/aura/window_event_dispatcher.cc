// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/window_event_dispatcher.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "services/ui/public/interfaces/window_tree_constants.mojom.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/event_client.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/env.h"
#include "ui/aura/env_input_state_controller.h"
#include "ui/aura/mus/mus_mouse_location_updater.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_targeter.h"
#include "ui/aura/window_tracker.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/hit_test.h"
#include "ui/base/ime/input_method.h"
#include "ui/compositor/dip_util.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/gestures/gesture_recognizer.h"
#include "ui/events/gestures/gesture_types.h"

typedef ui::EventDispatchDetails DispatchDetails;

namespace aura {

namespace {

// Returns true if |target| has a non-client (frame) component at |location|,
// in window coordinates.
bool IsNonClientLocation(Window* target, const gfx::Point& location) {
  if (!target->delegate())
    return false;
  int hit_test_code = target->delegate()->GetNonClientComponent(location);
  return hit_test_code != HTCLIENT && hit_test_code != HTNOWHERE;
}

Window* ConsumerToWindow(ui::GestureConsumer* consumer) {
  return consumer ? static_cast<Window*>(consumer) : NULL;
}

bool IsEventCandidateForHold(const ui::Event& event) {
  if (event.type() == ui::ET_TOUCH_MOVED)
    return true;
  if (event.type() == ui::ET_MOUSE_DRAGGED)
    return true;
  if (event.IsMouseEvent() && (event.flags() & ui::EF_IS_SYNTHESIZED))
    return true;
  return false;
}

void ConvertEventLocationToTarget(ui::EventTarget* event_target,
                                  ui::EventTarget* target,
                                  ui::Event* event) {
  if (target == event_target || !event->IsLocatedEvent())
    return;

  gfx::Point location = event->AsLocatedEvent()->location();
  gfx::Point root_location = event->AsLocatedEvent()->root_location();
  Window::ConvertPointToTarget(static_cast<Window*>(event_target),
                               static_cast<Window*>(target), &location);
  Window::ConvertPointToTarget(static_cast<Window*>(event_target),
                               static_cast<Window*>(target), &root_location);
  event->AsLocatedEvent()->set_location(location);
  event->AsLocatedEvent()->set_root_location(root_location);
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, public:

WindowEventDispatcher::WindowEventDispatcher(WindowTreeHost* host)
    : host_(host),
      mouse_pressed_handler_(NULL),
      mouse_moved_handler_(NULL),
      event_dispatch_target_(NULL),
      old_dispatch_target_(NULL),
      synthesize_mouse_move_(false),
      move_hold_count_(0),
      dispatching_held_event_(nullptr),
      observer_manager_(this),
      event_targeter_(new WindowTargeter),
      skip_ime_(false),
      repost_event_factory_(this),
      held_event_factory_(this) {
  ui::GestureRecognizer::Get()->AddGestureEventHelper(this);
  Env::GetInstance()->AddObserver(this);
  if (Env::GetInstance()->mode() == Env::Mode::MUS)
    mus_mouse_location_updater_ = base::MakeUnique<MusMouseLocationUpdater>();
}

WindowEventDispatcher::~WindowEventDispatcher() {
  TRACE_EVENT0("shutdown", "WindowEventDispatcher::Destructor");
  Env::GetInstance()->RemoveObserver(this);
  ui::GestureRecognizer::Get()->RemoveGestureEventHelper(this);
}

ui::EventTargeter* WindowEventDispatcher::GetDefaultEventTargeter() {
  return event_targeter_.get();
}

void WindowEventDispatcher::RepostEvent(const ui::LocatedEvent* event) {
  DCHECK(event->type() == ui::ET_MOUSE_PRESSED ||
         event->type() == ui::ET_GESTURE_TAP_DOWN ||
         event->type() == ui::ET_TOUCH_PRESSED);
  // We allow for only one outstanding repostable event. This is used
  // in exiting context menus.  A dropped repost request is allowed.
  if (event->type() == ui::ET_MOUSE_PRESSED) {
    held_repostable_event_.reset(new ui::MouseEvent(
        *event->AsMouseEvent(), static_cast<aura::Window*>(event->target()),
        window()));
  } else if (event->type() == ui::ET_TOUCH_PRESSED) {
    held_repostable_event_.reset(new ui::TouchEvent(*event->AsTouchEvent()));
  } else {
    DCHECK(event->type() == ui::ET_GESTURE_TAP_DOWN);
    held_repostable_event_.reset();
    // TODO(rbyers): Reposing of gestures is tricky to get
    // right, so it's not yet supported.  crbug.com/170987.
  }

  if (held_repostable_event_) {
    base::ThreadTaskRunnerHandle::Get()->PostNonNestableTask(
        FROM_HERE, base::Bind(
            base::IgnoreResult(&WindowEventDispatcher::DispatchHeldEvents),
            repost_event_factory_.GetWeakPtr()));
  }
}

void WindowEventDispatcher::OnMouseEventsEnableStateChanged(bool enabled) {
  // Send entered / exited so that visual state can be updated to match
  // mouse events state.
  PostSynthesizeMouseMove();
  // TODO(mazda): Add code to disable mouse events when |enabled| == false.
}

void WindowEventDispatcher::DispatchCancelModeEvent() {
  ui::CancelModeEvent event;
  Window* focused_window = client::GetFocusClient(window())->GetFocusedWindow();
  if (focused_window && !window()->Contains(focused_window))
    focused_window = NULL;
  DispatchDetails details =
      DispatchEvent(focused_window ? focused_window : window(), &event);
  if (details.dispatcher_destroyed)
    return;
}

void WindowEventDispatcher::DispatchGestureEvent(
    ui::GestureConsumer* raw_input_consumer,
    ui::GestureEvent* event) {
  DispatchDetails details = DispatchHeldEvents();
  if (details.dispatcher_destroyed)
    return;
  Window* target = ConsumerToWindow(raw_input_consumer);
  if (target) {
    event->ConvertLocationToTarget(window(), target);
    DispatchDetails details = DispatchEvent(target, event);
    if (details.dispatcher_destroyed)
      return;
  }
}

DispatchDetails WindowEventDispatcher::DispatchMouseExitAtPoint(
    Window* window,
    const gfx::Point& point,
    int event_flags) {
  ui::MouseEvent event(ui::ET_MOUSE_EXITED, point, point, ui::EventTimeForNow(),
                       event_flags, ui::EF_NONE);
  return DispatchMouseEnterOrExit(window, event, ui::ET_MOUSE_EXITED);
}

void WindowEventDispatcher::ProcessedTouchEvent(
    uint32_t unique_event_id,
    Window* window,
    ui::EventResult result,
    bool is_source_touch_event_set_non_blocking) {
  ui::GestureRecognizer::Gestures gestures =
      ui::GestureRecognizer::Get()->AckTouchEvent(
          unique_event_id, result, is_source_touch_event_set_non_blocking,
          window);
  DispatchDetails details = ProcessGestures(window, std::move(gestures));
  if (details.dispatcher_destroyed)
    return;
}

void WindowEventDispatcher::HoldPointerMoves() {
  if (!move_hold_count_)
    held_event_factory_.InvalidateWeakPtrs();
  ++move_hold_count_;
  TRACE_EVENT_ASYNC_BEGIN0("ui", "WindowEventDispatcher::HoldPointerMoves",
                           this);
}

void WindowEventDispatcher::ReleasePointerMoves() {
  --move_hold_count_;
  DCHECK_GE(move_hold_count_, 0);
  if (!move_hold_count_ && held_move_event_) {
    // We don't want to call DispatchHeldEvents directly, because this might be
    // called from a deep stack while another event, in which case dispatching
    // another one may not be safe/expected.  Instead we post a task, that we
    // may cancel if HoldPointerMoves is called again before it executes.
    base::ThreadTaskRunnerHandle::Get()->PostNonNestableTask(
        FROM_HERE, base::Bind(
          base::IgnoreResult(&WindowEventDispatcher::DispatchHeldEvents),
          held_event_factory_.GetWeakPtr()));
  }
  TRACE_EVENT_ASYNC_END0("ui", "WindowEventDispatcher::HoldPointerMoves", this);
}

gfx::Point WindowEventDispatcher::GetLastMouseLocationInRoot() const {
  gfx::Point location = Env::GetInstance()->last_mouse_location();
  client::ScreenPositionClient* client =
      client::GetScreenPositionClient(window());
  if (client)
    client->ConvertPointFromScreen(window(), &location);
  return location;
}

void WindowEventDispatcher::OnHostLostMouseGrab() {
  mouse_pressed_handler_ = NULL;
  mouse_moved_handler_ = NULL;
}

void WindowEventDispatcher::OnCursorMovedToRootLocation(
    const gfx::Point& root_location) {
  Env::GetInstance()->env_controller()->SetLastMouseLocation(window(),
                                                             root_location);

  // Synthesize a mouse move in case the cursor's location in root coordinates
  // changed but its position in WindowTreeHost coordinates did not.
  PostSynthesizeMouseMove();
}

void WindowEventDispatcher::OnPostNotifiedWindowDestroying(Window* window) {
  OnWindowHidden(window, WINDOW_DESTROYED);
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, private:

Window* WindowEventDispatcher::window() {
  return host_->window();
}

const Window* WindowEventDispatcher::window() const {
  return host_->window();
}

void WindowEventDispatcher::TransformEventForDeviceScaleFactor(
    ui::LocatedEvent* event) {
  event->UpdateForRootTransform(host_->GetInverseRootTransform());
}

void WindowEventDispatcher::DispatchMouseExitToHidingWindow(Window* window) {
  // The mouse capture is intentionally ignored. Think that a mouse enters
  // to a window, the window sets the capture, the mouse exits the window,
  // and then it releases the capture. In that case OnMouseExited won't
  // be called. So it is natural not to emit OnMouseExited even though
  // |window| is the capture window.
  gfx::Point last_mouse_location = GetLastMouseLocationInRoot();
  if (window->Contains(mouse_moved_handler_) &&
      window->ContainsPointInRoot(last_mouse_location)) {
    DispatchDetails details =
        DispatchMouseExitAtPoint(this->window(), last_mouse_location);
    if (details.dispatcher_destroyed)
      return;
  }
}

ui::EventDispatchDetails WindowEventDispatcher::DispatchMouseEnterOrExit(
    Window* target,
    const ui::MouseEvent& event,
    ui::EventType type) {
  Env::GetInstance()->env_controller()->UpdateStateForMouseEvent(window(),
                                                                 event);
  if (!mouse_moved_handler_ || !mouse_moved_handler_->delegate() ||
      !window()->Contains(mouse_moved_handler_))
    return DispatchDetails();

  // |event| may be an event in the process of being dispatched to a target (in
  // which case its locations will be in the event's target's coordinate
  // system), or a synthetic event created in root-window (in which case, the
  // event's target will be NULL, and the event will be in the root-window's
  // coordinate system.
  if (!target)
    target = window();
  ui::MouseEvent translated_event(event,
                                  target,
                                  mouse_moved_handler_,
                                  type,
                                  event.flags() | ui::EF_IS_SYNTHESIZED);
  return DispatchEvent(mouse_moved_handler_, &translated_event);
}

ui::EventDispatchDetails WindowEventDispatcher::ProcessGestures(
    Window* target,
    ui::GestureRecognizer::Gestures gestures) {
  DispatchDetails details;
  if (gestures.empty())
    return details;

  // If a window has been hidden between the touch event and now, the associated
  // gestures may not have a valid target.
  if (!target)
    return details;

  for (const auto& event : gestures) {
    event->ConvertLocationToTarget(window(), target);
    details = DispatchEvent(target, event.get());
    if (details.dispatcher_destroyed || details.target_destroyed)
      break;
  }
  return details;
}

void WindowEventDispatcher::OnWindowHidden(Window* invisible,
                                           WindowHiddenReason reason) {
  // If the window the mouse was pressed in becomes invisible, it should no
  // longer receive mouse events.
  if (invisible->Contains(mouse_pressed_handler_))
    mouse_pressed_handler_ = NULL;
  if (invisible->Contains(mouse_moved_handler_))
    mouse_moved_handler_ = NULL;

  // If events are being dispatched from a nested message-loop, and the target
  // of the outer loop is hidden or moved to another dispatcher during
  // dispatching events in the inner loop, then reset the target for the outer
  // loop.
  if (invisible->Contains(old_dispatch_target_))
    old_dispatch_target_ = NULL;

  invisible->CleanupGestureState();

  // Do not clear the capture, and the |event_dispatch_target_| if the
  // window is moving across hosts, because the target itself is actually still
  // visible and clearing them stops further event processing, which can cause
  // unexpected behaviors. See crbug.com/157583
  if (reason != WINDOW_MOVING) {
    // We don't ask |invisible| here, because invisible may have been removed
    // from the window hierarchy already by the time this function is called
    // (OnWindowDestroyed).
    client::CaptureClient* capture_client =
        client::GetCaptureClient(host_->window());
    Window* capture_window =
        capture_client ? capture_client->GetCaptureWindow() : NULL;

    if (invisible->Contains(event_dispatch_target_))
      event_dispatch_target_ = NULL;

    // If the ancestor of the capture window is hidden, release the capture.
    // Note that this may delete the window so do not use capture_window
    // after this.
    if (invisible->Contains(capture_window) && invisible != window())
      capture_window->ReleaseCapture();
  }
}

bool WindowEventDispatcher::is_dispatched_held_event(
    const ui::Event& event) const {
  return dispatching_held_event_ == &event;
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, aura::client::CaptureDelegate implementation:

void WindowEventDispatcher::UpdateCapture(Window* old_capture,
                                          Window* new_capture) {
  // |mouse_moved_handler_| may have been set to a Window in a different root
  // (see below). Clear it here to ensure we don't end up referencing a stale
  // Window.
  if (mouse_moved_handler_ && !window()->Contains(mouse_moved_handler_))
    mouse_moved_handler_ = NULL;

  if (old_capture && old_capture->GetRootWindow() == window() &&
      old_capture->delegate()) {
    // Send a capture changed event with bogus location data.
    ui::MouseEvent event(ui::ET_MOUSE_CAPTURE_CHANGED, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(), 0, 0);

    DispatchDetails details = DispatchEvent(old_capture, &event);
    if (details.dispatcher_destroyed)
      return;

    if (!details.target_destroyed)
      old_capture->delegate()->OnCaptureLost();
  }

  if (new_capture) {
    // Make all subsequent mouse events go to the capture window. We shouldn't
    // need to send an event here as OnCaptureLost() should take care of that.
    if (mouse_moved_handler_ || Env::GetInstance()->IsMouseButtonDown())
      mouse_moved_handler_ = new_capture;
  } else {
    // Make sure mouse_moved_handler gets updated.
    DispatchDetails details = SynthesizeMouseMoveEvent();
    if (details.dispatcher_destroyed)
      return;
  }
  mouse_pressed_handler_ = NULL;
}

void WindowEventDispatcher::OnOtherRootGotCapture() {
  // Windows provides the TrackMouseEvents API which allows us to rely on the
  // OS to send us the mouse exit events (WM_MOUSELEAVE). Additionally on
  // desktop Windows, every top level window could potentially have its own
  // root window, in which case this function will get called whenever those
  // windows grab mouse capture. Sending mouse exit messages in these cases
  // causes subtle bugs like (crbug.com/394672).
#if !defined(OS_WIN)
  if (mouse_moved_handler_) {
    // Dispatch a mouse exit to reset any state associated with hover. This is
    // important when going from no window having capture to a window having
    // capture because we do not dispatch ET_MOUSE_CAPTURE_CHANGED in this case.
    DispatchDetails details =
        DispatchMouseExitAtPoint(nullptr, GetLastMouseLocationInRoot());
    if (details.dispatcher_destroyed)
      return;
  }
#endif

  mouse_moved_handler_ = NULL;
  mouse_pressed_handler_ = NULL;
}

void WindowEventDispatcher::SetNativeCapture() {
  host_->SetCapture();
}

void WindowEventDispatcher::ReleaseNativeCapture() {
  host_->ReleaseCapture();
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, ui::EventProcessor implementation:
ui::EventTarget* WindowEventDispatcher::GetRootForEvent(ui::Event* event) {
  if (Env::GetInstance()->mode() == Env::Mode::LOCAL)
    return window();

  if (!event->target())
    return window();

  ui::EventTarget* event_target = event->target();
  if (event->IsLocatedEvent()) {
    ui::EventTarget* target = event_targeter_->FindTargetInRootWindow(
        window(), *event->AsLocatedEvent());
    if (target) {
      ConvertEventLocationToTarget(event_target, target, event);
      return target;
    }
  }

  ui::EventTarget* ancestor_with_targeter = event_target;
  for (ui::EventTarget* ancestor = event_target; ancestor;
       ancestor = ancestor->GetParentTarget()) {
    if (ancestor->GetEventTargeter())
      ancestor_with_targeter = ancestor;
    if (ancestor == window())
      break;
  }
  ConvertEventLocationToTarget(event_target, ancestor_with_targeter, event);
  return ancestor_with_targeter;
}

void WindowEventDispatcher::OnEventProcessingStarted(ui::Event* event) {
  // The held events are already in |window()|'s coordinate system. So it is
  // not necessary to apply the transform to convert from the host's
  // coordinate system to |window()|'s coordinate system.
  if (event->IsLocatedEvent() && !is_dispatched_held_event(*event))
    TransformEventForDeviceScaleFactor(static_cast<ui::LocatedEvent*>(event));

  if (mus_mouse_location_updater_)
    mus_mouse_location_updater_->OnEventProcessingStarted(*event);
}

void WindowEventDispatcher::OnEventProcessingFinished(ui::Event* event) {
  if (mus_mouse_location_updater_)
    mus_mouse_location_updater_->OnEventProcessingFinished();
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, ui::EventDispatcherDelegate implementation:

bool WindowEventDispatcher::CanDispatchToTarget(ui::EventTarget* target) {
  return event_dispatch_target_ == target;
}

ui::EventDispatchDetails WindowEventDispatcher::PreDispatchEvent(
    ui::EventTarget* target,
    ui::Event* event) {
  Window* target_window = static_cast<Window*>(target);
  CHECK(window()->Contains(target_window));

  if (!(event->flags() & ui::EF_IS_SYNTHESIZED)) {
    fraction_of_time_without_user_input_recorder_.RecordEventAtTime(
        event->time_stamp());
  }

  if (!dispatching_held_event_) {
    bool can_be_held = IsEventCandidateForHold(*event);
    if (!move_hold_count_ || !can_be_held) {
      if (can_be_held)
        held_move_event_.reset();
      DispatchDetails details = DispatchHeldEvents();
      if (details.dispatcher_destroyed || details.target_destroyed)
        return details;
    }
  }

  DispatchDetails details;
  if (event->IsMouseEvent()) {
    details = PreDispatchMouseEvent(target_window, event->AsMouseEvent());
  } else if (event->IsScrollEvent()) {
    details = PreDispatchLocatedEvent(target_window, event->AsScrollEvent());
  } else if (event->IsTouchEvent()) {
    details = PreDispatchTouchEvent(target_window, event->AsTouchEvent());
  } else if (event->IsKeyEvent()) {
    details = PreDispatchKeyEvent(event->AsKeyEvent());
  }
  if (details.dispatcher_destroyed || details.target_destroyed)
    return details;

  old_dispatch_target_ = event_dispatch_target_;
  event_dispatch_target_ = target_window;
  return DispatchDetails();
}

ui::EventDispatchDetails WindowEventDispatcher::PostDispatchEvent(
    ui::EventTarget* target,
    const ui::Event& event) {
  DispatchDetails details;
  if (!target || target != event_dispatch_target_)
    details.target_destroyed = true;
  event_dispatch_target_ = old_dispatch_target_;
  old_dispatch_target_ = NULL;
#ifndef NDEBUG
  DCHECK(!event_dispatch_target_ || window()->Contains(event_dispatch_target_));
#endif

  if (event.IsTouchEvent() && !details.target_destroyed) {
    // Do not let 'held' touch events contribute to any gestures unless it is
    // being dispatched.
    if (is_dispatched_held_event(event) || !held_move_event_ ||
        !held_move_event_->IsTouchEvent()) {
      const ui::TouchEvent& touchevent = *event.AsTouchEvent();

      if (!touchevent.synchronous_handling_disabled()) {
        Window* window = static_cast<Window*>(target);
        ui::GestureRecognizer::Gestures gestures =
            ui::GestureRecognizer::Get()->AckTouchEvent(
                touchevent.unique_event_id(), event.result(),
                false /* is_source_touch_event_set_non_blocking */, window);

        return ProcessGestures(window, std::move(gestures));
      }
    }
  }

  return details;
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, ui::GestureEventHelper implementation:

bool WindowEventDispatcher::CanDispatchToConsumer(
    ui::GestureConsumer* consumer) {
  Window* consumer_window = ConsumerToWindow(consumer);
  return (consumer_window && consumer_window->GetRootWindow() == window());
}

void WindowEventDispatcher::DispatchSyntheticTouchEvent(ui::TouchEvent* event) {
  // The synthetic event's location is based on the last known location of
  // the pointer, in dips. OnEventFromSource expects events with co-ordinates
  // in raw pixels, so we convert back to raw pixels here.
  DCHECK(event->type() == ui::ET_TOUCH_CANCELLED);
  event->UpdateForRootTransform(host_->GetRootTransform());
  DispatchDetails details = OnEventFromSource(event);
  if (details.dispatcher_destroyed)
    return;
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, WindowObserver implementation:

void WindowEventDispatcher::OnWindowDestroying(Window* window) {
  if (!host_->window()->Contains(window))
    return;

  SynthesizeMouseMoveAfterChangeToWindow(window);
}

void WindowEventDispatcher::OnWindowDestroyed(Window* window) {
  // We observe all windows regardless of what root Window (if any) they're
  // attached to.
  observer_manager_.Remove(window);
}

void WindowEventDispatcher::OnWindowAddedToRootWindow(Window* attached) {
  if (!observer_manager_.IsObserving(attached))
    observer_manager_.Add(attached);

  if (!host_->window()->Contains(attached))
    return;

  SynthesizeMouseMoveAfterChangeToWindow(attached);
}

void WindowEventDispatcher::OnWindowRemovingFromRootWindow(Window* detached,
                                                           Window* new_root) {
  if (!host_->window()->Contains(detached))
    return;

  DCHECK(client::GetCaptureWindow(window()) != window());

  DispatchMouseExitToHidingWindow(detached);
  SynthesizeMouseMoveAfterChangeToWindow(detached);

  // Hiding the window releases capture which can implicitly destroy the window
  // so the window may no longer be valid after this call.
  OnWindowHidden(detached, new_root ? WINDOW_MOVING : WINDOW_HIDDEN);
}

void WindowEventDispatcher::OnWindowVisibilityChanging(Window* window,
                                                       bool visible) {
  if (!host_->window()->Contains(window))
    return;

  DispatchMouseExitToHidingWindow(window);
}

void WindowEventDispatcher::OnWindowVisibilityChanged(Window* window,
                                                      bool visible) {
  if (!host_->window()->Contains(window))
    return;

  if (window->ContainsPointInRoot(GetLastMouseLocationInRoot()))
    PostSynthesizeMouseMove();

  // Hiding the window releases capture which can implicitly destroy the window
  // so the window may no longer be valid after this call.
  if (!visible)
    OnWindowHidden(window, WINDOW_HIDDEN);
}

void WindowEventDispatcher::OnWindowBoundsChanged(Window* window,
                                                  const gfx::Rect& old_bounds,
                                                  const gfx::Rect& new_bounds) {
  if (!host_->window()->Contains(window))
    return;

  if (window == host_->window()) {
    TRACE_EVENT1("ui", "WindowEventDispatcher::OnWindowBoundsChanged(root)",
                 "size", new_bounds.size().ToString());

    DispatchDetails details = DispatchHeldEvents();
    if (details.dispatcher_destroyed)
      return;

    synthesize_mouse_move_ = false;
  }

  if (window->IsVisible() && window->event_targeting_policy() !=
                                 ui::mojom::EventTargetingPolicy::NONE) {
    gfx::Rect old_bounds_in_root = old_bounds, new_bounds_in_root = new_bounds;
    Window::ConvertRectToTarget(window->parent(), host_->window(),
                                &old_bounds_in_root);
    Window::ConvertRectToTarget(window->parent(), host_->window(),
                                &new_bounds_in_root);
    gfx::Point last_mouse_location = GetLastMouseLocationInRoot();
    if (old_bounds_in_root.Contains(last_mouse_location) !=
        new_bounds_in_root.Contains(last_mouse_location)) {
      PostSynthesizeMouseMove();
    }
  }
}

void WindowEventDispatcher::OnWindowTransforming(Window* window) {
  if (!host_->window()->Contains(window))
    return;

  SynthesizeMouseMoveAfterChangeToWindow(window);
}

void WindowEventDispatcher::OnWindowTransformed(Window* window) {
  if (!host_->window()->Contains(window))
    return;

  SynthesizeMouseMoveAfterChangeToWindow(window);
}

///////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, EnvObserver implementation:

void WindowEventDispatcher::OnWindowInitialized(Window* window) {
  observer_manager_.Add(window);
}

////////////////////////////////////////////////////////////////////////////////
// WindowEventDispatcher, private:

ui::EventDispatchDetails WindowEventDispatcher::DispatchHeldEvents() {
  if (!held_repostable_event_ && !held_move_event_)
    return DispatchDetails();

  CHECK(!dispatching_held_event_);

  DispatchDetails dispatch_details;
  if (held_repostable_event_) {
    if (held_repostable_event_->type() == ui::ET_MOUSE_PRESSED ||
        held_repostable_event_->type() == ui::ET_TOUCH_PRESSED) {
      std::unique_ptr<ui::LocatedEvent> event =
          std::move(held_repostable_event_);
      dispatching_held_event_ = event.get();
      dispatch_details = OnEventFromSource(event.get());
    } else {
      // TODO(rbyers): GESTURE_TAP_DOWN not yet supported: crbug.com/170987.
      NOTREACHED();
    }
    if (dispatch_details.dispatcher_destroyed)
      return dispatch_details;
  }

  if (held_move_event_) {
    // If a mouse move has been synthesized, the target location is suspect,
    // so drop the held mouse event.
    if (held_move_event_->IsTouchEvent() ||
        (held_move_event_->IsMouseEvent() && !synthesize_mouse_move_)) {
      dispatching_held_event_ = held_move_event_.get();
      dispatch_details = OnEventFromSource(held_move_event_.get());
    }
    if (!dispatch_details.dispatcher_destroyed)
      held_move_event_.reset();
  }

  if (!dispatch_details.dispatcher_destroyed)
    dispatching_held_event_ = nullptr;
  return dispatch_details;
}

void WindowEventDispatcher::PostSynthesizeMouseMove() {
  if (synthesize_mouse_move_)
    return;
  synthesize_mouse_move_ = true;
  base::ThreadTaskRunnerHandle::Get()->PostNonNestableTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(
          &WindowEventDispatcher::SynthesizeMouseMoveEvent),
          held_event_factory_.GetWeakPtr()));
}

void WindowEventDispatcher::SynthesizeMouseMoveAfterChangeToWindow(
    Window* window) {
  if (window->IsVisible() &&
      window->ContainsPointInRoot(GetLastMouseLocationInRoot())) {
    PostSynthesizeMouseMove();
  }
}

ui::EventDispatchDetails WindowEventDispatcher::SynthesizeMouseMoveEvent() {
  DispatchDetails details;
  if (!synthesize_mouse_move_)
    return details;
  synthesize_mouse_move_ = false;

  // If one of the mouse buttons is currently down, then do not synthesize a
  // mouse-move event. In such cases, aura could synthesize a DRAGGED event
  // instead of a MOVED event, but in multi-display/multi-host scenarios, the
  // DRAGGED event can be synthesized in the incorrect host. So avoid
  // synthesizing any events at all.
  if (Env::GetInstance()->mouse_button_flags())
    return details;

  gfx::Point root_mouse_location = GetLastMouseLocationInRoot();
  if (!window()->bounds().Contains(root_mouse_location))
    return details;
  gfx::Point host_mouse_location = root_mouse_location;
  host_->ConvertDIPToPixels(&host_mouse_location);
  ui::MouseEvent event(ui::ET_MOUSE_MOVED, host_mouse_location,
                       host_mouse_location, ui::EventTimeForNow(),
                       ui::EF_IS_SYNTHESIZED, 0);
  return OnEventFromSource(&event);
}

DispatchDetails WindowEventDispatcher::PreDispatchLocatedEvent(
    Window* target,
    ui::LocatedEvent* event) {
  int flags = event->flags();
  if (IsNonClientLocation(target, event->location()))
    flags |= ui::EF_IS_NON_CLIENT;
  event->set_flags(flags);

  if (!is_dispatched_held_event(*event) &&
      (event->IsMouseEvent() || event->IsScrollEvent()) &&
      !(event->flags() & ui::EF_IS_SYNTHESIZED)) {
    synthesize_mouse_move_ = false;
  }

  return DispatchDetails();
}

DispatchDetails WindowEventDispatcher::PreDispatchMouseEvent(
    Window* target,
    ui::MouseEvent* event) {
  client::CursorClient* cursor_client = client::GetCursorClient(window());
  // We allow synthesized mouse exit events through even if mouse events are
  // disabled. This ensures that hover state, etc on controls like buttons is
  // cleared.
  if (cursor_client &&
      !cursor_client->IsMouseEventsEnabled() &&
      (event->flags() & ui::EF_IS_SYNTHESIZED) &&
      (event->type() != ui::ET_MOUSE_EXITED)) {
    event->SetHandled();
    return DispatchDetails();
  }

  Env::GetInstance()->env_controller()->UpdateStateForMouseEvent(window(),
                                                                 *event);

  if (IsEventCandidateForHold(*event) && !dispatching_held_event_) {
    if (move_hold_count_) {
      held_move_event_.reset(new ui::MouseEvent(*event, target, window()));
      event->SetHandled();
      return DispatchDetails();
    } else {
      // We may have a held event for a period between the time move_hold_count_
      // fell to 0 and the DispatchHeldEvents executes. Since we're going to
      // dispatch the new event directly below, we can reset the old one.
      held_move_event_.reset();
    }
  }

  switch (event->type()) {
    case ui::ET_MOUSE_EXITED:
      if (!target || target == window()) {
        DispatchDetails details =
            DispatchMouseEnterOrExit(target, *event, ui::ET_MOUSE_EXITED);
        if (details.dispatcher_destroyed) {
          event->SetHandled();
          return details;
        }
        mouse_moved_handler_ = NULL;
      }
      break;
    case ui::ET_MOUSE_MOVED:
      // Send an exit to the current |mouse_moved_handler_| and an enter to
      // |target|. Take care that both us and |target| aren't destroyed during
      // dispatch.
      if (target != mouse_moved_handler_) {
        aura::Window* old_mouse_moved_handler = mouse_moved_handler_;
        WindowTracker live_window;
        live_window.Add(target);
        DispatchDetails details =
            DispatchMouseEnterOrExit(target, *event, ui::ET_MOUSE_EXITED);
        // |details| contains information about |mouse_moved_handler_| being
        // destroyed which is not our |target|. Return value of this function
        // should be about our |target|.
        DispatchDetails target_details = details;
        target_details.target_destroyed = !live_window.Contains(target);
        if (details.dispatcher_destroyed) {
          event->SetHandled();
          return target_details;
        }
        // If the |mouse_moved_handler_| changes out from under us, assume a
        // nested run loop ran and we don't need to do anything.
        if (mouse_moved_handler_ != old_mouse_moved_handler) {
          event->SetHandled();
          return target_details;
        }
        if (details.target_destroyed || target_details.target_destroyed) {
          mouse_moved_handler_ = NULL;
          event->SetHandled();
          return target_details;
        }
        live_window.Remove(target);

        mouse_moved_handler_ = target;
        details =
            DispatchMouseEnterOrExit(target, *event, ui::ET_MOUSE_ENTERED);
        if (details.dispatcher_destroyed || details.target_destroyed) {
          event->SetHandled();
          return details;
        }
      }
      break;
    case ui::ET_MOUSE_PRESSED:
      // Don't set the mouse pressed handler for non client mouse down events.
      // These are only sent by Windows and are not always followed with non
      // client mouse up events which causes subsequent mouse events to be
      // sent to the wrong target.
      if (!(event->flags() & ui::EF_IS_NON_CLIENT) && !mouse_pressed_handler_)
        mouse_pressed_handler_ = target;
      break;
    case ui::ET_MOUSE_RELEASED:
      mouse_pressed_handler_ = NULL;
      break;
    default:
      break;
  }

  return PreDispatchLocatedEvent(target, event);
}

DispatchDetails WindowEventDispatcher::PreDispatchTouchEvent(
    Window* target,
    ui::TouchEvent* event) {
  if (event->type() == ui::ET_TOUCH_MOVED && move_hold_count_ &&
      !dispatching_held_event_) {
    held_move_event_.reset(new ui::TouchEvent(*event, target, window()));
    event->SetHandled();
    return DispatchDetails();
  }

  Env::GetInstance()->env_controller()->UpdateStateForTouchEvent(*event);

  ui::TouchEvent orig_event(*event, target, window());
  if (!ui::GestureRecognizer::Get()->ProcessTouchEventPreDispatch(&orig_event,
                                                                  target)) {
    // The event is invalid - ignore it.
    event->StopPropagation();
    event->DisableSynchronousHandling();
    return DispatchDetails();
  }

  // This flag is set depending on the gestures recognized in the call above,
  // and needs to propagate with the forwarded event.
  event->set_may_cause_scrolling(orig_event.may_cause_scrolling());

  return PreDispatchLocatedEvent(target, event);
}

DispatchDetails WindowEventDispatcher::PreDispatchKeyEvent(
    ui::KeyEvent* event) {
  if (skip_ime_ || !host_->has_input_method() ||
      (event->flags() & ui::EF_IS_SYNTHESIZED))
    return DispatchDetails();
  DispatchDetails details = host_->GetInputMethod()->DispatchKeyEvent(event);
  event->StopPropagation();
  return details;
}

}  // namespace aura
