// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_RELATIVE_ORIENTATION_EULER_ANGLES_FUSION_ALGORITHM_USING_ACCELEROMETER_H_
#define SERVICES_DEVICE_RELATIVE_ORIENTATION_EULER_ANGLES_FUSION_ALGORITHM_USING_ACCELEROMETER_H_

#include "base/macros.h"
#include "services/device/generic_sensor/platform_sensor_fusion_algorithm.h"

namespace device {

// Sensor fusion algorithm for implementing RELATIVE_ORIENTATION_EULER_ANGLES
// using ACCELEROMETER.
class RelativeOrientationEulerAnglesFusionAlgorithmUsingAccelerometer
    : public PlatformSensorFusionAlgorithm {
 public:
  RelativeOrientationEulerAnglesFusionAlgorithmUsingAccelerometer();
  ~RelativeOrientationEulerAnglesFusionAlgorithmUsingAccelerometer() override;

  void GetFusedData(const std::vector<SensorReading>& readings,
                    SensorReading* fused_reading) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(
      RelativeOrientationEulerAnglesFusionAlgorithmUsingAccelerometer);
};

}  // namespace device

#endif  // SERVICES_DEVICE_RELATIVE_ORIENTATION_EULER_ANGLES_FUSION_ALGORITHM_USING_ACCELEROMETER_H_
