// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_HID_HID_SERVICE_MAC_H_
#define DEVICE_HID_HID_SERVICE_MAC_H_

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDDevice.h>

#include <string>

#include "base/mac/foundation_util.h"
#include "base/mac/scoped_ionotificationportref.h"
#include "base/mac/scoped_ioobject.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "device/hid/hid_service.h"

namespace device {

class HidServiceMac : public HidService {
 public:
  HidServiceMac();
  ~HidServiceMac() override;

  void Connect(const std::string& device_id,
               const ConnectCallback& connect) override;

 private:
  static base::ScopedCFTypeRef<IOHIDDeviceRef> OpenOnBlockingThread(
      scoped_refptr<HidDeviceInfo> device_info);
  void DeviceOpened(scoped_refptr<HidDeviceInfo> device_info,
                    const ConnectCallback& callback,
                    base::ScopedCFTypeRef<IOHIDDeviceRef> hid_device);

  // IOService matching callbacks.
  static void FirstMatchCallback(void* context, io_iterator_t iterator);
  static void TerminatedCallback(void* context, io_iterator_t iterator);

  void AddDevices();
  void RemoveDevices();

  // Platform notification port.
  base::mac::ScopedIONotificationPortRef notify_port_;
  base::mac::ScopedIOObject<io_iterator_t> devices_added_iterator_;
  base::mac::ScopedIOObject<io_iterator_t> devices_removed_iterator_;

  base::WeakPtrFactory<HidServiceMac> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(HidServiceMac);
};

}  // namespace device

#endif  // DEVICE_HID_HID_SERVICE_MAC_H_
