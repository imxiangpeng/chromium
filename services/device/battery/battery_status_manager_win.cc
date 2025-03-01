// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/battery/battery_status_manager_win.h"

#include <memory>

#include "base/bind.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string16.h"
#include "base/win/message_window.h"
#include "services/device/battery/battery_status_manager.h"

namespace device {

namespace {

typedef BatteryStatusService::BatteryUpdateCallback BatteryCallback;

const wchar_t kWindowClassName[] = L"BatteryStatusMessageWindow";

// This enum is used for histogram. Don't change the order of the existing
// values.
enum NumberBatteriesType {
  UNKNOWN_BATTERIES = 0,
  NO_BATTERY = 1,
  ONE_OR_MORE_BATTERIES = 2,
  BATTERY_TYPES_COUNT = 3,
};

void UpdateNumberBatteriesHistogram(NumberBatteriesType count) {
  UMA_HISTOGRAM_ENUMERATION("BatteryStatus.NumberBatteriesWin", count,
                            BATTERY_TYPES_COUNT);
}

void UpdateNumberBatteriesHistogram() {
  SYSTEM_POWER_STATUS win_status;
  if (!GetSystemPowerStatus(&win_status)) {
    UpdateNumberBatteriesHistogram(UNKNOWN_BATTERIES);
    return;
  }

  if (win_status.BatteryFlag == 255)
    UpdateNumberBatteriesHistogram(UNKNOWN_BATTERIES);
  else if (win_status.BatteryFlag == 128)
    UpdateNumberBatteriesHistogram(NO_BATTERY);
  else
    UpdateNumberBatteriesHistogram(ONE_OR_MORE_BATTERIES);
}

// Message-only window for handling battery changes on Windows.
class BatteryStatusObserver {
 public:
  explicit BatteryStatusObserver(const BatteryCallback& callback)
      : power_handle_(NULL),
        battery_change_handle_(NULL),
        callback_(callback) {}

  ~BatteryStatusObserver() { DCHECK(!window_); }

  void Start() {
    if (CreateMessageWindow()) {
      BatteryChanged();
      // RegisterPowerSettingNotification function work from Windows Vista
      // onwards. However even without them we will receive notifications,
      // e.g. when a power source is connected.
      // TODO(timvolodine) : consider polling for battery changes on windows
      // versions prior to Vista, see crbug.com/402466.
      power_handle_ = RegisterNotification(&GUID_ACDC_POWER_SOURCE);
      battery_change_handle_ =
          RegisterNotification(&GUID_BATTERY_PERCENTAGE_REMAINING);
    } else {
      // Could not create a message window, execute callback with the default
      // values.
      callback_.Run(mojom::BatteryStatus());
    }

    UpdateNumberBatteriesHistogram();
  }

  void Stop() {
    if (power_handle_) {
      UnregisterNotification(power_handle_);
      power_handle_ = NULL;
    }
    if (battery_change_handle_) {
      UnregisterNotification(battery_change_handle_);
      battery_change_handle_ = NULL;
    }
    window_.reset();
  }

 private:
  void BatteryChanged() {
    SYSTEM_POWER_STATUS win_status;
    if (GetSystemPowerStatus(&win_status))
      callback_.Run(ComputeWebBatteryStatus(win_status));
    else
      callback_.Run(mojom::BatteryStatus());
  }

  bool HandleMessage(UINT message,
                     WPARAM wparam,
                     LPARAM lparam,
                     LRESULT* result) {
    switch (message) {
      case WM_POWERBROADCAST:
        if (wparam == PBT_APMPOWERSTATUSCHANGE ||
            wparam == PBT_POWERSETTINGCHANGE) {
          BatteryChanged();
        }
        *result = NULL;
        return true;
      default:
        return false;
    }
  }

  HPOWERNOTIFY RegisterNotification(LPCGUID power_setting) {
    return RegisterPowerSettingNotification(window_->hwnd(), power_setting,
                                            DEVICE_NOTIFY_WINDOW_HANDLE);
  }

  BOOL UnregisterNotification(HPOWERNOTIFY handle) {
    return UnregisterPowerSettingNotification(handle);
  }

  bool CreateMessageWindow() {
    // TODO(timvolodine): consider reusing the message window of PowerMonitor.
    window_.reset(new base::win::MessageWindow());
    if (!window_->CreateNamed(base::Bind(&BatteryStatusObserver::HandleMessage,
                                         base::Unretained(this)),
                              base::string16(kWindowClassName))) {
      LOG(ERROR) << "Failed to create message window: " << kWindowClassName;
      window_.reset();
      return false;
    }
    return true;
  }

  HPOWERNOTIFY power_handle_;
  HPOWERNOTIFY battery_change_handle_;
  BatteryCallback callback_;
  std::unique_ptr<base::win::MessageWindow> window_;

  DISALLOW_COPY_AND_ASSIGN(BatteryStatusObserver);
};

class BatteryStatusManagerWin : public BatteryStatusManager {
 public:
  explicit BatteryStatusManagerWin(const BatteryCallback& callback)
      : battery_observer_(new BatteryStatusObserver(callback)) {}
  ~BatteryStatusManagerWin() override { battery_observer_->Stop(); }

 public:
  // BatteryStatusManager:
  bool StartListeningBatteryChange() override {
    battery_observer_->Start();
    return true;
  }

  void StopListeningBatteryChange() override { battery_observer_->Stop(); }

 private:
  std::unique_ptr<BatteryStatusObserver> battery_observer_;

  DISALLOW_COPY_AND_ASSIGN(BatteryStatusManagerWin);
};

}  // namespace

mojom::BatteryStatus ComputeWebBatteryStatus(
    const SYSTEM_POWER_STATUS& win_status) {
  mojom::BatteryStatus status;
  status.charging = win_status.ACLineStatus != WIN_AC_LINE_STATUS_OFFLINE;

  // Set level if available. Otherwise keep the default value which is 1.
  if (win_status.BatteryLifePercent != 255) {
    // Convert percentage to a value between 0 and 1 with 2 significant digits.
    status.level = static_cast<double>(win_status.BatteryLifePercent) / 100.;
  }

  if (!status.charging) {
    // Set discharging_time if available otherwise keep the default value,
    // which is +Infinity.
    if (win_status.BatteryLifeTime != (DWORD)-1)
      status.discharging_time = win_status.BatteryLifeTime;
    status.charging_time = std::numeric_limits<double>::infinity();
  } else {
    // Set charging_time to +Infinity if not fully charged, otherwise leave the
    // default value, which is 0.
    if (status.level < 1)
      status.charging_time = std::numeric_limits<double>::infinity();
  }
  return status;
}

// static
std::unique_ptr<BatteryStatusManager> BatteryStatusManager::Create(
    const BatteryStatusService::BatteryUpdateCallback& callback) {
  return std::unique_ptr<BatteryStatusManager>(
      new BatteryStatusManagerWin(callback));
}

}  // namespace device
