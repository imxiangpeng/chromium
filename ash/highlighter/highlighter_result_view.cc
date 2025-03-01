// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/highlighter/highlighter_result_view.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/views/widget/widget.h"

namespace ash {

namespace {

// Variables for rendering the highlight result. Sizes in DIP.
constexpr float kCornerCircleRadius = 6;
constexpr float kStrokeFillWidth = 2;
constexpr float kStrokeOutlineWidth = 1;
constexpr float kOutsetForAntialiasing = 1;
constexpr float kResultLayerMargin =
    kOutsetForAntialiasing + kCornerCircleRadius;

constexpr int kInnerFillOpacity = 0x0D;
const SkColor kInnerFillColor = SkColorSetRGB(0x00, 0x00, 0x00);

constexpr int kStrokeFillOpacity = 0xFF;
const SkColor kStrokeFillColor = SkColorSetRGB(0xFF, 0xFF, 0xFF);

constexpr int kStrokeOutlineOpacity = 0x14;
const SkColor kStrokeOutlineColor = SkColorSetRGB(0x00, 0x00, 0x00);

constexpr int kCornerCircleOpacity = 0xFF;
const SkColor kCornerCircleColorLT = SkColorSetRGB(0x42, 0x85, 0xF4);
const SkColor kCornerCircleColorRT = SkColorSetRGB(0xEA, 0x43, 0x35);
const SkColor kCornerCircleColorLB = SkColorSetRGB(0x34, 0xA8, 0x53);
const SkColor kCornerCircleColorRB = SkColorSetRGB(0xFB, 0xBC, 0x05);

constexpr int kResultFadeinDelayMs = 600;
constexpr int kResultFadeinDurationMs = 400;
constexpr int kResultFadeoutDelayMs = 500;
constexpr int kResultFadeoutDurationMs = 200;

constexpr int kResultInPlaceFadeinDelayMs = 500;
constexpr int kResultInPlaceFadeinDurationMs = 500;

constexpr float kInitialScale = 1.2;

class ResultLayer : public ui::Layer, public ui::LayerDelegate {
 public:
  ResultLayer(const gfx::Rect& bounds);

 private:
  void OnDelegatedFrameDamage(const gfx::Rect& damage_rect_in_dip) override {}

  void OnDeviceScaleFactorChanged(float device_scale_factor) override {}

  void OnPaintLayer(const ui::PaintContext& context) override;

  void DrawVerticalBar(gfx::Canvas& canvas,
                       float x,
                       float y,
                       float height,
                       cc::PaintFlags& flags);
  void DrawHorizontalBar(gfx::Canvas& canvas,
                         float x,
                         float y,
                         float width,
                         cc::PaintFlags& flags);

  DISALLOW_COPY_AND_ASSIGN(ResultLayer);
};

ResultLayer::ResultLayer(const gfx::Rect& box) {
  set_name("HighlighterResultView:ResultLayer");
  gfx::Rect bounds = box;
  bounds.Inset(-kResultLayerMargin, -kResultLayerMargin);
  SetBounds(bounds);
  SetFillsBoundsOpaquely(false);
  SetMasksToBounds(false);
  set_delegate(this);
}

void ResultLayer::OnPaintLayer(const ui::PaintContext& context) {
  ui::PaintRecorder recorder(context, size());
  gfx::Canvas& canvas = *recorder.canvas();

  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setAntiAlias(true);

  const float left = kResultLayerMargin;
  const float right = size().width() - kResultLayerMargin;
  const float width = right - left;

  const float top = kResultLayerMargin;
  const float bottom = size().height() - kResultLayerMargin;
  const float height = bottom - top;

  flags.setColor(SkColorSetA(kInnerFillColor, kInnerFillOpacity));
  canvas.DrawRect(gfx::RectF(left, top, width, height), flags);

  DrawVerticalBar(canvas, left, top, height, flags);
  DrawVerticalBar(canvas, right, top, height, flags);
  DrawHorizontalBar(canvas, left, top, width, flags);
  DrawHorizontalBar(canvas, left, bottom, width, flags);

  flags.setColor(SkColorSetA(kCornerCircleColorLT, kCornerCircleOpacity));
  canvas.DrawCircle(gfx::PointF(left, top), kCornerCircleRadius, flags);

  flags.setColor(SkColorSetA(kCornerCircleColorRT, kCornerCircleOpacity));
  canvas.DrawCircle(gfx::PointF(right, top), kCornerCircleRadius, flags);

  flags.setColor(SkColorSetA(kCornerCircleColorLB, kCornerCircleOpacity));
  canvas.DrawCircle(gfx::PointF(right, bottom), kCornerCircleRadius, flags);

  flags.setColor(SkColorSetA(kCornerCircleColorRB, kCornerCircleOpacity));
  canvas.DrawCircle(gfx::PointF(left, bottom), kCornerCircleRadius, flags);
}

void ResultLayer::DrawVerticalBar(gfx::Canvas& canvas,
                                  float x,
                                  float y,
                                  float height,
                                  cc::PaintFlags& flags) {
  const float x_fill = x - kStrokeFillWidth / 2;
  const float x_outline_left = x_fill - kStrokeOutlineWidth;
  const float x_outline_right = x_fill + kStrokeFillWidth;

  flags.setColor(SkColorSetA(kStrokeFillColor, kStrokeFillOpacity));
  canvas.DrawRect(gfx::RectF(x_fill, y, kStrokeFillWidth, height), flags);

  flags.setColor(SkColorSetA(kStrokeOutlineColor, kStrokeOutlineOpacity));
  canvas.DrawRect(gfx::RectF(x_outline_left, y, kStrokeOutlineWidth, height),
                  flags);
  canvas.DrawRect(gfx::RectF(x_outline_right, y, kStrokeOutlineWidth, height),
                  flags);
}

void ResultLayer::DrawHorizontalBar(gfx::Canvas& canvas,
                                    float x,
                                    float y,
                                    float width,
                                    cc::PaintFlags& flags) {
  const float y_fill = y - kStrokeFillWidth / 2;
  const float y_outline_left = y_fill - kStrokeOutlineWidth;
  const float y_outline_right = y_fill + kStrokeFillWidth;

  flags.setColor(SkColorSetA(kStrokeFillColor, kStrokeFillOpacity));
  canvas.DrawRect(gfx::RectF(x, y_fill, width, kStrokeFillWidth), flags);

  flags.setColor(SkColorSetA(kStrokeOutlineColor, kStrokeOutlineOpacity));
  canvas.DrawRect(gfx::RectF(x, y_outline_left, width, kStrokeOutlineWidth),
                  flags);
  canvas.DrawRect(gfx::RectF(x, y_outline_right, width, kStrokeOutlineWidth),
                  flags);
}

}  // namespace

HighlighterResultView::HighlighterResultView(aura::Window* root_window) {
  widget_ = base::MakeUnique<views::Widget>();

  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_WINDOW_FRAMELESS;
  params.name = "HighlighterResult";
  params.accept_events = false;
  params.activatable = views::Widget::InitParams::ACTIVATABLE_NO;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.parent =
      Shell::GetContainer(root_window, kShellWindowId_OverlayContainer);
  params.layer_type = ui::LAYER_SOLID_COLOR;

  widget_->Init(params);
  widget_->Show();
  widget_->SetContentsView(this);
  widget_->SetFullscreen(true);
  set_owned_by_client();
}

HighlighterResultView::~HighlighterResultView() {}

void HighlighterResultView::AnimateInPlace(const gfx::Rect& bounds,
                                           SkColor color) {
  ui::Layer* layer = widget_->GetLayer();

  // A solid transparent rectangle.
  result_layer_ = base::MakeUnique<ui::Layer>(ui::LAYER_SOLID_COLOR);
  result_layer_->set_name("HighlighterResultView:SOLID_LAYER");
  result_layer_->SetBounds(bounds);
  result_layer_->SetFillsBoundsOpaquely(false);
  result_layer_->SetMasksToBounds(false);
  result_layer_->SetColor(color);

  layer->Add(result_layer_.get());

  ScheduleFadeIn(
      base::TimeDelta::FromMilliseconds(kResultInPlaceFadeinDelayMs),
      base::TimeDelta::FromMilliseconds(kResultInPlaceFadeinDurationMs));
}

void HighlighterResultView::AnimateDeflate(const gfx::Rect& bounds) {
  ui::Layer* layer = widget_->GetLayer();

  result_layer_ = base::MakeUnique<ResultLayer>(bounds);
  layer->Add(result_layer_.get());

  gfx::Transform transform;
  const gfx::Point pivot = bounds.CenterPoint();
  transform.Translate(pivot.x() * (1 - kInitialScale),
                      pivot.y() * (1 - kInitialScale));
  transform.Scale(kInitialScale, kInitialScale);
  layer->SetTransform(transform);

  ScheduleFadeIn(base::TimeDelta::FromMilliseconds(kResultFadeinDelayMs),
                 base::TimeDelta::FromMilliseconds(kResultFadeinDurationMs));
}

void HighlighterResultView::ScheduleFadeIn(const base::TimeDelta& delay,
                                           const base::TimeDelta& duration) {
  ui::Layer* layer = widget_->GetLayer();

  layer->SetOpacity(0);

  animation_timer_.reset(
      new base::Timer(FROM_HERE, delay,
                      base::Bind(&HighlighterResultView::FadeIn,
                                 base::Unretained(this), duration),
                      false));
  animation_timer_->Reset();
}

void HighlighterResultView::FadeIn(const base::TimeDelta& duration) {
  ui::Layer* layer = widget_->GetLayer();

  {
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(duration);
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    layer->SetOpacity(1);

    gfx::Transform transform;
    transform.Scale(1, 1);
    transform.Translate(0, 0);
    layer->SetTransform(transform);
  }

  animation_timer_.reset(new base::Timer(
      FROM_HERE,
      base::TimeDelta::FromMilliseconds(kResultFadeinDurationMs +
                                        kResultFadeoutDelayMs),
      base::Bind(&HighlighterResultView::FadeOut, base::Unretained(this)),
      false));
  animation_timer_->Reset();
}

void HighlighterResultView::FadeOut() {
  ui::Layer* layer = widget_->GetLayer();

  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kResultFadeoutDurationMs));
  settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
  layer->SetOpacity(0);
}

}  // namespace ash
