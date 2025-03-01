// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_NET_TETHER_NOTIFICATION_PRESENTER_H_
#define CHROME_BROWSER_CHROMEOS_NET_TETHER_NOTIFICATION_PRESENTER_H_

#include <memory>
#include <string>

#include "ash/system/network/network_icon.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "chromeos/components/tether/notification_presenter.h"
#include "chromeos/network/network_connect.h"
#include "chromeos/network/network_state.h"
#include "components/cryptauth/remote_device.h"
#include "ui/message_center/message_center_observer.h"
#include "ui/message_center/notification.h"

class Profile;

namespace message_center {
class MessageCenter;
class Notification;
}  // namespace message_center

namespace chromeos {

namespace tether {

// Produces notifications associated with CrOS tether network events and alerts
// observers about interactions with those notifications.
class TetherNotificationPresenter
    : public NotificationPresenter,
      public message_center::MessageCenterObserver {
 public:
  // Caller must ensure that |profile|, |message_center|, and |network_connect|
  // outlive this instance.
  TetherNotificationPresenter(Profile* profile,
                              message_center::MessageCenter* message_center,
                              NetworkConnect* network_connect);
  ~TetherNotificationPresenter() override;

  // NotificationPresenter:
  void NotifyPotentialHotspotNearby(
      const cryptauth::RemoteDevice& remote_device,
      int signal_strength) override;
  void NotifyMultiplePotentialHotspotsNearby() override;
  void RemovePotentialHotspotNotification() override;
  void NotifySetupRequired(const std::string& device_name) override;
  void RemoveSetupRequiredNotification() override;
  void NotifyConnectionToHostFailed() override;
  void RemoveConnectionToHostFailedNotification() override;
  void NotifyEnableBluetooth() override;
  void RemoveEnableBluetoothNotification() override;

  // message_center::MessageCenterObserver:
  void OnNotificationClicked(const std::string& notification_id) override;
  void OnNotificationButtonClicked(const std::string& notification_id,
                                   int button_index) override;

  class SettingsUiDelegate {
   public:
    virtual ~SettingsUiDelegate() {}

    // Displays the settings page (opening a new window if necessary) at the
    // provided subpage for the user with the Profile |profile|.
    virtual void ShowSettingsSubPageForProfile(Profile* profile,
                                               const std::string& sub_page) = 0;
  };

 private:
  // IDs associated with Tether notification types.
  static const char kTetherNotifierId[];
  static const char kPotentialHotspotNotificationId[];
  static const char kActiveHostNotificationId[];
  static const char kSetupRequiredNotificationId[];
  static const char kEnableBluetoothNotificationId[];

  // IDs of all notifications which, when clicked, open mobile data settings.
  static const char* const kIdsWhichOpenTetherSettingsOnClick[];

  static std::unique_ptr<message_center::Notification>
  CreateNotificationWithMediumSignalStrengthIcon(const std::string& id,
                                                 const base::string16& title,
                                                 const base::string16& message);
  static std::unique_ptr<message_center::Notification> CreateNotification(
      const std::string& id,
      const base::string16& title,
      const base::string16& message,
      const message_center::RichNotificationData rich_notification_data,
      int signal_strength);

  friend class TetherNotificationPresenterTest;

  void SetSettingsUiDelegateForTesting(
      std::unique_ptr<SettingsUiDelegate> settings_ui_delegate);
  void ShowNotification(
      std::unique_ptr<message_center::Notification> notification);
  void OpenSettingsAndRemoveNotification(const std::string& settings_subpage,
                                         const std::string& notification_id);
  void RemoveNotificationIfVisible(const std::string& notification_id);

  Profile* profile_;
  message_center::MessageCenter* message_center_;
  NetworkConnect* network_connect_;

  std::unique_ptr<SettingsUiDelegate> settings_ui_delegate_;

  cryptauth::RemoteDevice hotspot_nearby_device_;
  base::WeakPtrFactory<TetherNotificationPresenter> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(TetherNotificationPresenter);
};

}  // namespace tether

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_NET_TETHER_NOTIFICATION_PRESENTER_H_
