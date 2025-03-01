// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/fast_ink/fast_ink_points.h"

#include <algorithm>
#include <array>
#include <limits>

#include "base/containers/adapters.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace ash {

FastInkPoints::FastInkPoints(base::TimeDelta life_duration)
    : life_duration_(life_duration) {}

FastInkPoints::~FastInkPoints() {}

void FastInkPoints::AddPoint(const gfx::PointF& point,
                             const base::TimeTicks& time) {
  FastInkPoint new_point;
  new_point.location = point;
  new_point.time = time;
  points_.push_back(new_point);
}

void FastInkPoints::MoveForwardToTime(const base::TimeTicks& latest_time) {
  DCHECK_GE(latest_time, collection_latest_time_);
  collection_latest_time_ = latest_time;

  if (!points_.empty() && !life_duration_.is_zero()) {
    // Remove obsolete points.
    const base::TimeTicks expiration = latest_time - life_duration_;
    auto first_alive_point = std::find_if(
        points_.begin(), points_.end(),
        [expiration](const FastInkPoint& p) { return p.time > expiration; });
    points_.erase(points_.begin(), first_alive_point);
  }
}

void FastInkPoints::Clear() {
  points_.clear();
}

gfx::Rect FastInkPoints::GetBoundingBox() const {
  if (IsEmpty())
    return gfx::Rect();

  gfx::PointF min_point = GetOldest().location;
  gfx::PointF max_point = GetOldest().location;
  for (const FastInkPoint& point : points_) {
    min_point.SetToMin(point.location);
    max_point.SetToMax(point.location);
  }
  return gfx::ToEnclosingRect(gfx::BoundingRect(min_point, max_point));
}

FastInkPoints::FastInkPoint FastInkPoints::GetOldest() const {
  DCHECK(!IsEmpty());
  return points_.front();
}

FastInkPoints::FastInkPoint FastInkPoints::GetNewest() const {
  DCHECK(!IsEmpty());
  return points_.back();
}

bool FastInkPoints::IsEmpty() const {
  return points_.empty();
}

int FastInkPoints::GetNumberOfPoints() const {
  return points_.size();
}

const std::deque<FastInkPoints::FastInkPoint>& FastInkPoints::points() const {
  return points_;
}

float FastInkPoints::GetFadeoutFactor(int index) const {
  DCHECK(!life_duration_.is_zero());
  DCHECK(0 <= index && index < GetNumberOfPoints());
  base::TimeDelta age = collection_latest_time_ - points_[index].time;
  return std::min(age.InMillisecondsF() / life_duration_.InMillisecondsF(),
                  1.0);
}

void FastInkPoints::Predict(const FastInkPoints& real_points,
                            const base::TimeTicks& current_time,
                            base::TimeDelta prediction_duration,
                            const gfx::Size& screen_size) {
  Clear();

  if (real_points.IsEmpty() || prediction_duration.is_zero())
    return;

  gfx::Vector2dF scale(1.0f / screen_size.width(), 1.0f / screen_size.height());

  // Create a new set of predicted points based on the last four points added.
  // We add enough predicted points to fill the time between the new point and
  // the expected presentation time. Note that estimated presentation time is
  // based on current time and inefficient rendering of points can result in an
  // actual presentation time that is later.

  // TODO(reveman): Determine interval based on history when event time stamps
  // are accurate. b/36137953
  const float kPredictionIntervalMs = 5.0f;
  const float kMaxPointIntervalMs = 10.0f;
  base::TimeDelta prediction_interval =
      base::TimeDelta::FromMilliseconds(kPredictionIntervalMs);
  base::TimeDelta max_point_interval =
      base::TimeDelta::FromMilliseconds(kMaxPointIntervalMs);
  const FastInkPoint newest_real_point = real_points.GetNewest();
  base::TimeTicks last_point_time = newest_real_point.time;
  gfx::PointF last_point_location =
      gfx::ScalePoint(newest_real_point.location, scale.x(), scale.y());

  // Use the last four points for prediction.
  using PositionArray = std::array<gfx::PointF, 4>;
  PositionArray position;
  PositionArray::iterator it = position.begin();
  for (const auto& point : base::Reversed(real_points.points())) {
    // Stop adding positions if interval between points is too large to provide
    // an accurate history for prediction.
    if ((last_point_time - point.time) > max_point_interval)
      break;

    last_point_time = point.time;
    last_point_location = gfx::ScalePoint(point.location, scale.x(), scale.y());
    *it++ = last_point_location;

    // Stop when no more positions are needed.
    if (it == position.end())
      break;
  }

  const size_t valid_positions = it - position.begin();
  if (valid_positions < 2)  // Not enough reliable data, bail out.
    return;

  // Note: Currently there's no need to divide by the time delta between
  // points as we assume a constant delta between points that matches the
  // prediction point interval.
  gfx::Vector2dF velocity[3];
  for (size_t i = 0; i < valid_positions - 1; ++i)
    velocity[i] = position[i] - position[i + 1];
  // velocity[0] is always valid, since |valid_positions| >=2

  gfx::Vector2dF acceleration[2];
  for (size_t i = 0; i < valid_positions - 2; ++i)
    acceleration[i] = velocity[i] - velocity[i + 1];
  // acceleration[0] is always valid (zero if |valid_positions| < 3).

  gfx::Vector2dF jerk;
  if (valid_positions > 3)
     jerk = acceleration[0] - acceleration[1];
  // |jerk| is aways valid (zero if |valid_positions| < 4).

  // Adjust max prediction time based on speed as prediction data is not great
  // at lower speeds.
  const float kMaxPredictionScaleSpeed = 1e-5;
  double speed = velocity[0].LengthSquared();
  base::TimeTicks max_prediction_time =
      current_time +
      std::min(prediction_duration * (speed / kMaxPredictionScaleSpeed),
               prediction_duration);

  // Add predicted points until we reach the max prediction time.
  gfx::PointF location = position[0];
  for (base::TimeTicks time = newest_real_point.time + prediction_interval;
       time < max_prediction_time; time += prediction_interval) {
    // Note: Currently there's no need to multiply by the prediction interval
    // as the velocity is calculated based on a time delta between points that
    // is the same as the prediction interval.
    velocity[0] += acceleration[0];
    acceleration[0] += jerk;
    location += velocity[0];

    AddPoint(gfx::ScalePoint(location, 1 / scale.x(), 1 / scale.y()), time);

    // Always stop at three predicted points as a four point history doesn't
    // provide accurate prediction of more points.
    if (GetNumberOfPoints() == 3)
      break;
  }
}

}  // namespace ash
