// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/animation.h"

#include <cmath>

#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "cc/animation/animation_curve.h"

namespace {

// This should match the RunState enum.
static const char* const s_runStateNames[] = {"WAITING_FOR_TARGET_AVAILABILITY",
                                              "WAITING_FOR_DELETION",
                                              "STARTING",
                                              "RUNNING",
                                              "PAUSED",
                                              "FINISHED",
                                              "ABORTED",
                                              "ABORTED_BUT_NEEDS_COMPLETION"};

static_assert(static_cast<int>(cc::Animation::LAST_RUN_STATE) + 1 ==
                  arraysize(s_runStateNames),
              "RunStateEnumSize should equal the number of elements in "
              "s_runStateNames");

static const char* const s_curveTypeNames[] = {
    "COLOR",         "FLOAT", "TRANSFORM", "FILTER",
    "SCROLL_OFFSET", "SIZE",  "BOOLEAN"};

static_assert(static_cast<int>(cc::AnimationCurve::LAST_CURVE_TYPE) + 1 ==
                  arraysize(s_curveTypeNames),
              "CurveType enum should equal the number of elements in "
              "s_runStateNames");

}  // namespace

namespace cc {

std::unique_ptr<Animation> Animation::Create(
    std::unique_ptr<AnimationCurve> curve,
    int animation_id,
    int group_id,
    int target_property_id) {
  return base::WrapUnique(new Animation(std::move(curve), animation_id,
                                        group_id, target_property_id));
}

Animation::Animation(std::unique_ptr<AnimationCurve> curve,
                     int animation_id,
                     int group_id,
                     int target_property_id)
    : curve_(std::move(curve)),
      id_(animation_id),
      group_(group_id),
      target_property_id_(target_property_id),
      run_state_(WAITING_FOR_TARGET_AVAILABILITY),
      iterations_(1),
      iteration_start_(0),
      direction_(Direction::NORMAL),
      playback_rate_(1),
      fill_mode_(FillMode::BOTH),
      needs_synchronized_start_time_(false),
      received_finished_event_(false),
      suspended_(false),
      is_controlling_instance_(false),
      is_impl_only_(false),
      affects_active_elements_(true),
      affects_pending_elements_(true) {}

Animation::~Animation() {
  if (run_state_ == RUNNING || run_state_ == PAUSED)
    SetRunState(ABORTED, base::TimeTicks());
}

void Animation::SetRunState(RunState run_state,
                            base::TimeTicks monotonic_time) {
  if (suspended_)
    return;

  char name_buffer[256];
  base::snprintf(name_buffer, sizeof(name_buffer), "%s-%d-%d",
                 s_curveTypeNames[curve_->Type()], target_property_id_, group_);

  bool is_waiting_to_start =
      run_state_ == WAITING_FOR_TARGET_AVAILABILITY || run_state_ == STARTING;

  if (is_controlling_instance_ && is_waiting_to_start && run_state == RUNNING) {
    TRACE_EVENT_ASYNC_BEGIN1(
        "cc", "Animation", this, "Name", TRACE_STR_COPY(name_buffer));
  }

  bool was_finished = is_finished();

  const char* old_run_state_name = s_runStateNames[run_state_];

  if (run_state == RUNNING && run_state_ == PAUSED)
    total_paused_time_ += (monotonic_time - pause_time_);
  else if (run_state == PAUSED)
    pause_time_ = monotonic_time;
  run_state_ = run_state;

  const char* new_run_state_name = s_runStateNames[run_state];

  if (is_controlling_instance_ && !was_finished && is_finished())
    TRACE_EVENT_ASYNC_END0("cc", "Animation", this);

  char state_buffer[256];
  base::snprintf(state_buffer,
                 sizeof(state_buffer),
                 "%s->%s",
                 old_run_state_name,
                 new_run_state_name);

  TRACE_EVENT_INSTANT2(
      "cc", "ElementAnimations::SetRunState", TRACE_EVENT_SCOPE_THREAD, "Name",
      TRACE_STR_COPY(name_buffer), "State", TRACE_STR_COPY(state_buffer));
}

void Animation::Suspend(base::TimeTicks monotonic_time) {
  SetRunState(PAUSED, monotonic_time);
  suspended_ = true;
}

void Animation::Resume(base::TimeTicks monotonic_time) {
  suspended_ = false;
  SetRunState(RUNNING, monotonic_time);
}

bool Animation::IsFinishedAt(base::TimeTicks monotonic_time) const {
  if (is_finished())
    return true;

  if (needs_synchronized_start_time_)
    return false;

  if (playback_rate_ == 0)
    return false;

  return run_state_ == RUNNING && iterations_ >= 0 &&
         (curve_->Duration() * (iterations_ / std::abs(playback_rate_))) <=
             (monotonic_time + time_offset_ - start_time_ - total_paused_time_);
}

bool Animation::InEffect(base::TimeTicks monotonic_time) const {
  return ConvertToActiveTime(monotonic_time) >= base::TimeDelta() ||
         (fill_mode_ == FillMode::BOTH || fill_mode_ == FillMode::BACKWARDS);
}

base::TimeDelta Animation::ConvertToActiveTime(
    base::TimeTicks monotonic_time) const {
  // If we're just starting or we're waiting on receiving a start time,
  // time is 'stuck' at the initial state.
  if ((run_state_ == STARTING && !has_set_start_time()) ||
      needs_synchronized_start_time()) {
    return time_offset_;
  }

  // Compute active time. If we're paused, time is 'stuck' at the pause time.
  base::TimeTicks active_time =
      (run_state_ == PAUSED) ? pause_time_ : (monotonic_time + time_offset_);

  // Returned time should always be relative to the start time and should
  // subtract all time spent paused.
  return active_time - start_time_ - total_paused_time_;
}

base::TimeDelta Animation::TrimTimeToCurrentIteration(
    base::TimeTicks monotonic_time) const {
  // Check for valid parameters
  DCHECK(playback_rate_);
  DCHECK_GE(iteration_start_, 0);

  base::TimeDelta active_time = ConvertToActiveTime(monotonic_time);
  base::TimeDelta start_offset = curve_->Duration() * iteration_start_;

  // Return start offset if we are before the start of the animation
  if (active_time < base::TimeDelta())
    return start_offset;
  // Always return zero if we have no iterations.
  if (!iterations_)
    return base::TimeDelta();

  // Don't attempt to trim if we have no duration.
  if (curve_->Duration() <= base::TimeDelta())
    return base::TimeDelta();

  base::TimeDelta repeated_duration = curve_->Duration() * iterations_;
  base::TimeDelta active_duration =
      repeated_duration / std::abs(playback_rate_);

  // Check if we are past active duration
  if (iterations_ > 0 && active_time >= active_duration)
    active_time = active_duration;

  // Calculate the scaled active time
  base::TimeDelta scaled_active_time;
  if (playback_rate_ < 0) {
    scaled_active_time =
        ((active_time - active_duration) * playback_rate_) + start_offset;
  } else {
    scaled_active_time = (active_time * playback_rate_) + start_offset;
  }

  // Calculate the iteration time
  base::TimeDelta iteration_time;
  if (scaled_active_time - start_offset == repeated_duration &&
      fmod(iterations_ + iteration_start_, 1) == 0)
    iteration_time = curve_->Duration();
  else
    iteration_time = scaled_active_time % curve_->Duration();

  // Calculate the current iteration
  int iteration;
  if (scaled_active_time <= base::TimeDelta())
    iteration = 0;
  else if (iteration_time == curve_->Duration())
    iteration = ceil(iteration_start_ + iterations_ - 1);
  else
    iteration = static_cast<int>(scaled_active_time / curve_->Duration());

  // Check if we are running the animation in reverse direction for the current
  // iteration
  bool reverse =
      (direction_ == Direction::REVERSE) ||
      (direction_ == Direction::ALTERNATE_NORMAL && iteration % 2 == 1) ||
      (direction_ == Direction::ALTERNATE_REVERSE && iteration % 2 == 0);

  // If we are running the animation in reverse direction, reverse the result
  if (reverse)
    iteration_time = curve_->Duration() - iteration_time;

  return iteration_time;
}

std::unique_ptr<Animation> Animation::CloneAndInitialize(
    RunState initial_run_state) const {
  std::unique_ptr<Animation> to_return(
      new Animation(curve_->Clone(), id_, group_, target_property_id_));
  to_return->run_state_ = initial_run_state;
  to_return->iterations_ = iterations_;
  to_return->iteration_start_ = iteration_start_;
  to_return->start_time_ = start_time_;
  to_return->pause_time_ = pause_time_;
  to_return->total_paused_time_ = total_paused_time_;
  to_return->time_offset_ = time_offset_;
  to_return->direction_ = direction_;
  to_return->playback_rate_ = playback_rate_;
  to_return->fill_mode_ = fill_mode_;
  DCHECK(!to_return->is_controlling_instance_);
  to_return->is_controlling_instance_ = true;
  return to_return;
}

void Animation::PushPropertiesTo(Animation* other) const {
  // Currently, we only push changes due to pausing and resuming animations on
  // the main thread.
  if (run_state_ == Animation::PAUSED ||
      other->run_state_ == Animation::PAUSED) {
    other->run_state_ = run_state_;
    other->pause_time_ = pause_time_;
    other->total_paused_time_ = total_paused_time_;
  }
}

}  // namespace cc
