// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_TETHER_NOTIFICATION_PRESENTER_H_
#define CHROMEOS_COMPONENTS_TETHER_NOTIFICATION_PRESENTER_H_

#include "base/macros.h"
#include "base/observer_list.h"
#include "chromeos/network/network_state.h"
#include "components/cryptauth/remote_device.h"

namespace chromeos {

namespace tether {

class NotificationPresenter {
 public:
  NotificationPresenter() {}
  virtual ~NotificationPresenter() {}

  // Notifies the user that a nearby device can potentially provide a tether
  // hotspot, and shows the signal strength with a blue icon.
  virtual void NotifyPotentialHotspotNearby(
      const cryptauth::RemoteDevice& remote_device,
      int signal_strength) = 0;

  // Notifies the user that multiple nearby devices can potentially provide
  // tether hotspots.
  virtual void NotifyMultiplePotentialHotspotsNearby() = 0;

  // Removes the notification created by either NotifyPotentialHotspotNearby()
  // or NotifyMultiplePotentialHotspotsNearby(), or does nothing if that
  // notification is not currently displayed.
  virtual void RemovePotentialHotspotNotification() = 0;

  // Notifies the user that the device they are connecting to requires
  // first time setup and must be interacted with.
  virtual void NotifySetupRequired(const std::string& device_name) = 0;

  // Removes the notification created by NotifyFirstTimeSetupRequired(), or does
  // nothing if that notification is not currently displayed.
  virtual void RemoveSetupRequiredNotification() = 0;

  // Notifies the user that the connection attempt has failed.
  virtual void NotifyConnectionToHostFailed() = 0;

  // Removes the notification created by NotifyConnectionToHostFailed(), or does
  // nothing if that notification is not currently displayed.
  virtual void RemoveConnectionToHostFailedNotification() = 0;

  // Notifies the user that enabling Blueooth allows searching for Tether
  // networks.
  virtual void NotifyEnableBluetooth() = 0;

  // Removes the notification created by NotifyEnableBluetooth(), or does
  // nothign if that notification is not currently displayed.
  virtual void RemoveEnableBluetoothNotification() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(NotificationPresenter);
};

}  // namespace tether

}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_TETHER_NOTIFICATION_PRESENTER_H_
