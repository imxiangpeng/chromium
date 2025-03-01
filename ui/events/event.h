// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_EVENT_H_
#define UI_EVENTS_EVENT_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/compiler_specific.h"
#include "base/event_types.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "ui/events/event_constants.h"
#include "ui/events/gesture_event_details.h"
#include "ui/events/gestures/gesture_types.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/latency/latency_info.h"

namespace gfx {
class Transform;
}

namespace ui {
class CancelModeEvent;
class EventTarget;
class KeyEvent;
class LocatedEvent;
class MouseEvent;
class MouseWheelEvent;
class PointerEvent;
class ScrollEvent;
class TouchEvent;
enum class DomCode;
class Event;
class MouseWheelEvent;

using ScopedEvent = std::unique_ptr<Event>;
using PointerId = int32_t;

class EVENTS_EXPORT Event {
 public:
  static std::unique_ptr<Event> Clone(const Event& event);

  virtual ~Event();

  class DispatcherApi {
   public:
    explicit DispatcherApi(Event* event) : event_(event) {}

    void set_target(EventTarget* target) {
      event_->target_ = target;
    }

    void set_phase(EventPhase phase) { event_->phase_ = phase; }
    void set_result(int result) {
      event_->result_ = static_cast<EventResult>(result);
    }

   private:
    DispatcherApi();
    Event* event_;

    DISALLOW_COPY_AND_ASSIGN(DispatcherApi);
  };

  const base::NativeEvent& native_event() const { return native_event_; }
  EventType type() const { return type_; }
  // time_stamp represents time since machine was booted.
  const base::TimeTicks time_stamp() const { return time_stamp_; }
  int flags() const { return flags_; }

  // Returns a name for the event, typically used in logging/debugging.
  const char* GetName() const;

  // This is only intended to be used externally by classes that are modifying
  // events in an EventRewriter.
  void set_flags(int flags) { flags_ = flags; }

  EventTarget* target() const { return target_; }
  EventPhase phase() const { return phase_; }
  EventResult result() const { return result_; }

  LatencyInfo* latency() { return &latency_; }
  const LatencyInfo* latency() const { return &latency_; }
  void set_latency(const LatencyInfo& latency) { latency_ = latency; }

  int source_device_id() const { return source_device_id_; }
  void set_source_device_id(int id) { source_device_id_ = id; }

  // By default, events are "cancelable", this means any default processing that
  // the containing abstraction layer may perform can be prevented by calling
  // SetHandled(). SetHandled() or StopPropagation() must not be called for
  // events that are not cancelable.
  bool cancelable() const { return cancelable_; }

  // The following methods return true if the respective keys were pressed at
  // the time the event was created.
  bool IsShiftDown() const { return (flags_ & EF_SHIFT_DOWN) != 0; }
  bool IsControlDown() const { return (flags_ & EF_CONTROL_DOWN) != 0; }
  bool IsAltDown() const { return (flags_ & EF_ALT_DOWN) != 0; }
  bool IsCommandDown() const { return (flags_ & EF_COMMAND_DOWN) != 0; }
  bool IsAltGrDown() const { return (flags_ & EF_ALTGR_DOWN) != 0; }
  bool IsCapsLockOn() const { return (flags_ & EF_CAPS_LOCK_ON) != 0; }

  bool IsSynthesized() const { return (flags_ & EF_IS_SYNTHESIZED) != 0; }

  bool IsCancelModeEvent() const {
    return type_ == ET_CANCEL_MODE;
  }

  bool IsKeyEvent() const {
    return type_ == ET_KEY_PRESSED || type_ == ET_KEY_RELEASED;
  }

  bool IsMouseEvent() const {
    return type_ == ET_MOUSE_PRESSED ||
           type_ == ET_MOUSE_DRAGGED ||
           type_ == ET_MOUSE_RELEASED ||
           type_ == ET_MOUSE_MOVED ||
           type_ == ET_MOUSE_ENTERED ||
           type_ == ET_MOUSE_EXITED ||
           type_ == ET_MOUSEWHEEL ||
           type_ == ET_MOUSE_CAPTURE_CHANGED;
  }

  bool IsTouchEvent() const {
    return type_ == ET_TOUCH_RELEASED ||
           type_ == ET_TOUCH_PRESSED ||
           type_ == ET_TOUCH_MOVED ||
           type_ == ET_TOUCH_CANCELLED;
  }

  bool IsPointerEvent() const {
    return type_ == ET_POINTER_DOWN || type_ == ET_POINTER_MOVED ||
           type_ == ET_POINTER_UP || type_ == ET_POINTER_CANCELLED ||
           type_ == ET_POINTER_ENTERED || type_ == ET_POINTER_EXITED ||
           type_ == ET_POINTER_WHEEL_CHANGED ||
           type_ == ET_POINTER_CAPTURE_CHANGED;
  }

  // Convenience methods to check pointer type of |this|. Returns false if
  // |this| is not a PointerEvent.
  bool IsMousePointerEvent() const;
  bool IsTouchPointerEvent() const;

  bool IsGestureEvent() const {
    switch (type_) {
      case ET_GESTURE_SCROLL_BEGIN:
      case ET_GESTURE_SCROLL_END:
      case ET_GESTURE_SCROLL_UPDATE:
      case ET_GESTURE_TAP:
      case ET_GESTURE_TAP_CANCEL:
      case ET_GESTURE_TAP_DOWN:
      case ET_GESTURE_BEGIN:
      case ET_GESTURE_END:
      case ET_GESTURE_TWO_FINGER_TAP:
      case ET_GESTURE_PINCH_BEGIN:
      case ET_GESTURE_PINCH_END:
      case ET_GESTURE_PINCH_UPDATE:
      case ET_GESTURE_LONG_PRESS:
      case ET_GESTURE_LONG_TAP:
      case ET_GESTURE_SWIPE:
      case ET_GESTURE_SHOW_PRESS:
        // When adding a gesture event which is paired with an event which
        // occurs earlier, add the event to |IsEndingEvent|.
        return true;

      case ET_SCROLL_FLING_CANCEL:
      case ET_SCROLL_FLING_START:
        // These can be ScrollEvents too. EF_FROM_TOUCH determines if they're
        // Gesture or Scroll events.
        return (flags_ & EF_FROM_TOUCH) == EF_FROM_TOUCH;

      default:
        break;
    }
    return false;
  }

  // An ending event is paired with the event which started it. Setting capture
  // should not prevent ending events from getting to their initial target.
  bool IsEndingEvent() const {
    switch(type_) {
      case ui::ET_TOUCH_CANCELLED:
      case ui::ET_GESTURE_TAP_CANCEL:
      case ui::ET_GESTURE_END:
      case ui::ET_GESTURE_SCROLL_END:
      case ui::ET_GESTURE_PINCH_END:
        return true;
      default:
        return false;
    }
  }

  bool IsScrollEvent() const {
    // Flings can be GestureEvents too. EF_FROM_TOUCH determins if they're
    // Gesture or Scroll events.
    return type_ == ET_SCROLL ||
           ((type_ == ET_SCROLL_FLING_START ||
           type_ == ET_SCROLL_FLING_CANCEL) &&
           !(flags() & EF_FROM_TOUCH));
  }

  bool IsScrollGestureEvent() const {
    return type_ == ET_GESTURE_SCROLL_BEGIN ||
           type_ == ET_GESTURE_SCROLL_UPDATE ||
           type_ == ET_GESTURE_SCROLL_END;
  }

  bool IsFlingScrollEvent() const {
    return type_ == ET_SCROLL_FLING_CANCEL ||
           type_ == ET_SCROLL_FLING_START;
  }

  bool IsMouseWheelEvent() const {
    return type_ == ET_MOUSEWHEEL;
  }

  bool IsLocatedEvent() const {
    return IsMouseEvent() || IsScrollEvent() || IsTouchEvent() ||
           IsGestureEvent() || IsPointerEvent();
  }

  // Convenience methods to cast |this| to a CancelModeEvent.
  // IsCancelModeEvent() must be true as a precondition to calling these
  // methods.
  CancelModeEvent* AsCancelModeEvent();
  const CancelModeEvent* AsCancelModeEvent() const;

  // Convenience methods to cast |this| to a GestureEvent. IsGestureEvent()
  // must be true as a precondition to calling these methods.
  GestureEvent* AsGestureEvent();
  const GestureEvent* AsGestureEvent() const;

  // Convenience methods to cast |this| to a KeyEvent. IsKeyEvent()
  // must be true as a precondition to calling these methods.
  KeyEvent* AsKeyEvent();
  const KeyEvent* AsKeyEvent() const;

  // Convenience methods to cast |this| to a LocatedEvent. IsLocatedEvent()
  // must be true as a precondition to calling these methods.
  LocatedEvent* AsLocatedEvent();
  const LocatedEvent* AsLocatedEvent() const;

  // Convenience methods to cast |this| to a MouseEvent. IsMouseEvent()
  // must be true as a precondition to calling these methods.
  MouseEvent* AsMouseEvent();
  const MouseEvent* AsMouseEvent() const;

  // Convenience methods to cast |this| to a MouseWheelEvent.
  // IsMouseWheelEvent() must be true as a precondition to calling these
  // methods.
  MouseWheelEvent* AsMouseWheelEvent();
  const MouseWheelEvent* AsMouseWheelEvent() const;

  // Convenience methods to cast |this| to a PointerEvent. IsPointerEvent()
  // must be true as a precondition to calling these methods.
  PointerEvent* AsPointerEvent();
  const PointerEvent* AsPointerEvent() const;

  // Convenience methods to cast |this| to a ScrollEvent. IsScrollEvent()
  // must be true as a precondition to calling these methods.
  ScrollEvent* AsScrollEvent();
  const ScrollEvent* AsScrollEvent() const;

  // Convenience methods to cast |this| to a TouchEvent. IsTouchEvent()
  // must be true as a precondition to calling these methods.
  TouchEvent* AsTouchEvent();
  const TouchEvent* AsTouchEvent() const;

  // Returns true if the event has a valid |native_event_|.
  bool HasNativeEvent() const;

  // Immediately stops the propagation of the event. This must be called only
  // from an EventHandler during an event-dispatch. Any event handler that may
  // be in the list will not receive the event after this is called.
  // Note that StopPropagation() can be called only for cancelable events.
  void StopPropagation();
  bool stopped_propagation() const { return !!(result_ & ER_CONSUMED); }

  // Marks the event as having been handled. A handled event does not reach the
  // next event phase. For example, if an event is handled during the pre-target
  // phase, then the event is dispatched to all pre-target handlers, but not to
  // the target or post-target handlers.
  // Note that SetHandled() can be called only for cancelable events.
  void SetHandled();
  bool handled() const { return result_ != ER_UNHANDLED; }

 protected:
  Event(EventType type, base::TimeTicks time_stamp, int flags);
  Event(const base::NativeEvent& native_event, EventType type, int flags);
  Event(const Event& copy);
  void SetType(EventType type);
  void set_cancelable(bool cancelable) { cancelable_ = cancelable; }

  void set_time_stamp(base::TimeTicks time_stamp) {
    time_stamp_ = time_stamp;
  }

 private:
  friend class EventTestApi;

  EventType type_;
  base::TimeTicks time_stamp_;
  LatencyInfo latency_;
  int flags_;
  base::NativeEvent native_event_;
  bool delete_native_event_;
  bool cancelable_;
  EventTarget* target_;
  EventPhase phase_;
  EventResult result_;

  // The device id the event came from, or ED_UNKNOWN_DEVICE if the information
  // is not available.
  int source_device_id_;
};

class EVENTS_EXPORT CancelModeEvent : public Event {
 public:
  CancelModeEvent();
  ~CancelModeEvent() override;
};

class EVENTS_EXPORT LocatedEvent : public Event {
 public:
  // Convenience function that casts |event| to a LocatedEvent if it is one,
  // otherwise returns null.
  static const ui::LocatedEvent* FromIfValid(const ui::Event* event) {
    return event && event->IsLocatedEvent() ? event->AsLocatedEvent() : nullptr;
  }

  ~LocatedEvent() override;

  float x() const { return location_.x(); }
  float y() const { return location_.y(); }
  void set_location(const gfx::Point& location) {
    location_ = gfx::PointF(location);
  }
  void set_location_f(const gfx::PointF& location) { location_ = location; }
  gfx::Point location() const { return gfx::ToFlooredPoint(location_); }
  const gfx::PointF& location_f() const { return location_; }
  void set_root_location(const gfx::Point& root_location) {
    root_location_ = gfx::PointF(root_location);
  }
  void set_root_location_f(const gfx::PointF& root_location) {
    root_location_ = root_location;
  }
  gfx::Point root_location() const {
    return gfx::ToFlooredPoint(root_location_);
  }
  const gfx::PointF& root_location_f() const {
    return root_location_;
  }

  // Transform the locations using |inverted_root_transform|.
  // This is applied to both |location_| and |root_location_|.
  virtual void UpdateForRootTransform(
      const gfx::Transform& inverted_root_transform);

  template <class T> void ConvertLocationToTarget(T* source, T* target) {
    if (!target || target == source)
      return;
    gfx::Point offset = gfx::ToFlooredPoint(location_);
    T::ConvertPointToTarget(source, target, &offset);
    gfx::Vector2d diff = gfx::ToFlooredPoint(location_) - offset;
    location_= location_ - diff;
  }

 protected:
  friend class LocatedEventTestApi;

  explicit LocatedEvent(const base::NativeEvent& native_event);

  // Create a new LocatedEvent which is identical to the provided model.
  // If source / target windows are provided, the model location will be
  // converted from |source| coordinate system to |target| coordinate system.
  template <class T>
  LocatedEvent(const LocatedEvent& model, T* source, T* target)
      : Event(model),
        location_(model.location_),
        root_location_(model.root_location_) {
    ConvertLocationToTarget(source, target);
  }

  // Used for synthetic events in testing.
  LocatedEvent(EventType type,
               const gfx::PointF& location,
               const gfx::PointF& root_location,
               base::TimeTicks time_stamp,
               int flags);

  gfx::PointF location_;

  // |location_| multiplied by an optional transformation matrix for
  // rotations, animations and skews.
  gfx::PointF root_location_;
};

// Structure for handling common fields between touch and mouse to support
// PointerEvents API.
struct EVENTS_EXPORT PointerDetails {
 public:
  PointerDetails();
  explicit PointerDetails(EventPointerType pointer_type,
                          PointerId pointer_id = kUnknownPointerId);
  PointerDetails(EventPointerType pointer_type,
                 PointerId pointer_id,
                 float radius_x,
                 float radius_y,
                 float force,
                 float tilt_x = 0.0f,
                 float tilt_y = 0.0f,
                 float tangential_pressure = 0.0f,
                 int twist = 0);
  PointerDetails(EventPointerType pointer_type,
                 const gfx::Vector2d& pointer_offset,
                 PointerId pointer_id = kUnknownPointerId);
  PointerDetails(const PointerDetails& other);

  bool operator==(const PointerDetails& other) const {
    return pointer_type == other.pointer_type && radius_x == other.radius_x &&
           radius_y == other.radius_y &&
           (force == other.force ||
            (std::isnan(force) && std::isnan(other.force))) &&
           tilt_x == other.tilt_x && tilt_y == other.tilt_y &&
           tangential_pressure == other.tangential_pressure &&
           twist == other.twist && id == other.id && offset == other.offset;
  }

  // A value for pointer id which means it needs to be initialized for all
  // pointer types.
  static const PointerId kUnknownPointerId;

  // The type of pointer device.
  EventPointerType pointer_type = EventPointerType::POINTER_TYPE_UNKNOWN;

  // Radius of the X (major) axis of the touch ellipse. 0.0 if unknown.
  float radius_x = 0.0;

  // Radius of the Y (minor) axis of the touch ellipse. 0.0 if unknown.
  float radius_y = 0.0;

  // Force (pressure) of the touch. Normalized to be [0, 1] except NaN means
  // pressure is not supported by the input device.
  float force = 0.0;

  // Tilt of a pen/stylus from surface normal as plane angle in degrees, values
  // lie in [-90,90]. A positive tilt_x is to the right and a positive tilt_y
  // is towards the user. 0.0 if unknown.
  float tilt_x = 0.0;
  float tilt_y = 0.0;

  // The normalized tangential pressure (or barrel pressure), typically set by
  // an additional control of the stylus, which has a range of [-1,1], where 0
  // is the neutral position of the control. Always 0 if the device does not
  // support it.
  float tangential_pressure = 0.0;

  // The clockwise rotation of a pen stylus around its own major axis, in
  // degrees in the range [0,359]. Always 0 if the device does not support it.
  int twist = 0;

  // An identifier that uniquely identifies a pointer during its lifetime.
  PointerId id = 0;

  // Only used by mouse wheel events. The amount to scroll. This is in multiples
  // of kWheelDelta.
  // Note: offset_.x() > 0/offset_.y() > 0 means scroll left/up.
  gfx::Vector2d offset;
};

class EVENTS_EXPORT MouseEvent : public LocatedEvent {
 public:
  static const PointerId kMousePointerId;

  explicit MouseEvent(const base::NativeEvent& native_event);

  // |pointer_event.IsMousePointerEvent()| must be true.
  // Note: If |pointer_event| is a mouse wheel pointer event, use the
  // MouseWheelEvent version of this function to convert to a MouseWheelEvent
  // instead.
  explicit MouseEvent(const PointerEvent& pointer_event);

  // Create a new MouseEvent based on the provided model.
  // Uses the provided |type| and |flags| for the new event.
  // If source / target windows are provided, the model location will be
  // converted from |source| coordinate system to |target| coordinate system.
  template <class T>
  MouseEvent(const MouseEvent& model, T* source, T* target)
      : LocatedEvent(model, source, target),
        changed_button_flags_(model.changed_button_flags_),
        pointer_details_(model.pointer_details_) {}

  template <class T>
  MouseEvent(const MouseEvent& model,
             T* source,
             T* target,
             EventType type,
             int flags)
      : LocatedEvent(model, source, target),
        changed_button_flags_(model.changed_button_flags_),
        pointer_details_(model.pointer_details_) {
    SetType(type);
    set_flags(flags);
  }

  // Used for synthetic events in testing, gesture recognizer and Ozone
  // Note: Use the ctor for MouseWheelEvent if type is ET_MOUSEWHEEL.
  MouseEvent(EventType type,
             const gfx::Point& location,
             const gfx::Point& root_location,
             base::TimeTicks time_stamp,
             int flags,
             int changed_button_flags,
             const PointerDetails& pointer_details =
                 PointerDetails(EventPointerType::POINTER_TYPE_MOUSE,
                                kMousePointerId));

  // Conveniences to quickly test what button is down
  bool IsOnlyLeftMouseButton() const {
    return button_flags() == EF_LEFT_MOUSE_BUTTON;
  }

  bool IsLeftMouseButton() const {
    return (flags() & EF_LEFT_MOUSE_BUTTON) != 0;
  }

  bool IsOnlyMiddleMouseButton() const {
    return button_flags() == EF_MIDDLE_MOUSE_BUTTON;
  }

  bool IsMiddleMouseButton() const {
    return (flags() & EF_MIDDLE_MOUSE_BUTTON) != 0;
  }

  bool IsOnlyRightMouseButton() const {
    return button_flags() == EF_RIGHT_MOUSE_BUTTON;
  }

  bool IsRightMouseButton() const {
    return (flags() & EF_RIGHT_MOUSE_BUTTON) != 0;
  }

  bool IsAnyButton() const {
    return button_flags() != 0;
  }

  // Returns the flags for the mouse buttons.
  int button_flags() const {
    return flags() & (EF_LEFT_MOUSE_BUTTON | EF_MIDDLE_MOUSE_BUTTON |
                      EF_RIGHT_MOUSE_BUTTON | EF_BACK_MOUSE_BUTTON |
                      EF_FORWARD_MOUSE_BUTTON);
  }

  // Compares two mouse down events and returns true if the second one should
  // be considered a repeat of the first.
  static bool IsRepeatedClickEvent(
      const MouseEvent& event1,
      const MouseEvent& event2);

  // Get the click count. Can be 1, 2 or 3 for mousedown messages, 0 otherwise.
  int GetClickCount() const;

  // Set the click count for a mousedown message. Can be 1, 2 or 3.
  void SetClickCount(int click_count);

  // Identifies the button that changed. During a press this corresponds to the
  // button that was pressed and during a release this corresponds to the button
  // that was released.
  // NOTE: during a press and release flags() contains the complete set of
  // flags. Use this to determine the button that was pressed or released.
  int changed_button_flags() const { return changed_button_flags_; }

  // Updates the button that changed.
  void set_changed_button_flags(int flags) { changed_button_flags_ = flags; }

  // Event details common to MouseEvent and TouchEvent.
  const PointerDetails& pointer_details() const { return pointer_details_; }

 private:
  FRIEND_TEST_ALL_PREFIXES(EventTest, DoubleClickRequiresRelease);
  FRIEND_TEST_ALL_PREFIXES(EventTest, SingleClickRightLeft);

  // Returns the repeat count based on the previous mouse click, if it is
  // recent enough and within a small enough distance.
  static int GetRepeatCount(const MouseEvent& click_event);

  // Resets the last_click_event_ for unit tests.
  static void ResetLastClickForTest();

  // See description above getter for details.
  int changed_button_flags_;

  static MouseEvent* last_click_event_;

  // We can create a MouseEvent for a native event more than once. We set this
  // to true when the next event either has a different timestamp or we see a
  // release signalling that the press (click) event was completed.
  static bool last_click_complete_;

  // Structure for holding pointer details for implementing PointerEvents API.
  PointerDetails pointer_details_;
};

class ScrollEvent;

class EVENTS_EXPORT MouseWheelEvent : public MouseEvent {
 public:
  // See |offset| for details.
  static const int kWheelDelta;

  explicit MouseWheelEvent(const base::NativeEvent& native_event);
  explicit MouseWheelEvent(const ScrollEvent& scroll_event);
  explicit MouseWheelEvent(const PointerEvent& pointer_event);
  MouseWheelEvent(const MouseEvent& mouse_event, int x_offset, int y_offset);
  MouseWheelEvent(const MouseWheelEvent& mouse_wheel_event);

  template <class T>
  MouseWheelEvent(const MouseWheelEvent& model,
                  T* source,
                  T* target)
      : MouseEvent(model, source, target, model.type(), model.flags()),
        offset_(model.x_offset(), model.y_offset()) {
  }

  // Used for synthetic events in testing and by the gesture recognizer.
  MouseWheelEvent(const gfx::Vector2d& offset,
                  const gfx::Point& location,
                  const gfx::Point& root_location,
                  base::TimeTicks time_stamp,
                  int flags,
                  int changed_button_flags);

  // The amount to scroll. This is in multiples of kWheelDelta.
  // Note: x_offset() > 0/y_offset() > 0 means scroll left/up.
  int x_offset() const { return offset_.x(); }
  int y_offset() const { return offset_.y(); }
  const gfx::Vector2d& offset() const { return offset_; }

 private:
  gfx::Vector2d offset_;
};

class EVENTS_EXPORT TouchEvent : public LocatedEvent {
 public:
  explicit TouchEvent(const base::NativeEvent& native_event);

  // |pointer_event.IsTouchPointerEvent()| must be true.
  explicit TouchEvent(const PointerEvent& pointer_event);

  // Create a new TouchEvent which is identical to the provided model.
  // If source / target windows are provided, the model location will be
  // converted from |source| coordinate system to |target| coordinate system.
  template <class T>
  TouchEvent(const TouchEvent& model, T* source, T* target)
      : LocatedEvent(model, source, target),
        unique_event_id_(model.unique_event_id_),
        rotation_angle_(model.rotation_angle_),
        may_cause_scrolling_(model.may_cause_scrolling_),
        should_remove_native_touch_id_mapping_(false),
        pointer_details_(model.pointer_details_) {}

  TouchEvent(EventType type,
             const gfx::Point& location,
             base::TimeTicks time_stamp,
             const PointerDetails& pointer_details,
             int flags = 0,
             float angle = 0.0f);

  TouchEvent(const TouchEvent& copy);

  ~TouchEvent() override;

  // A unique identifier for this event.
  uint32_t unique_event_id() const { return unique_event_id_; }

  float rotation_angle() const { return rotation_angle_; }

  void set_may_cause_scrolling(bool causes) { may_cause_scrolling_ = causes; }
  bool may_cause_scrolling() const { return may_cause_scrolling_; }

  void set_should_remove_native_touch_id_mapping(
      bool should_remove_native_touch_id_mapping) {
    should_remove_native_touch_id_mapping_ =
        should_remove_native_touch_id_mapping;
  }

  // Overridden from LocatedEvent.
  void UpdateForRootTransform(
      const gfx::Transform& inverted_root_transform) override;

  // Marks the event as not participating in synchronous gesture recognition.
  void DisableSynchronousHandling();
  bool synchronous_handling_disabled() const {
    return !!(result() & ER_DISABLE_SYNC_HANDLING);
  }

  // Event details common to MouseEvent and TouchEvent.
  const PointerDetails& pointer_details() const { return pointer_details_; }
  void set_pointer_details(const PointerDetails& pointer_details);

 private:
  // Adjusts rotation_angle_ to within the acceptable range.
  void FixRotationAngle();

  // A unique identifier for the touch event.
  uint32_t unique_event_id_;

  // TODO(726824): Remove rotation_angle_ from ui::TouchEvent, just use twist
  // in PointerDetails.
  // Clockwise angle (in degrees) of the major axis from the X axis. Must be
  // less than 180 and non-negative.
  float rotation_angle_;

  // Whether the (unhandled) touch event will produce a scroll event (e.g., a
  // touchmove that exceeds the platform slop region, or a touchend that
  // causes a fling). Defaults to false.
  bool may_cause_scrolling_;

  // True if this event should remove the mapping between the native
  // event id and the touch_id_. This should only be the case for
  // release and cancel events where the associated touch press event
  // created a mapping between the native id and the touch_id_.
  bool should_remove_native_touch_id_mapping_;

  // Structure for holding pointer details for implementing PointerEvents API.
  PointerDetails pointer_details_;
};

class EVENTS_EXPORT PointerEvent : public LocatedEvent {
 public:
  // Returns true if a PointerEvent can be constructed from |event|. Currently,
  // only mouse and touch events can be converted to pointer events.
  static bool CanConvertFrom(const Event& event);

  PointerEvent(const PointerEvent& pointer_event);
  explicit PointerEvent(const MouseEvent& mouse_event);
  explicit PointerEvent(const TouchEvent& touch_event);

  PointerEvent(EventType type,
               const gfx::Point& location,
               const gfx::Point& root_location,
               int flags,
               int changed_button_flags,
               const PointerDetails& pointer_details,
               base::TimeTicks time_stamp);

  int changed_button_flags() const { return changed_button_flags_; }
  void set_changed_button_flags(int flags) { changed_button_flags_ = flags; }
  const PointerDetails& pointer_details() const { return details_; }

 private:
  int changed_button_flags_;
  PointerDetails details_;
};

// A KeyEvent is really two distinct classes, melded together due to the
// DOM legacy of Windows key events: a keystroke event (is_char_ == false),
// or a character event (is_char_ == true).
//
// For a keystroke event,
// -- |bool is_char_| is false.
// -- |EventType Event::type()| can be ET_KEY_PRESSED or ET_KEY_RELEASED.
// -- |DomCode code_| and |int Event::flags()| represent the physical key event.
//    - code_ is a platform-independent representation of the physical key,
//      based on DOM UI Events KeyboardEvent |code| values. It does not
//      vary depending on key layout.
//      http://www.w3.org/TR/DOM-Level-3-Events-code/
//    - Event::flags() provides the active modifiers for the physical key
//      press. Its value reflects the state after the event; that is, for
//      a modifier key, a press includes the corresponding flag and a release
//      does not.
// -- |DomKey key_| provides the meaning (character or action) of the key
//    event, in the context of the active layout and modifiers. It corresponds
//    to DOM UI Events KeyboardEvent |key| values.
//    http://www.w3.org/TR/DOM-Level-3-Events-key/
// -- |KeyboardCode key_code_| supports the legacy web event |keyCode| field,
//    and its VKEY_ values are chosen to match Windows/IE for compatibility.
//    For printable characters, this may or may not be a layout-mapped value,
//    imitating MS Windows: if the mapped key generates a character that has
//    an associated VKEY_ code, then key_code_ is that code; if not, then
//    key_code_ is the unmapped VKEY_ code. For example, US, Greek, Cyrillic,
//    Japanese, etc. all use VKEY_Q for the key beside Tab, while French uses
//    VKEY_A. The stored key_code_ is non-located (e.g. VKEY_SHIFT rather than
//    VKEY_LSHIFT, VKEY_1 rather than VKEY_NUMPAD1).
//
// For a character event,
// -- |bool is_char_| is true.
// -- |EventType Event::type()| is ET_KEY_PRESSED.
// -- |DomCode code_| is DomCode::NONE.
// -- |DomKey key_| is a UTF-16 code point.
// -- |KeyboardCode key_code_| is conflated with the character-valued key_
//    by some code, because both arrive in the wParam field of a Windows event.
//
class EVENTS_EXPORT KeyEvent : public Event {
 public:
  using Properties = std::unordered_map<std::string, std::vector<uint8_t>>;

  // Create a KeyEvent from a NativeEvent. For Windows this native event can
  // be either a keystroke message (WM_KEYUP/WM_KEYDOWN) or a character message
  // (WM_CHAR). Other systems have only keystroke events.
  explicit KeyEvent(const base::NativeEvent& native_event);

  // Create a KeyEvent from a NativeEvent but with mocked flags.
  // This method is necessary for Windows since MSG does not contain all flags.
  KeyEvent(const base::NativeEvent& native_event, int event_flags);

  // Create a keystroke event from a legacy KeyboardCode.
  // This should not be used in new code.
  KeyEvent(EventType type,
           KeyboardCode key_code,
           int flags,
           base::TimeTicks time_stamp = base::TimeTicks());

  // Create a fully defined keystroke event.
  KeyEvent(EventType type,
           KeyboardCode key_code,
           DomCode code,
           int flags,
           DomKey key,
           base::TimeTicks time_stamp);

  // Create a character event.
  KeyEvent(base::char16 character,
           KeyboardCode key_code,
           int flags,
           base::TimeTicks time_stamp = base::TimeTicks());

  // Used for synthetic events with code of DOM KeyboardEvent (e.g. 'KeyA')
  // See also: ui/events/keycodes/dom/dom_values.txt
  KeyEvent(EventType type,
           KeyboardCode key_code,
           DomCode code,
           int flags);

  KeyEvent(const KeyEvent& rhs);

  KeyEvent& operator=(const KeyEvent& rhs);

  ~KeyEvent() override;

  // This bypasses the normal mapping from keystroke events to characters,
  // which allows an I18N virtual keyboard to fabricate a keyboard event that
  // does not have a corresponding KeyboardCode (example: U+00E1 Latin small
  // letter A with acute, U+0410 Cyrillic capital letter A).
  void set_character(base::char16 character) {
    key_ = DomKey::FromCharacter(character);
  }

  // Gets the character generated by this key event. It only supports Unicode
  // BMP characters.
  base::char16 GetCharacter() const;

  // If this is a keystroke event with key_code_ VKEY_RETURN, returns '\r';
  // otherwise returns the same as GetCharacter().
  base::char16 GetUnmodifiedText() const;

  // If the Control key is down in the event, returns a layout-independent
  // character (corresponding to US layout); otherwise returns the same
  // as GetUnmodifiedText().
  base::char16 GetText() const;

  // True if this is a character event, false if this is a keystroke event.
  bool is_char() const { return is_char_; }

  bool is_repeat() const { return (flags() & EF_IS_REPEAT) != 0; }

  // Gets the associated (Windows-based) KeyboardCode for this key event.
  // Historically, this has also been used to obtain the character associated
  // with a character event, because both use the Window message 'wParam' field.
  // This should be avoided; if necessary for backwards compatibility, use
  // GetConflatedWindowsKeyCode().
  KeyboardCode key_code() const { return key_code_; }

  // This is only intended to be used externally by classes that are modifying
  // events in an EventRewriter.
  void set_key_code(KeyboardCode key_code) { key_code_ = key_code; }

  // Returns the same value as key_code(), except that located codes are
  // returned in place of non-located ones (e.g. VKEY_LSHIFT or VKEY_RSHIFT
  // instead of VKEY_SHIFT). This is a hybrid of semantic and physical
  // for legacy DOM reasons.
  KeyboardCode GetLocatedWindowsKeyboardCode() const;

  // For a keystroke event, returns the same value as key_code().
  // For a character event, returns the same value as GetCharacter().
  // This exists for backwards compatibility with Windows key events.
  uint16_t GetConflatedWindowsKeyCode() const;

  // Returns true for [Alt]+<num-pad digit> Unicode alt key codes used by Win.
  // TODO(msw): Additional work may be needed for analogues on other platforms.
  bool IsUnicodeKeyCode() const;

  // Returns the DOM .code (physical key identifier) for a keystroke event.
  DomCode code() const { return code_; };
  std::string GetCodeString() const;

  // Returns the DOM .key (layout meaning) for a keystroke event.
  DomKey GetDomKey() const;

  // Normalizes flags_ so that it describes the state after the event.
  // (Native X11 event flags describe the state before the event.)
  void NormalizeFlags();

  // Sets the properties associated with this KeyEvent.
  void SetProperties(const Properties& properties);

  // Returns the properties associated with this event, which may be null.
  // The properties are meant to provide a way to associate arbitrary key/value
  // pairs with KeyEvents and not used by KeyEvent.
  const Properties* properties() const { return properties_.get(); }

 protected:
  friend class KeyEventTestApi;

  // This allows a subclass TranslatedKeyEvent to be a non character event.
  void set_is_char(bool is_char) { is_char_ = is_char; }

 private:
  // Determine key_ on a keystroke event from code_ and flags().
  void ApplyLayout() const;

  static bool IsRepeated(const KeyEvent& event);

  KeyboardCode key_code_;

  // DOM KeyboardEvent |code| (e.g. DomCode::US_A, DomCode::SPACE).
  // http://www.w3.org/TR/DOM-Level-3-Events-code/
  //
  // This value represents the physical position in the keyboard and can be
  // converted from / to keyboard scan code like XKB.
  DomCode code_;

  // True if this is a character event, false if this is a keystroke event.
  bool is_char_ = false;

  // TODO(kpschoedel): refactor so that key_ is not mutable.
  // This requires defining the KeyEvent completely at construction rather
  // than lazily under GetCharacter(), which likely also means removing
  // the two 'incomplete' constructors. crbug.com/444045
  //
  // DOM KeyboardEvent |key|
  // http://www.w3.org/TR/DOM-Level-3-Events-key/
  //
  // This value represents the meaning of a key, which is either a Unicode
  // character, or a named DomKey:: value.
  // This is not necessarily initialized when the event is constructed;
  // it may be set only if and when GetCharacter() or GetDomKey() is called.
  mutable DomKey key_ = DomKey::NONE;

  std::unique_ptr<Properties> properties_;

  static KeyEvent* last_key_event_;
#if defined(USE_X11)
  static KeyEvent* last_ibus_key_event_;
#endif
};

class EVENTS_EXPORT ScrollEvent : public MouseEvent {
 public:
  explicit ScrollEvent(const base::NativeEvent& native_event);
  template <class T>
  ScrollEvent(const ScrollEvent& model,
              T* source,
              T* target)
      : MouseEvent(model, source, target),
        x_offset_(model.x_offset_),
        y_offset_(model.y_offset_),
        x_offset_ordinal_(model.x_offset_ordinal_),
        y_offset_ordinal_(model.y_offset_ordinal_),
        finger_count_(model.finger_count_){
  }

  // Used for tests.
  ScrollEvent(EventType type,
              const gfx::Point& location,
              base::TimeTicks time_stamp,
              int flags,
              float x_offset,
              float y_offset,
              float x_offset_ordinal,
              float y_offset_ordinal,
              int finger_count,
              EventMomentumPhase momentum_phase = EventMomentumPhase::NONE);

  // Scale the scroll event's offset value.
  // This is useful in the multi-monitor setup where it needs to be scaled
  // to provide a consistent user experience.
  void Scale(const float factor);

  float x_offset() const { return x_offset_; }
  float y_offset() const { return y_offset_; }
  float x_offset_ordinal() const { return x_offset_ordinal_; }
  float y_offset_ordinal() const { return y_offset_ordinal_; }
  int finger_count() const { return finger_count_; }
  EventMomentumPhase momentum_phase() const { return momentum_phase_; }

 private:
  // Potential accelerated offsets.
  float x_offset_;
  float y_offset_;
  // Unaccelerated offsets.
  float x_offset_ordinal_;
  float y_offset_ordinal_;
  // Number of fingers on the pad.
  int finger_count_;

  // For non-fling events, provides momentum information (e.g. for the case
  // where the device provides continuous event updates during a fling).
  EventMomentumPhase momentum_phase_;
};

class EVENTS_EXPORT GestureEvent : public LocatedEvent {
 public:
    // The constructor takes a default unique_touch_id of zero to support many
    // (80+) existing tests that doesn't care about this id.
  GestureEvent(float x,
               float y,
               int flags,
               base::TimeTicks time_stamp,
               const GestureEventDetails& details,
               uint32_t unique_touch_event_id = 0);

  // Create a new GestureEvent which is identical to the provided model.
  // If source / target windows are provided, the model location will be
  // converted from |source| coordinate system to |target| coordinate system.
  template <typename T>
  GestureEvent(const GestureEvent& model, T* source, T* target)
      : LocatedEvent(model, source, target),
        details_(model.details_) {
  }

  ~GestureEvent() override;

  const GestureEventDetails& details() const { return details_; }

  uint32_t unique_touch_event_id() const {
    return unique_touch_event_id_;
  }

 private:
  GestureEventDetails details_;

  // The unique id of the touch event that caused the gesture event to be
  // dispatched. This field gets a non-zero value only for gestures that are
  // released through TouchDispositionGestureFilter::SendGesture. The gesture
  // events that aren't fired directly in response to processing a touch-event
  // (e.g. timer fired ones), this id is zero. See crbug.com/618738.
  uint32_t unique_touch_event_id_;
};

}  // namespace ui

#endif  // UI_EVENTS_EVENT_H_
