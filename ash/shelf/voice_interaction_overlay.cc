// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/voice_interaction_overlay.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "ash/public/cpp/shelf_types.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/shelf/app_list_button.h"
#include "ash/shelf/ink_drop_button_listener.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_constants.h"
#include "ash/shelf/shelf_view.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/tray_popup_utils.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chromeos/chromeos_switches.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/app_list/presenter/app_list.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/callback_layer_animation_observer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/paint_context.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/animation/flood_fill_ink_drop_ripple.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_mask.h"
#include "ui/views/animation/ink_drop_painted_layer_delegates.h"
#include "ui/views/painter.h"
#include "ui/views/widget/widget.h"

namespace ash {
namespace {
constexpr int kFullExpandDurationMs = 450;
constexpr int kFullRetractDurationMs = 300;
constexpr int kFullBurstDurationMs = 200;

constexpr float kRippleCircleInitRadiusDip = 40.f;
constexpr float kRippleCircleStartRadiusDip = 1.f;
constexpr float kRippleCircleRadiusDip = 77.f;
constexpr float kRippleCircleBurstRadiusDip = 96.f;
constexpr SkColor kRippleColor = SK_ColorWHITE;
constexpr int kRippleExpandDurationMs = 400;
constexpr int kRippleOpacityDurationMs = 100;
constexpr int kRippleOpacityRetractDurationMs = 200;
constexpr float kRippleOpacity = 0.2f;

constexpr float kIconInitSizeDip = 48.f;
constexpr float kIconStartSizeDip = 4.f;
constexpr float kIconSizeDip = 24.f;
constexpr float kIconEndSizeDip = 48.f;
constexpr float kIconOffsetDip = 56.f;
constexpr float kIconOpacity = 1.f;

constexpr float kBackgroundInitSizeDip = 48.f;
constexpr float kBackgroundStartSizeDip = 10.f;
constexpr float kBackgroundSizeDip = 48.f;
constexpr int kBackgroundOpacityDurationMs = 200;
constexpr float kBackgroundShadowElevationDip = 24.f;
// TODO(xiaohuic): this is 2x device size, 1x actually have a different size.
// Need to figure out a way to dynamically change sizes.
constexpr float kBackgroundLargeWidthDip = 352.5f;
constexpr float kBackgroundLargeHeightDip = 540.0f;
constexpr float kBackgroundCornerRadiusDip = 2.f;
constexpr float kBackgroundPaddingDip = 6.f;
constexpr int kBackgroundMorphDurationMs = 300;
constexpr SkColor kBackgroundColor = SK_ColorWHITE;
constexpr SkColor kBackgroundFinalColor = static_cast<SkColor>(0xFFF5F5F5);

constexpr int kHideDurationMs = 200;

// The minimum scale factor to use when scaling rectangle layers. Smaller values
// were causing visual anomalies.
constexpr float kMinimumRectScale = 0.0001f;

// The minimum scale factor to use when scaling circle layers. Smaller values
// were causing visual anomalies.
constexpr float kMinimumCircleScale = 0.001f;

// These are voice interaction logo specs.
constexpr float kMoleculeOffsetXDip[] = {-10.f, 10.f, 10.f, 19.f};
constexpr float kMoleculeOffsetYDip[] = {-8.f, -2.f, 13.f, -9.f};
constexpr float kMoleculeRadiusDip[] = {12.f, 6.f, 7.f, 3.f};
constexpr float kMoleculeAmplitude = 2.f;
constexpr SkColor kMoleculeColors[] = {
    static_cast<SkColor>(0xFF4184F3),  // Blue
    static_cast<SkColor>(0xFFEA4335),  // Red
    static_cast<SkColor>(0xFFFBBC05),  // Yellow
    static_cast<SkColor>(0xFF34A853)   // Green
};
constexpr int kMoleculeAnimationDurationMs = 1200;
constexpr int kMoleculeAnimationOffset = 50;
constexpr int kMoleculeOrder[] = {0, 2, 3, 1};

}  // namespace

class VoiceInteractionIcon : public ui::Layer {
 public:
  VoiceInteractionIcon() : Layer(ui::LAYER_NOT_DRAWN) {
    set_name("VoiceInteractionOverlay:ICON_LAYER");
    SetBounds(gfx::Rect(0, 0, kIconInitSizeDip, kIconInitSizeDip));
    SetFillsBoundsOpaquely(false);
    SetMasksToBounds(false);

    InitMoleculeShape();
  }

  void StartAnimation() {
    animation_timer_.Start(FROM_HERE,
                           base::TimeDelta::FromMilliseconds(
                               base::TimeTicks::kMillisecondsPerSecond /
                               gfx::LinearAnimation::kDefaultFrameRate),
                           this, &VoiceInteractionIcon::AnimationProgressed);
  }

  void StopAnimation() { animation_timer_.Stop(); }

 private:
  enum Dot {
    BLUE_DOT = 0,
    RED_DOT,
    YELLOW_DOT,
    GREEN_DOT,
    // The total number of shapes, not an actual shape.
    DOT_COUNT
  };

  std::string ToLayerName(Dot dot) {
    switch (dot) {
      case BLUE_DOT:
        return "BLUE_DOT";
      case RED_DOT:
        return "RED_DOT";
      case YELLOW_DOT:
        return "YELLOW_DOT";
      case GREEN_DOT:
        return "GREEN_DOT";
      case DOT_COUNT:
        NOTREACHED() << "The DOT_COUNT value should never be used.";
        return "DOT_COUNT";
    }
    return "UNKNOWN";
  }

  void AnimationProgressed() {
    gfx::Transform transform;

    uint64_t now = base::TimeTicks::Now().since_origin().InMilliseconds();
    for (int i = 0; i < DOT_COUNT; ++i) {
      float normalizedTime =
          ((now - kMoleculeAnimationOffset * kMoleculeOrder[i]) %
           kMoleculeAnimationDurationMs) /
          static_cast<float>(kMoleculeAnimationDurationMs);

      transform.MakeIdentity();
      transform.Translate(0,
                          kMoleculeAmplitude * sin(normalizedTime * 2 * M_PI));

      dot_layers_[i]->SetTransform(transform);
    }
  }

  /**
   * Convenience method to place dots to Molecule shape used by Molecule
   * animations.
   */
  void InitMoleculeShape() {
    for (int i = 0; i < DOT_COUNT; ++i) {
      dot_layer_delegates_[i] = base::MakeUnique<views::CircleLayerDelegate>(
          kMoleculeColors[i], kMoleculeRadiusDip[i]);
      dot_layers_[i] = base::MakeUnique<ui::Layer>();

      dot_layers_[i]->SetBounds(gfx::Rect(
          kIconInitSizeDip / 2 + kMoleculeOffsetXDip[i] - kMoleculeRadiusDip[i],
          kIconInitSizeDip / 2 + kMoleculeOffsetYDip[i] - kMoleculeRadiusDip[i],
          kMoleculeRadiusDip[i] * 2, kMoleculeRadiusDip[i] * 2));
      dot_layers_[i]->SetFillsBoundsOpaquely(false);
      dot_layers_[i]->set_delegate(dot_layer_delegates_[i].get());
      dot_layers_[i]->SetVisible(true);
      dot_layers_[i]->SetOpacity(1.0);
      dot_layers_[i]->SetMasksToBounds(false);
      dot_layers_[i]->set_name("DOT:" + ToLayerName(static_cast<Dot>(i)));

      Add(dot_layers_[i].get());
    }
  }

  std::unique_ptr<ui::Layer> dot_layers_[DOT_COUNT];
  std::unique_ptr<views::CircleLayerDelegate> dot_layer_delegates_[DOT_COUNT];

  base::RepeatingTimer animation_timer_;

  DISALLOW_COPY_AND_ASSIGN(VoiceInteractionIcon);
};

class VoiceInteractionIconBackground : public ui::Layer,
                                       public ui::LayerDelegate {
 public:
  VoiceInteractionIconBackground()
      : Layer(ui::LAYER_NOT_DRAWN),
        large_size_(
            gfx::Size(kBackgroundLargeWidthDip, kBackgroundLargeHeightDip)),
        small_size_(gfx::Size(kBackgroundSizeDip, kBackgroundSizeDip)),
        center_point_(
            gfx::PointF(kBackgroundSizeDip / 2, kBackgroundSizeDip / 2)),
        circle_layer_delegate_(base::MakeUnique<views::CircleLayerDelegate>(
            kBackgroundColor,
            kBackgroundSizeDip / 2)),
        bg_circle_layer_delegate_(base::MakeUnique<views::CircleLayerDelegate>(
            kBackgroundFinalColor,
            kBackgroundSizeDip / 2)),
        rect_layer_delegate_(base::MakeUnique<views::RectangleLayerDelegate>(
            kBackgroundColor,
            gfx::SizeF(small_size_))),
        bg_rect_layer_delegate_(base::MakeUnique<views::RectangleLayerDelegate>(
            kBackgroundFinalColor,
            gfx::SizeF(small_size_))) {
    set_name("VoiceInteractionOverlay:BACKGROUND_LAYER");
    SetBounds(gfx::Rect(0, 0, kBackgroundInitSizeDip, kBackgroundInitSizeDip));
    SetFillsBoundsOpaquely(false);
    SetMasksToBounds(false);

    for (int i = 0; i < PAINTED_SHAPE_COUNT; ++i) {
      AddPaintLayer(static_cast<PaintedShape>(i), true);
      AddPaintLayer(static_cast<PaintedShape>(i), false);
    }

    shadow_values_ =
        gfx::ShadowValue::MakeMdShadowValues(kBackgroundShadowElevationDip);
    const gfx::Insets shadow_margin =
        gfx::ShadowValue::GetMargin(shadow_values_);

    shadow_layer_.reset(new ui::Layer());
    shadow_layer_->set_delegate(this);
    shadow_layer_->SetFillsBoundsOpaquely(false);
    shadow_layer_->SetBounds(
        gfx::Rect(shadow_margin.left(), shadow_margin.top(),
                  kBackgroundInitSizeDip - shadow_margin.width(),
                  kBackgroundInitSizeDip - shadow_margin.height()));
    Add(shadow_layer_.get());
  }
  ~VoiceInteractionIconBackground() override{};

  void AnimateToLarge(const gfx::PointF& new_center,
                      ui::LayerAnimationObserver* animation_observer) {
    PaintedShapeTransforms transforms;
    // Setup the painted layers to be the small round size and show it
    CalculateCircleTransforms(small_size_, &transforms);
    SetTransforms(transforms);
    SetPaintedLayersVisible(true);

    // Hide the shadow layer
    shadow_layer_->SetVisible(false);

    center_point_ = new_center;
    // Animate the painted layers to the large rectangle size
    CalculateRectTransforms(large_size_, kBackgroundCornerRadiusDip,
                            &transforms);

    AnimateToTransforms(
        transforms,
        base::TimeDelta::FromMilliseconds(kBackgroundMorphDurationMs),
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET,
        gfx::Tween::LINEAR_OUT_SLOW_IN, animation_observer);
  }

  void ResetShape() {
    // This reverts to the original small round shape.
    shadow_layer_->SetVisible(true);
    SetPaintedLayersVisible(false);
    center_point_.SetPoint(small_size_.width() / 2.f,
                           small_size_.height() / 2.f);
  }

 private:
  // Enumeration of the different shapes that compose the background.
  enum PaintedShape {
    TOP_LEFT_CIRCLE = 0,
    TOP_RIGHT_CIRCLE,
    BOTTOM_RIGHT_CIRCLE,
    BOTTOM_LEFT_CIRCLE,
    HORIZONTAL_RECT,
    VERTICAL_RECT,
    // The total number of shapes, not an actual shape.
    PAINTED_SHAPE_COUNT
  };

  typedef gfx::Transform PaintedShapeTransforms[PAINTED_SHAPE_COUNT];

  void AddPaintLayer(PaintedShape painted_shape, bool is_background) {
    ui::LayerDelegate* delegate = nullptr;
    switch (painted_shape) {
      case TOP_LEFT_CIRCLE:
      case TOP_RIGHT_CIRCLE:
      case BOTTOM_RIGHT_CIRCLE:
      case BOTTOM_LEFT_CIRCLE:
        if (is_background) {
          delegate = bg_circle_layer_delegate_.get();
        } else {
          delegate = circle_layer_delegate_.get();
        }
        break;
      case HORIZONTAL_RECT:
      case VERTICAL_RECT:
        if (is_background) {
          delegate = bg_rect_layer_delegate_.get();
        } else {
          delegate = rect_layer_delegate_.get();
        }
        break;
      case PAINTED_SHAPE_COUNT:
        NOTREACHED() << "PAINTED_SHAPE_COUNT is not an actual shape type.";
        break;
    }

    ui::Layer* layer = new ui::Layer();
    Add(layer);

    layer->SetBounds(gfx::Rect(small_size_));
    layer->SetFillsBoundsOpaquely(false);
    layer->set_delegate(delegate);
    layer->SetVisible(true);
    layer->SetOpacity(1.0);
    layer->SetMasksToBounds(false);
    layer->set_name("PAINTED_SHAPE_COUNT:" + ToLayerName(painted_shape));

    if (is_background) {
      bg_painted_layers_[static_cast<int>(painted_shape)].reset(layer);
    } else {
      painted_layers_[static_cast<int>(painted_shape)].reset(layer);
    }
  }

  void SetTransforms(const PaintedShapeTransforms transforms) {
    for (int i = 0; i < PAINTED_SHAPE_COUNT; ++i) {
      painted_layers_[i]->SetTransform(transforms[i]);
      bg_painted_layers_[i]->SetTransform(transforms[i]);
    }
  }

  void SetPaintedLayersVisible(bool visible) {
    for (int i = 0; i < PAINTED_SHAPE_COUNT; ++i) {
      painted_layers_[i]->SetVisible(visible);
      painted_layers_[i]->SetOpacity(1);
      bg_painted_layers_[i]->SetVisible(visible);
      bg_painted_layers_[i]->SetOpacity(1);
    }
  }

  void CalculateCircleTransforms(const gfx::Size& size,
                                 PaintedShapeTransforms* transforms_out) const {
    CalculateRectTransforms(size, std::min(size.width(), size.height()) / 2.0f,
                            transforms_out);
  }

  void CalculateRectTransforms(const gfx::Size& desired_size,
                               float corner_radius,
                               PaintedShapeTransforms* transforms_out) const {
    DCHECK_GE(desired_size.width() / 2.0f, corner_radius)
        << "The circle's diameter should not be greater than the total width.";
    DCHECK_GE(desired_size.height() / 2.0f, corner_radius)
        << "The circle's diameter should not be greater than the total height.";

    gfx::SizeF size(desired_size);
    // This function can be called before the layer's been added to a view,
    // either at construction time or in tests.
    if (GetCompositor()) {
      // Modify |desired_size| so that the ripple aligns to pixel bounds.
      const float dsf = GetCompositor()->device_scale_factor();
      gfx::RectF ripple_bounds((gfx::PointF(center_point_)), gfx::SizeF());
      ripple_bounds.Inset(-gfx::InsetsF(desired_size.height() / 2.0f,
                                        desired_size.width() / 2.0f));
      ripple_bounds.Scale(dsf);
      ripple_bounds = gfx::RectF(gfx::ToEnclosingRect(ripple_bounds));
      ripple_bounds.Scale(1.0f / dsf);
      size = ripple_bounds.size();
    }

    // The shapes are drawn such that their center points are not at the origin.
    // Thus we use the CalculateCircleTransform() and CalculateRectTransform()
    // methods to calculate the complex Transforms.

    const float circle_scale = std::max(
        kMinimumCircleScale,
        corner_radius / static_cast<float>(circle_layer_delegate_->radius()));

    const float circle_target_x_offset = size.width() / 2.0f - corner_radius;
    const float circle_target_y_offset = size.height() / 2.0f - corner_radius;

    (*transforms_out)[TOP_LEFT_CIRCLE] = CalculateCircleTransform(
        circle_scale, -circle_target_x_offset, -circle_target_y_offset);
    (*transforms_out)[TOP_RIGHT_CIRCLE] = CalculateCircleTransform(
        circle_scale, circle_target_x_offset, -circle_target_y_offset);
    (*transforms_out)[BOTTOM_RIGHT_CIRCLE] = CalculateCircleTransform(
        circle_scale, circle_target_x_offset, circle_target_y_offset);
    (*transforms_out)[BOTTOM_LEFT_CIRCLE] = CalculateCircleTransform(
        circle_scale, -circle_target_x_offset, circle_target_y_offset);

    const float rect_delegate_width = rect_layer_delegate_->size().width();
    const float rect_delegate_height = rect_layer_delegate_->size().height();

    (*transforms_out)[HORIZONTAL_RECT] = CalculateRectTransform(
        std::max(kMinimumRectScale, size.width() / rect_delegate_width),
        std::max(kMinimumRectScale, (size.height() - 2.0f * corner_radius) /
                                        rect_delegate_height));

    (*transforms_out)[VERTICAL_RECT] = CalculateRectTransform(
        std::max(kMinimumRectScale,
                 (size.width() - 2.0f * corner_radius) / rect_delegate_width),
        std::max(kMinimumRectScale, size.height() / rect_delegate_height));
  }

  gfx::Transform CalculateCircleTransform(float scale,
                                          float target_center_x,
                                          float target_center_y) const {
    gfx::Transform transform;
    // Offset for the center point of the ripple.
    transform.Translate(center_point_.x(), center_point_.y());
    // Move circle to target.
    transform.Translate(target_center_x, target_center_y);
    transform.Scale(scale, scale);
    // Align center point of the painted circle.
    const gfx::Vector2dF circle_center_offset =
        circle_layer_delegate_->GetCenteringOffset();
    transform.Translate(-circle_center_offset.x(), -circle_center_offset.y());
    return transform;
  }

  gfx::Transform CalculateRectTransform(float x_scale, float y_scale) const {
    gfx::Transform transform;
    transform.Translate(center_point_.x(), center_point_.y());
    transform.Scale(x_scale, y_scale);
    const gfx::Vector2dF rect_center_offset =
        rect_layer_delegate_->GetCenteringOffset();
    transform.Translate(-rect_center_offset.x(), -rect_center_offset.y());
    return transform;
  }

  void AnimateToTransforms(
      const PaintedShapeTransforms transforms,
      base::TimeDelta duration,
      ui::LayerAnimator::PreemptionStrategy preemption_strategy,
      gfx::Tween::Type tween,
      ui::LayerAnimationObserver* animation_observer) {
    for (int i = 0; i < PAINTED_SHAPE_COUNT; ++i) {
      ui::LayerAnimator* animator = painted_layers_[i]->GetAnimator();
      ui::ScopedLayerAnimationSettings animation(animator);
      animation.SetPreemptionStrategy(preemption_strategy);
      animation.SetTweenType(tween);
      animation.SetTransitionDuration(duration);
      painted_layers_[i]->SetOpacity(0);
      std::unique_ptr<ui::LayerAnimationElement> element =
          ui::LayerAnimationElement::CreateTransformElement(transforms[i],
                                                            duration);
      ui::LayerAnimationSequence* sequence =
          new ui::LayerAnimationSequence(std::move(element));

      if (animation_observer)
        sequence->AddObserver(animation_observer);

      animator->StartAnimation(sequence);
    }

    for (int i = 0; i < PAINTED_SHAPE_COUNT; ++i) {
      ui::LayerAnimator* animator = bg_painted_layers_[i]->GetAnimator();
      ui::ScopedLayerAnimationSettings animation(animator);
      animation.SetPreemptionStrategy(preemption_strategy);
      animation.SetTweenType(tween);
      std::unique_ptr<ui::LayerAnimationElement> element =
          ui::LayerAnimationElement::CreateTransformElement(transforms[i],
                                                            duration);
      ui::LayerAnimationSequence* sequence =
          new ui::LayerAnimationSequence(std::move(element));

      if (animation_observer)
        sequence->AddObserver(animation_observer);

      animator->StartAnimation(sequence);
    }
  }

  std::string ToLayerName(PaintedShape painted_shape) {
    switch (painted_shape) {
      case TOP_LEFT_CIRCLE:
        return "TOP_LEFT_CIRCLE";
      case TOP_RIGHT_CIRCLE:
        return "TOP_RIGHT_CIRCLE";
      case BOTTOM_RIGHT_CIRCLE:
        return "BOTTOM_RIGHT_CIRCLE";
      case BOTTOM_LEFT_CIRCLE:
        return "BOTTOM_LEFT_CIRCLE";
      case HORIZONTAL_RECT:
        return "HORIZONTAL_RECT";
      case VERTICAL_RECT:
        return "VERTICAL_RECT";
      case PAINTED_SHAPE_COUNT:
        NOTREACHED() << "The PAINTED_SHAPE_COUNT value should never be used.";
        return "PAINTED_SHAPE_COUNT";
    }
    return "UNKNOWN";
  }

  void OnPaintLayer(const ui::PaintContext& context) override {
    // Radius is based on the parent layer size, the shadow layer is expanded
    // to make room for the shadow.
    float radius = size().width() / 2.f;

    ui::PaintRecorder recorder(context, shadow_layer_->size());
    gfx::Canvas* canvas = recorder.canvas();

    cc::PaintFlags flags;
    flags.setColor(kBackgroundColor);
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setLooper(gfx::CreateShadowDrawLooper(shadow_values_));
    gfx::Rect shadow_bounds = shadow_layer_->bounds();
    canvas->DrawCircle(
        gfx::PointF(radius - shadow_bounds.x(), radius - shadow_bounds.y()),
        radius, flags);
  }

  void OnDelegatedFrameDamage(const gfx::Rect& damage_rect_in_dip) override {}

  void OnDeviceScaleFactorChanged(float device_scale_factor) override {}

  // ui::Layers for all of the painted shape layers that compose the morphing
  // shape. We have two sets, one is rendered in the foreground, the other set
  // behind. We use them to create an animated transition between two colors by
  // fading out one set during transformation.
  std::unique_ptr<ui::Layer> painted_layers_[PAINTED_SHAPE_COUNT];
  std::unique_ptr<ui::Layer> bg_painted_layers_[PAINTED_SHAPE_COUNT];

  const gfx::Size large_size_;

  const gfx::Size small_size_;

  // The center point of the painted shape.
  gfx::PointF center_point_;

  // ui::LayerDelegate to paint circles for all the circle layers.
  std::unique_ptr<views::CircleLayerDelegate> circle_layer_delegate_;
  std::unique_ptr<views::CircleLayerDelegate> bg_circle_layer_delegate_;

  // ui::LayerDelegate to paint rectangles for all the rectangle layers.
  std::unique_ptr<views::RectangleLayerDelegate> rect_layer_delegate_;
  std::unique_ptr<views::RectangleLayerDelegate> bg_rect_layer_delegate_;

  gfx::ShadowValues shadow_values_;

  std::unique_ptr<ui::Layer> shadow_layer_;

  DISALLOW_COPY_AND_ASSIGN(VoiceInteractionIconBackground);
};

VoiceInteractionOverlay::VoiceInteractionOverlay(AppListButton* host_view)
    : ripple_layer_(base::MakeUnique<ui::Layer>()),
      icon_layer_(base::MakeUnique<VoiceInteractionIcon>()),
      background_layer_(base::MakeUnique<VoiceInteractionIconBackground>()),
      host_view_(host_view),
      is_bursting_(false),
      show_icon_(false),
      should_hide_animation_(false),
      circle_layer_delegate_(kRippleColor, kRippleCircleInitRadiusDip) {
  SetPaintToLayer(ui::LAYER_NOT_DRAWN);
  layer()->set_name("VoiceInteractionOverlay:ROOT_LAYER");
  layer()->SetMasksToBounds(false);

  ripple_layer_->SetBounds(gfx::Rect(0, 0, kRippleCircleInitRadiusDip * 2,
                                     kRippleCircleInitRadiusDip * 2));
  ripple_layer_->set_delegate(&circle_layer_delegate_);
  ripple_layer_->SetFillsBoundsOpaquely(false);
  ripple_layer_->SetMasksToBounds(true);
  ripple_layer_->set_name("VoiceInteractionOverlay:PAINTED_LAYER");
  layer()->Add(ripple_layer_.get());

  layer()->Add(background_layer_.get());

  layer()->Add(icon_layer_.get());
}

VoiceInteractionOverlay::~VoiceInteractionOverlay() {}

void VoiceInteractionOverlay::StartAnimation(bool show_icon) {
  is_bursting_ = false;
  show_icon_ = show_icon;
  SetVisible(true);

  // Setup ripple initial state.
  ripple_layer_->SetOpacity(0);

  SkMScalar scale_factor =
      kRippleCircleStartRadiusDip / kRippleCircleInitRadiusDip;
  gfx::Transform transform;

  const gfx::Point center = host_view_->GetAppListButtonCenterPoint();
  transform.Translate(center.x() - kRippleCircleStartRadiusDip,
                      center.y() - kRippleCircleStartRadiusDip);
  transform.Scale(scale_factor, scale_factor);
  ripple_layer_->SetTransform(transform);

  // Setup ripple animations.
  {
    scale_factor = kRippleCircleRadiusDip / kRippleCircleInitRadiusDip;
    transform.MakeIdentity();
    transform.Translate(center.x() - kRippleCircleRadiusDip,
                        center.y() - kRippleCircleRadiusDip);
    transform.Scale(scale_factor, scale_factor);

    ui::ScopedLayerAnimationSettings settings(ripple_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kRippleExpandDurationMs));
    settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_2);

    ripple_layer_->SetTransform(transform);

    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kRippleOpacityDurationMs));
    ripple_layer_->SetOpacity(kRippleOpacity);
  }

  icon_layer_->SetOpacity(0);
  background_layer_->SetOpacity(0);
  if (!show_icon_)
    return;

  // Setup icon initial state.
  transform.MakeIdentity();
  transform.Translate(center.x() - kIconStartSizeDip / 2.f,
                      center.y() - kIconStartSizeDip / 2.f);

  scale_factor = kIconStartSizeDip / kIconInitSizeDip;
  transform.Scale(scale_factor, scale_factor);
  icon_layer_->SetTransform(transform);

  // Setup icon animation.
  scale_factor = kIconSizeDip / kIconInitSizeDip;
  transform.MakeIdentity();
  transform.Translate(center.x() - kIconSizeDip / 2 + kIconOffsetDip,
                      center.y() - kIconSizeDip / 2 - kIconOffsetDip);
  transform.Scale(scale_factor, scale_factor);

  {
    ui::ScopedLayerAnimationSettings settings(icon_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullExpandDurationMs));
    settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_2);

    icon_layer_->SetTransform(transform);
    icon_layer_->SetOpacity(kIconOpacity);
  }

  // Setup background initial state.
  background_layer_->ResetShape();

  transform.MakeIdentity();
  transform.Translate(center.x() - kBackgroundStartSizeDip / 2.f,
                      center.y() - kBackgroundStartSizeDip / 2.f);

  scale_factor = kBackgroundStartSizeDip / kBackgroundInitSizeDip;
  transform.Scale(scale_factor, scale_factor);
  background_layer_->SetTransform(transform);

  // Setup background animation.
  scale_factor = kBackgroundSizeDip / kBackgroundInitSizeDip;
  transform.MakeIdentity();
  transform.Translate(center.x() - kBackgroundSizeDip / 2 + kIconOffsetDip,
                      center.y() - kBackgroundSizeDip / 2 - kIconOffsetDip);
  transform.Scale(scale_factor, scale_factor);

  {
    ui::ScopedLayerAnimationSettings settings(background_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullExpandDurationMs));
    settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_2);

    background_layer_->SetTransform(transform);
  }

  {
    ui::ScopedLayerAnimationSettings settings(background_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kBackgroundOpacityDurationMs));
    settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_2);

    background_layer_->SetOpacity(1);
  }
}

void VoiceInteractionOverlay::BurstAnimation() {
  is_bursting_ = true;
  should_hide_animation_ = false;

  gfx::Point center = host_view_->GetAppListButtonCenterPoint();
  gfx::Transform transform;

  // Setup ripple animations.
  {
    SkMScalar scale_factor =
        kRippleCircleBurstRadiusDip / kRippleCircleInitRadiusDip;
    transform.Translate(center.x() - kRippleCircleBurstRadiusDip,
                        center.y() - kRippleCircleBurstRadiusDip);
    transform.Scale(scale_factor, scale_factor);

    ui::ScopedLayerAnimationSettings settings(ripple_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullBurstDurationMs));
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::PreemptionStrategy::ENQUEUE_NEW_ANIMATION);

    ripple_layer_->SetTransform(transform);
    ripple_layer_->SetOpacity(0);
  }

  if (!show_icon_)
    return;

  // Setup icon animation.
  // TODO(xiaohuic): Currently the animation does not support RTL.
  {
    ui::ScopedLayerAnimationSettings settings(icon_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kBackgroundMorphDurationMs));
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::PreemptionStrategy::ENQUEUE_NEW_ANIMATION);
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);

    transform.MakeIdentity();
    transform.Translate(kBackgroundLargeWidthDip / 2 + kBackgroundPaddingDip -
                            kIconEndSizeDip / 2,
                        -kBackgroundLargeHeightDip / 2 - kBackgroundPaddingDip -
                            kIconEndSizeDip / 2);
    SkMScalar scale_factor = kIconEndSizeDip / kIconInitSizeDip;
    transform.Scale(scale_factor, scale_factor);

    icon_layer_->SetTransform(transform);
    icon_layer_->StartAnimation();
  }

  // Setup background animation.
  ui::CallbackLayerAnimationObserver* observer =
      new ui::CallbackLayerAnimationObserver(
          base::Bind(&VoiceInteractionOverlay::AnimationEndedCallback,
                     base::Unretained(this)));
  // Transform to new shape.
  // We want to animate from the background's current position into a larger
  // size. The animation moves the background's center point while morphing from
  // circle to a rectangle.
  float x_offset = center.x() - kBackgroundSizeDip / 2 + kIconOffsetDip;
  float y_offset = center.y() - kBackgroundSizeDip / 2 - kIconOffsetDip;

  background_layer_->AnimateToLarge(
      gfx::PointF(
          kBackgroundLargeWidthDip / 2 + kBackgroundPaddingDip - x_offset,
          -kBackgroundLargeHeightDip / 2 - kBackgroundPaddingDip - y_offset),
      observer);
  observer->SetActive();
}

void VoiceInteractionOverlay::EndAnimation() {
  if (is_bursting_) {
    // Too late, user action already fired, we have to finish what's started.
    return;
  }

  // Play reverse animation
  // Setup ripple animations.
  SkMScalar scale_factor =
      kRippleCircleStartRadiusDip / kRippleCircleInitRadiusDip;
  gfx::Transform transform;

  const gfx::Point center = host_view_->GetAppListButtonCenterPoint();
  transform.Translate(center.x() - kRippleCircleStartRadiusDip,
                      center.y() - kRippleCircleStartRadiusDip);
  transform.Scale(scale_factor, scale_factor);

  {
    ui::ScopedLayerAnimationSettings settings(ripple_layer_->GetAnimator());
    settings.SetPreemptionStrategy(ui::LayerAnimator::PreemptionStrategy::
                                       IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullRetractDurationMs));
    settings.SetTweenType(gfx::Tween::SLOW_OUT_LINEAR_IN);

    ripple_layer_->SetTransform(transform);

    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kRippleOpacityRetractDurationMs));
    ripple_layer_->SetOpacity(0);
  }

  if (!show_icon_)
    return;

  // Setup icon animation.
  transform.MakeIdentity();

  transform.Translate(center.x() - kIconStartSizeDip / 2.f,
                      center.y() - kIconStartSizeDip / 2.f);

  scale_factor = kIconStartSizeDip / kIconInitSizeDip;
  transform.Scale(scale_factor, scale_factor);

  {
    ui::ScopedLayerAnimationSettings settings(icon_layer_->GetAnimator());
    settings.SetPreemptionStrategy(ui::LayerAnimator::PreemptionStrategy::
                                       IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullRetractDurationMs));
    settings.SetTweenType(gfx::Tween::SLOW_OUT_LINEAR_IN);

    icon_layer_->SetTransform(transform);
    icon_layer_->SetOpacity(0);
  }

  // Setup background animation.
  transform.MakeIdentity();

  transform.Translate(center.x() - kBackgroundStartSizeDip / 2.f,
                      center.y() - kBackgroundStartSizeDip / 2.f);

  scale_factor = kBackgroundStartSizeDip / kBackgroundInitSizeDip;
  transform.Scale(scale_factor, scale_factor);

  {
    ui::ScopedLayerAnimationSettings settings(background_layer_->GetAnimator());
    settings.SetPreemptionStrategy(ui::LayerAnimator::PreemptionStrategy::
                                       IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kFullRetractDurationMs));
    settings.SetTweenType(gfx::Tween::SLOW_OUT_LINEAR_IN);

    background_layer_->SetTransform(transform);
    background_layer_->SetOpacity(0);
  }
}

void VoiceInteractionOverlay::HideAnimation() {
  is_bursting_ = false;

  if (background_layer_->GetAnimator()->is_animating()) {
    // Wait for current animation to finish
    should_hide_animation_ = true;
    return;
  }
  should_hide_animation_ = false;

  // Setup ripple animations.
  {
    ui::ScopedLayerAnimationSettings settings(ripple_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kHideDurationMs));
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::PreemptionStrategy::ENQUEUE_NEW_ANIMATION);

    ripple_layer_->SetOpacity(0);
  }

  // Setup icon animation.
  {
    ui::ScopedLayerAnimationSettings settings(icon_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kHideDurationMs));
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::PreemptionStrategy::ENQUEUE_NEW_ANIMATION);

    icon_layer_->SetOpacity(0);
    icon_layer_->StopAnimation();
  }

  // Setup background animation.
  {
    ui::ScopedLayerAnimationSettings settings(background_layer_->GetAnimator());
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kHideDurationMs));
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::PreemptionStrategy::ENQUEUE_NEW_ANIMATION);

    background_layer_->SetOpacity(0);
  }
}

bool VoiceInteractionOverlay::AnimationEndedCallback(
    const ui::CallbackLayerAnimationObserver& observer) {
  if (should_hide_animation_)
    HideAnimation();

  return true;
}

}  // namespace ash
