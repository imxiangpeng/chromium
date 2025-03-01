// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/test_session_state_animator.h"

#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/bind.h"

namespace ash {

namespace {
// A no-op callback that can be used when managing an animation that didn't
// actually have a callback given.
void DummyCallback() {}
}  // namespace

const SessionStateAnimator::Container
    TestSessionStateAnimator::kAllContainers[] = {
        SessionStateAnimator::WALLPAPER,
        SessionStateAnimator::LAUNCHER,
        SessionStateAnimator::NON_LOCK_SCREEN_CONTAINERS,
        SessionStateAnimator::LOCK_SCREEN_WALLPAPER,
        SessionStateAnimator::LOCK_SCREEN_CONTAINERS,
        SessionStateAnimator::LOCK_SCREEN_RELATED_CONTAINERS,
        SessionStateAnimator::ROOT_CONTAINER};

// A simple SessionStateAnimator::AnimationSequence that tracks the number of
// attached sequences.  The callback will be invoked if all animations complete
// successfully.
class TestSessionStateAnimator::AnimationSequence
    : public SessionStateAnimator::AnimationSequence {
 public:
  AnimationSequence(base::OnceClosure callback,
                    TestSessionStateAnimator* animator)
      : SessionStateAnimator::AnimationSequence(std::move(callback)),
        sequence_count_(0),
        sequence_aborted_(false),
        animator_(animator) {}

  ~AnimationSequence() override {}

  virtual void SequenceAttached() { ++sequence_count_; }

  // Notify the sequence that is has completed.
  virtual void SequenceFinished(bool successfully) {
    DCHECK_GT(sequence_count_, 0);
    --sequence_count_;
    sequence_aborted_ |= !successfully;
    if (sequence_count_ == 0) {
      if (sequence_aborted_)
        OnAnimationAborted();
      else
        OnAnimationCompleted();
    }
  }

  // ash::SessionStateAnimator::AnimationSequence:
  void StartAnimation(int container_mask,
                      AnimationType type,
                      AnimationSpeed speed) override {
    animator_->StartAnimationInSequence(container_mask, type, speed, this);
  }

 private:
  // Tracks the number of contained animations.
  int sequence_count_;

  // True if the sequence was aborted.
  bool sequence_aborted_;

  // The TestSessionAnimator that created this.  Not owned.
  TestSessionStateAnimator* animator_;

  DISALLOW_COPY_AND_ASSIGN(AnimationSequence);
};

TestSessionStateAnimator::ActiveAnimation::ActiveAnimation(
    int animation_epoch,
    base::TimeDelta duration,
    SessionStateAnimator::Container container,
    AnimationType type,
    AnimationSpeed speed,
    base::Closure success_callback,
    base::Closure failed_callback)
    : animation_epoch(animation_epoch),
      remaining_duration(duration),
      container(container),
      type(type),
      speed(speed),
      success_callback(success_callback),
      failed_callback(failed_callback) {}

TestSessionStateAnimator::ActiveAnimation::ActiveAnimation(
    const ActiveAnimation& other) = default;

TestSessionStateAnimator::ActiveAnimation::~ActiveAnimation() {}

TestSessionStateAnimator::TestSessionStateAnimator()
    : last_animation_epoch_(0), is_wallpaper_hidden_(false) {}

TestSessionStateAnimator::~TestSessionStateAnimator() {
  CompleteAllAnimations(false);
}

void TestSessionStateAnimator::ResetAnimationEpoch() {
  CompleteAllAnimations(false);
  last_animation_epoch_ = 0;
}

void TestSessionStateAnimator::Advance(const base::TimeDelta& duration) {
  for (ActiveAnimationsMap::iterator container_iter =
           active_animations_.begin();
       container_iter != active_animations_.end(); ++container_iter) {
    AnimationList::iterator animation_iter = (*container_iter).second.begin();
    while (animation_iter != (*container_iter).second.end()) {
      ActiveAnimation& active_animation = *animation_iter;
      active_animation.remaining_duration -= duration;
      if (active_animation.remaining_duration <= base::TimeDelta()) {
        active_animation.success_callback.Run();
        animation_iter = (*container_iter).second.erase(animation_iter);
      } else {
        ++animation_iter;
      }
    }
  }
}

void TestSessionStateAnimator::CompleteAnimations(int animation_epoch,
                                                  bool completed_successfully) {
  for (ActiveAnimationsMap::iterator container_iter =
           active_animations_.begin();
       container_iter != active_animations_.end(); ++container_iter) {
    AnimationList::iterator animation_iter = (*container_iter).second.begin();
    while (animation_iter != (*container_iter).second.end()) {
      ActiveAnimation active_animation = *animation_iter;
      if (active_animation.animation_epoch <= animation_epoch) {
        if (completed_successfully)
          active_animation.success_callback.Run();
        else
          active_animation.failed_callback.Run();
        animation_iter = (*container_iter).second.erase(animation_iter);
      } else {
        ++animation_iter;
      }
    }
  }
}

void TestSessionStateAnimator::CompleteAllAnimations(
    bool completed_successfully) {
  CompleteAnimations(last_animation_epoch_, completed_successfully);
}

bool TestSessionStateAnimator::IsContainerAnimated(
    SessionStateAnimator::Container container,
    SessionStateAnimator::AnimationType type) const {
  ActiveAnimationsMap::const_iterator container_iter =
      active_animations_.find(container);
  if (container_iter != active_animations_.end()) {
    for (AnimationList::const_iterator animation_iter =
             (*container_iter).second.begin();
         animation_iter != (*container_iter).second.end(); ++animation_iter) {
      const ActiveAnimation& active_animation = *animation_iter;
      if (active_animation.type == type)
        return true;
    }
  }
  return false;
}

bool TestSessionStateAnimator::AreContainersAnimated(
    int container_mask,
    SessionStateAnimator::AnimationType type) const {
  for (size_t i = 0; i < arraysize(kAllContainers); ++i) {
    if (container_mask & kAllContainers[i] &&
        !IsContainerAnimated(kAllContainers[i], type)) {
      return false;
    }
  }
  return true;
}

size_t TestSessionStateAnimator::GetAnimationCount() const {
  size_t count = 0;
  for (ActiveAnimationsMap::const_iterator container_iter =
           active_animations_.begin();
       container_iter != active_animations_.end(); ++container_iter) {
    count += (*container_iter).second.size();
  }
  return count;
}

void TestSessionStateAnimator::StartAnimation(int container_mask,
                                              AnimationType type,
                                              AnimationSpeed speed) {
  ++last_animation_epoch_;
  for (size_t i = 0; i < arraysize(kAllContainers); ++i) {
    if (container_mask & kAllContainers[i]) {
      // Use a dummy no-op callback because one isn't required by the client
      // but one is required when completing or aborting animations.
      base::Closure callback = base::Bind(&DummyCallback);
      AddAnimation(kAllContainers[i], type, speed, callback, callback);
    }
  }
}

void TestSessionStateAnimator::StartAnimationWithCallback(
    int container_mask,
    AnimationType type,
    AnimationSpeed speed,
    base::OnceClosure callback) {
  ++last_animation_epoch_;

  int container_count = 0;
  for (size_t i = 0; i < arraysize(kAllContainers); ++i) {
    if (container_mask & kAllContainers[i])
      ++container_count;
  }

  base::RepeatingClosure completion_callback =
      base::BarrierClosure(container_count, std::move(callback));
  for (size_t i = 0; i < arraysize(kAllContainers); ++i) {
    if (container_mask & kAllContainers[i]) {
      // ash::SessionStateAnimatorImpl invokes the callback whether or not the
      // animation was completed successfully or not.
      AddAnimation(kAllContainers[i], type, speed, completion_callback,
                   completion_callback);
    }
  }
}

ash::SessionStateAnimator::AnimationSequence*
TestSessionStateAnimator::BeginAnimationSequence(base::OnceClosure callback) {
  return new AnimationSequence(std::move(callback), this);
}

bool TestSessionStateAnimator::IsWallpaperHidden() const {
  return is_wallpaper_hidden_;
}

void TestSessionStateAnimator::ShowWallpaper() {
  is_wallpaper_hidden_ = false;
}

void TestSessionStateAnimator::HideWallpaper() {
  is_wallpaper_hidden_ = true;
}

void TestSessionStateAnimator::StartAnimationInSequence(
    int container_mask,
    AnimationType type,
    AnimationSpeed speed,
    AnimationSequence* animation_sequence) {
  ++last_animation_epoch_;
  for (size_t i = 0; i < arraysize(kAllContainers); ++i) {
    if (container_mask & kAllContainers[i]) {
      base::Closure success_callback =
          base::Bind(&AnimationSequence::SequenceFinished,
                     base::Unretained(animation_sequence), true);
      base::Closure failed_callback =
          base::Bind(&AnimationSequence::SequenceFinished,
                     base::Unretained(animation_sequence), false);
      animation_sequence->SequenceAttached();
      AddAnimation(kAllContainers[i], type, speed, success_callback,
                   failed_callback);
    }
  }
}

void TestSessionStateAnimator::AddAnimation(
    SessionStateAnimator::Container container,
    AnimationType type,
    AnimationSpeed speed,
    base::Closure success_callback,
    base::Closure failed_callback) {
  base::TimeDelta duration = GetDuration(speed);
  ActiveAnimation active_animation(last_animation_epoch_, duration, container,
                                   type, speed, success_callback,
                                   failed_callback);
  // This test double is limited to only have one animation active for a given
  // container at a time.
  AbortAnimation(container);
  active_animations_[container].push_back(active_animation);
}

void TestSessionStateAnimator::AbortAnimation(
    SessionStateAnimator::Container container) {
  ActiveAnimationsMap::iterator container_iter =
      active_animations_.find(container);
  if (container_iter != active_animations_.end()) {
    AnimationList::iterator animation_iter = (*container_iter).second.begin();
    while (animation_iter != (*container_iter).second.end()) {
      ActiveAnimation active_animation = *animation_iter;
      active_animation.failed_callback.Run();
      animation_iter = (*container_iter).second.erase(animation_iter);
    }
  }
}

}  // namespace ash
