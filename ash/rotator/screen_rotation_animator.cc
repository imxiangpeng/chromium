// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/rotator/screen_rotation_animator.h"

#include "ash/ash_switches.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/rotator/screen_rotation_animation.h"
#include "ash/rotator/screen_rotation_animator_observer.h"
#include "ash/shell.h"
#include "ash/utility/transformer_util.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"
#include "components/viz/common/quads/copy_output_request.h"
#include "components/viz/common/quads/copy_output_result.h"
#include "ui/aura/window.h"
#include "ui/compositor/callback_layer_animation_observer.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_owner.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/display/display.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/manager/managed_display_info.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"
#include "ui/wm/core/window_util.h"

namespace ash {

namespace {

// The number of degrees that the rotation animations animate through.
const int kRotationDegrees = 20;

// The time it takes for the rotation animations to run.
const int kRotationDurationInMs = 250;

// The rotation factors.
const int kCounterClockWiseRotationFactor = 1;
const int kClockWiseRotationFactor = -1;

display::Display::Rotation GetCurrentScreenRotation(int64_t display_id) {
  return Shell::Get()
      ->display_manager()
      ->GetDisplayInfo(display_id)
      .GetActiveRotation();
}

// 180 degree rotations should animate clock-wise.
int GetRotationFactor(display::Display::Rotation initial_rotation,
                      display::Display::Rotation new_rotation) {
  return (initial_rotation + 3) % 4 == new_rotation
             ? kCounterClockWiseRotationFactor
             : kClockWiseRotationFactor;
}

aura::Window* GetScreenRotationContainer(aura::Window* root_window) {
  return root_window->GetChildById(kShellWindowId_ScreenRotationContainer);
}

// Returns true if the rotation between |initial_rotation| and |new_rotation| is
// 180 degrees.
bool Is180DegreeFlip(display::Display::Rotation initial_rotation,
                     display::Display::Rotation new_rotation) {
  return (initial_rotation + 2) % 4 == new_rotation;
}

// Returns the initial degrees the old layer animation to begin with.
int GetInitialDegrees(display::Display::Rotation initial_rotation,
                      display::Display::Rotation new_rotation) {
  return (Is180DegreeFlip(initial_rotation, new_rotation) ? 180 : 90);
}

void AddLayerAtTopOfWindowLayers(aura::Window* root_window, ui::Layer* layer) {
  // Add the cloned/copied layer tree into the root, so it will be rendered.
  root_window->layer()->Add(layer);
  root_window->layer()->StackAtTop(layer);
}

void AddLayerBelowWindowLayer(aura::Window* root_window,
                              ui::Layer* top_layer,
                              ui::Layer* layer) {
  // Add the cloned/copied layer tree into the root, so it will be rendered.
  root_window->layer()->Add(layer);
  root_window->layer()->StackBelow(layer, top_layer);
}

// The Callback will be invoked when all animation sequences have
// finished. |observer| will be destroyed after invoking the Callback if it
// returns true.
bool AnimationEndedCallback(
    base::WeakPtr<ScreenRotationAnimator> animator,
    const ui::CallbackLayerAnimationObserver& observer) {
  if (animator)
    animator->ProcessAnimationQueue();
  return true;
}

// Creates a Transform for the old layer in screen rotation animation.
gfx::Transform CreateScreenRotationOldLayerTransformForDisplay(
    display::Display::Rotation old_rotation,
    display::Display::Rotation new_rotation,
    const display::Display& display) {
  gfx::Transform inverse;
  CHECK(CreateRotationTransform(old_rotation, new_rotation, display)
            .GetInverse(&inverse));
  return inverse;
}

// The |request_id| changed since last copy request, which means a
// new rotation stated, we need to ignore this copy result.
bool IgnoreCopyResult(int64_t request_id, int64_t current_request_id) {
  DCHECK(request_id <= current_request_id);
  return request_id < current_request_id;
}

bool RootWindowChangedForDisplayId(aura::Window* root_window,
                                   int64_t display_id) {
  return root_window != Shell::GetRootWindowForDisplayId(display_id);
}

// Creates a mask layer and returns the |mask_layer_tree_owner|.
std::unique_ptr<ui::LayerTreeOwner> CreateMaskLayerTreeOwner(
    const gfx::Rect& rect) {
  std::unique_ptr<ui::Layer> mask_layer =
      base::MakeUnique<ui::Layer>(ui::LAYER_SOLID_COLOR);
  mask_layer->SetBounds(rect);
  mask_layer->SetColor(SK_ColorBLACK);
  return base::MakeUnique<ui::LayerTreeOwner>(std::move(mask_layer));
}

class ScreenRotationAnimationMetricsReporter
    : public ui::AnimationMetricsReporter {
 public:
  ScreenRotationAnimationMetricsReporter() = default;
  ~ScreenRotationAnimationMetricsReporter() override = default;

  void Report(int value) override {
    UMA_HISTOGRAM_PERCENTAGE("Ash.Rotation.AnimationSmoothness", value);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScreenRotationAnimationMetricsReporter);
};

}  // namespace

ScreenRotationAnimator::ScreenRotationAnimator(aura::Window* root_window)
    : root_window_(root_window),
      screen_rotation_state_(IDLE),
      rotation_request_id_(0),
      metrics_reporter_(
          base::MakeUnique<ScreenRotationAnimationMetricsReporter>()),
      disable_animation_timers_for_test_(false),
      has_switch_ash_disable_smooth_screen_rotation_(
          base::CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kAshDisableSmoothScreenRotation) &&
          base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
              switches::kAshDisableSmoothScreenRotation) != "false"),
      weak_factory_(this) {}

ScreenRotationAnimator::~ScreenRotationAnimator() {
  // To prevent a call to |AnimationEndedCallback()| from calling a method on
  // the |animator_|.
  weak_factory_.InvalidateWeakPtrs();

  // Explicitly reset the |old_layer_tree_owner_| and |metrics_reporter_| in
  // order to make sure |metrics_reporter_| outlives the attached animation
  // sequence.
  old_layer_tree_owner_.reset();
  metrics_reporter_.reset();
}

void ScreenRotationAnimator::StartRotationAnimation(
    std::unique_ptr<ScreenRotationRequest> rotation_request) {
  const display::Display::Rotation current_rotation =
      GetCurrentScreenRotation(rotation_request->display_id);
  if (current_rotation == rotation_request->new_rotation) {
    // We need to call |ProcessAnimationQueue()| to prepare for next rotation
    // request.
    ProcessAnimationQueue();
    return;
  }

  rotation_request->old_rotation = current_rotation;
  if (has_switch_ash_disable_smooth_screen_rotation_) {
    StartSlowAnimation(std::move(rotation_request));
  } else {
    std::unique_ptr<viz::CopyOutputRequest> copy_output_request =
        viz::CopyOutputRequest::CreateRequest(
            CreateAfterCopyCallbackBeforeRotation(std::move(rotation_request)));
    RequestCopyScreenRotationContainerLayer(std::move(copy_output_request));
    screen_rotation_state_ = COPY_REQUESTED;
  }
}

void ScreenRotationAnimator::StartSlowAnimation(
    std::unique_ptr<ScreenRotationRequest> rotation_request) {
  CreateOldLayerTreeForSlowAnimation();
  SetRotation(rotation_request->display_id, rotation_request->old_rotation,
              rotation_request->new_rotation, rotation_request->source);
  AnimateRotation(std::move(rotation_request));
}

void ScreenRotationAnimator::SetRotation(
    int64_t display_id,
    display::Display::Rotation old_rotation,
    display::Display::Rotation new_rotation,
    display::Display::RotationSource source) {
  // Allow compositor locks to extend timeout, so that screen rotation only
  // takes output copy after contents are properlly resized, such as wallpaper
  // and ARC apps.
  ui::Compositor* compositor = root_window_->layer()->GetCompositor();
  compositor->set_allow_locks_to_extend_timeout(true);
  Shell::Get()->display_manager()->SetDisplayRotation(display_id, new_rotation,
                                                      source);
  compositor->set_allow_locks_to_extend_timeout(false);
  const display::Display display =
      Shell::Get()->display_manager()->GetDisplayForId(display_id);
  old_layer_tree_owner_->root()->SetTransform(
      CreateScreenRotationOldLayerTransformForDisplay(old_rotation,
                                                      new_rotation, display));
}

void ScreenRotationAnimator::RequestCopyScreenRotationContainerLayer(
    std::unique_ptr<viz::CopyOutputRequest> copy_output_request) {
  ui::Layer* screen_rotation_container_layer =
      GetScreenRotationContainer(root_window_)->layer();
  copy_output_request->set_area(
      gfx::Rect(screen_rotation_container_layer->size()));
  screen_rotation_container_layer->RequestCopyOfOutput(
      std::move(copy_output_request));
}

ScreenRotationAnimator::CopyCallback
ScreenRotationAnimator::CreateAfterCopyCallbackBeforeRotation(
    std::unique_ptr<ScreenRotationRequest> rotation_request) {
  return base::BindOnce(&ScreenRotationAnimator::
                            OnScreenRotationContainerLayerCopiedBeforeRotation,
                        weak_factory_.GetWeakPtr(),
                        base::Passed(&rotation_request));
}

ScreenRotationAnimator::CopyCallback
ScreenRotationAnimator::CreateAfterCopyCallbackAfterRotation(
    std::unique_ptr<ScreenRotationRequest> rotation_request) {
  return base::BindOnce(&ScreenRotationAnimator::
                            OnScreenRotationContainerLayerCopiedAfterRotation,
                        weak_factory_.GetWeakPtr(),
                        base::Passed(&rotation_request));
}

void ScreenRotationAnimator::OnScreenRotationContainerLayerCopiedBeforeRotation(
    std::unique_ptr<ScreenRotationRequest> rotation_request,
    std::unique_ptr<viz::CopyOutputResult> result) {
  if (IgnoreCopyResult(rotation_request->id, rotation_request_id_))
    return;
  // Abort rotation and animation if the display was removed or the
  // |root_window| was changed for |display_id|.
  if (RootWindowChangedForDisplayId(root_window_,
                                    rotation_request->display_id)) {
    ProcessAnimationQueue();
    return;
  }
  // Abort animation and set the rotation to target rotation when the copy
  // request has been canceled or failed. It would fail if, for examples: a) The
  // layer is removed from the compositor and destroye before committing the
  // request to the compositor. b) The compositor is shutdown.
  if (result->IsEmpty()) {
    Shell::Get()->display_manager()->SetDisplayRotation(
        rotation_request->display_id, rotation_request->new_rotation,
        rotation_request->source);
    ProcessAnimationQueue();
    return;
  }

  old_layer_tree_owner_ = CopyLayerTree(std::move(result));
  AddLayerAtTopOfWindowLayers(root_window_, old_layer_tree_owner_->root());
  SetRotation(rotation_request->display_id, rotation_request->old_rotation,
              rotation_request->new_rotation, rotation_request->source);
  std::unique_ptr<viz::CopyOutputRequest> copy_output_request =
      viz::CopyOutputRequest::CreateRequest(
          CreateAfterCopyCallbackAfterRotation(std::move(rotation_request)));
  RequestCopyScreenRotationContainerLayer(std::move(copy_output_request));
}

void ScreenRotationAnimator::OnScreenRotationContainerLayerCopiedAfterRotation(
    std::unique_ptr<ScreenRotationRequest> rotation_request,
    std::unique_ptr<viz::CopyOutputResult> result) {
  if (IgnoreCopyResult(rotation_request->id, rotation_request_id_))
    return;
  // In the following cases, abort animation:
  // 1) if the display was removed,
  // 2) if the |root_window| was changed for |display_id|,
  // 3) the copy request has been canceled or failed. It would fail if,
  // for examples: a) The layer is removed from the compositor and destroye
  // before committing the request to the compositor. b) The compositor is
  // shutdown.
  if (RootWindowChangedForDisplayId(root_window_,
                                    rotation_request->display_id) ||
      result->IsEmpty()) {
    ProcessAnimationQueue();
    return;
  }

  new_layer_tree_owner_ = CopyLayerTree(std::move(result));
  AddLayerBelowWindowLayer(root_window_, old_layer_tree_owner_->root(),
                           new_layer_tree_owner_->root());
  AnimateRotation(std::move(rotation_request));
}

void ScreenRotationAnimator::CreateOldLayerTreeForSlowAnimation() {
  old_layer_tree_owner_ = ::wm::RecreateLayers(root_window_);
  AddLayerAtTopOfWindowLayers(root_window_, old_layer_tree_owner_->root());
}

std::unique_ptr<ui::LayerTreeOwner> ScreenRotationAnimator::CopyLayerTree(
    std::unique_ptr<viz::CopyOutputResult> result) {
  viz::TextureMailbox texture_mailbox;
  std::unique_ptr<viz::SingleReleaseCallback> release_callback;
  result->TakeTexture(&texture_mailbox, &release_callback);
  DCHECK(texture_mailbox.IsTexture());
  const gfx::Rect rect(
      GetScreenRotationContainer(root_window_)->layer()->size());
  std::unique_ptr<ui::Layer> copy_layer = base::MakeUnique<ui::Layer>();
  copy_layer->SetBounds(rect);
  copy_layer->SetTextureMailbox(texture_mailbox, std::move(release_callback),
                                rect.size());
  return base::MakeUnique<ui::LayerTreeOwner>(std::move(copy_layer));
}

void ScreenRotationAnimator::AnimateRotation(
    std::unique_ptr<ScreenRotationRequest> rotation_request) {
  screen_rotation_state_ = ROTATING;
  const int rotation_factor = GetRotationFactor(rotation_request->old_rotation,
                                                rotation_request->new_rotation);
  const int old_layer_initial_rotation_degrees = GetInitialDegrees(
      rotation_request->old_rotation, rotation_request->new_rotation);
  const base::TimeDelta duration =
      base::TimeDelta::FromMilliseconds(kRotationDurationInMs);
  const gfx::Tween::Type tween_type = gfx::Tween::FAST_OUT_LINEAR_IN;
  const gfx::Rect rotated_screen_bounds = root_window_->GetTargetBounds();
  const gfx::Point pivot = gfx::Point(rotated_screen_bounds.width() / 2,
                                      rotated_screen_bounds.height() / 2);

  ui::Layer* screen_rotation_container_layer =
      GetScreenRotationContainer(root_window_)->layer();
  ui::Layer* new_root_layer;
  if (!new_layer_tree_owner_ ||
      has_switch_ash_disable_smooth_screen_rotation_) {
    new_root_layer = screen_rotation_container_layer;
  } else {
    new_root_layer = new_layer_tree_owner_->root();
    // Add a black mask layer on top of |screen_rotation_container_layer|.
    mask_layer_tree_owner_ = CreateMaskLayerTreeOwner(
        gfx::Rect(screen_rotation_container_layer->size()));
    AddLayerBelowWindowLayer(root_window_, new_root_layer,
                             mask_layer_tree_owner_->root());
  }

  std::unique_ptr<ScreenRotationAnimation> new_layer_screen_rotation =
      base::MakeUnique<ScreenRotationAnimation>(
          new_root_layer, kRotationDegrees * rotation_factor,
          0 /* end_degrees */, new_root_layer->opacity(),
          new_root_layer->opacity() /* target_opacity */, pivot, duration,
          tween_type);

  ui::LayerAnimator* new_layer_animator = new_root_layer->GetAnimator();
  new_layer_animator->set_preemption_strategy(
      ui::LayerAnimator::REPLACE_QUEUED_ANIMATIONS);
  std::unique_ptr<ui::LayerAnimationSequence> new_layer_animation_sequence =
      base::MakeUnique<ui::LayerAnimationSequence>(
          std::move(new_layer_screen_rotation));

  ui::Layer* old_root_layer = old_layer_tree_owner_->root();
  const gfx::Rect original_screen_bounds = old_root_layer->GetTargetBounds();
  // The old layer will also be transformed into the new orientation. We will
  // translate it so that the old layer's center point aligns with the new
  // orientation's center point and use that center point as the pivot for the
  // rotation animation.
  gfx::Transform translate_transform;
  translate_transform.Translate(
      (rotated_screen_bounds.width() - original_screen_bounds.width()) / 2,
      (rotated_screen_bounds.height() - original_screen_bounds.height()) / 2);
  old_root_layer->SetTransform(translate_transform);

  std::unique_ptr<ScreenRotationAnimation> old_layer_screen_rotation =
      base::MakeUnique<ScreenRotationAnimation>(
          old_root_layer, old_layer_initial_rotation_degrees * rotation_factor,
          (old_layer_initial_rotation_degrees - kRotationDegrees) *
              rotation_factor,
          old_root_layer->opacity(), 0.0f /* target_opacity */, pivot, duration,
          tween_type);

  ui::LayerAnimator* old_layer_animator = old_root_layer->GetAnimator();
  old_layer_animator->set_preemption_strategy(
      ui::LayerAnimator::REPLACE_QUEUED_ANIMATIONS);
  std::unique_ptr<ui::LayerAnimationSequence> old_layer_animation_sequence =
      base::MakeUnique<ui::LayerAnimationSequence>(
          std::move(old_layer_screen_rotation));

  // In unit tests, we can use ash::ScreenRotationAnimatorTestApi to control the
  // animation.
  if (disable_animation_timers_for_test_) {
    if (new_layer_tree_owner_)
      new_layer_animator->set_disable_timer_for_test(true);
    old_layer_animator->set_disable_timer_for_test(true);
  }
  old_layer_animation_sequence->SetAnimationMetricsReporter(
      metrics_reporter_.get());

  // Add an observer so that the cloned/copied layers can be cleaned up with the
  // animation completes/aborts.
  ui::CallbackLayerAnimationObserver* observer =
      new ui::CallbackLayerAnimationObserver(
          base::Bind(&AnimationEndedCallback, weak_factory_.GetWeakPtr()));
  if (new_layer_tree_owner_)
    new_layer_animator->AddObserver(observer);
  new_layer_animator->StartAnimation(new_layer_animation_sequence.release());
  old_layer_animator->AddObserver(observer);
  old_layer_animator->StartAnimation(old_layer_animation_sequence.release());
  observer->SetActive();
}

void ScreenRotationAnimator::Rotate(display::Display::Rotation new_rotation,
                                    display::Display::RotationSource source) {
  // |rotation_request_id_| is used to skip stale requests. Before the layer
  // CopyOutputResult callback called, there could have new rotation request.
  // Increases |rotation_request_id_| for each new request and in the callback,
  // we compare the |rotation_request.id| and |rotation_request_id_| to
  // determine the stale status.
  rotation_request_id_++;
  const int64_t display_id =
      display::Screen::GetScreen()->GetDisplayNearestWindow(root_window_).id();
  std::unique_ptr<ScreenRotationRequest> rotation_request =
      base::MakeUnique<ScreenRotationRequest>(rotation_request_id_, display_id,
                                              new_rotation, source);
  target_rotation_ = new_rotation;
  switch (screen_rotation_state_) {
    case IDLE:
    case COPY_REQUESTED:
      StartRotationAnimation(std::move(rotation_request));
      break;
    case ROTATING:
      last_pending_request_ = std::move(rotation_request);
      // The pending request will be processed when the
      // |AnimationEndedCallback()| should be called after |StopAnimating()|.
      StopAnimating();
      break;
  }
}

void ScreenRotationAnimator::AddScreenRotationAnimatorObserver(
    ScreenRotationAnimatorObserver* observer) {
  screen_rotation_animator_observers_.AddObserver(observer);
}

void ScreenRotationAnimator::RemoveScreenRotationAnimatorObserver(
    ScreenRotationAnimatorObserver* observer) {
  screen_rotation_animator_observers_.RemoveObserver(observer);
}

void ScreenRotationAnimator::ProcessAnimationQueue() {
  screen_rotation_state_ = IDLE;
  old_layer_tree_owner_.reset();
  new_layer_tree_owner_.reset();
  mask_layer_tree_owner_.reset();
  if (last_pending_request_ &&
      !RootWindowChangedForDisplayId(root_window_,
                                     last_pending_request_->display_id)) {
    StartRotationAnimation(std::move(last_pending_request_));
    return;
  }

  // This is only used in test to notify animator observer.
  for (auto& observer : screen_rotation_animator_observers_)
    observer.OnScreenRotationAnimationFinished(this);
}

bool ScreenRotationAnimator::IsRotating() const {
  return screen_rotation_state_ != IDLE;
}

display::Display::Rotation ScreenRotationAnimator::GetTargetRotation() const {
  return target_rotation_;
}

void ScreenRotationAnimator::StopAnimating() {
  // |old_layer_tree_owner_| new_layer_tree_owner_| could be nullptr if another
  // the rotation request comes before the copy request finished.
  if (old_layer_tree_owner_)
    old_layer_tree_owner_->root()->GetAnimator()->StopAnimating();
  if (new_layer_tree_owner_)
    new_layer_tree_owner_->root()->GetAnimator()->StopAnimating();
  mask_layer_tree_owner_.reset();
}

}  // namespace ash
