// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/serial/serial_device_enumerator_win.h"

#include <devguid.h>
#include <setupapi.h>
#include <stdint.h>
#include <windows.h>

#include <memory>
#include <unordered_set>

#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/registry.h"
#include "third_party/re2/src/re2/re2.h"

namespace device {

namespace {

// Searches the specified device info for a property with the specified key,
// assigns the result to value, and returns whether the operation was
// successful.
bool GetProperty(HDEVINFO dev_info,
                 SP_DEVINFO_DATA dev_info_data,
                 const int key,
                 std::string* value) {
  // We don't know how much space the property's value will take up, so we call
  // the property retrieval function once to fetch the size of the required
  // value buffer, then again once we've allocated a sufficiently large buffer.
  DWORD buffer_size = 0;
  SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data, key, nullptr,
                                   nullptr, buffer_size, &buffer_size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return false;

  std::unique_ptr<wchar_t[]> buffer(new wchar_t[buffer_size]);
  if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data, key, nullptr,
                                        reinterpret_cast<PBYTE>(buffer.get()),
                                        buffer_size, nullptr))
    return false;

  *value = base::WideToUTF8(buffer.get());
  return true;
}

// Searches for the COM port in the device's friendly name, assigns its value to
// com_port, and returns whether the operation was successful.
bool GetCOMPort(const std::string friendly_name, std::string* com_port) {
  return RE2::PartialMatch(friendly_name, ".* \\((COM[0-9]+)\\)", com_port);
}

// Searches for the display name in the device's friendly name, assigns its
// value to display_name, and returns whether the operation was successful.
bool GetDisplayName(const std::string friendly_name,
                    std::string* display_name) {
  return RE2::PartialMatch(friendly_name, "(.*) \\(COM[0-9]+\\)", display_name);
}

// Searches for the vendor ID in the device's hardware ID, assigns its value to
// vendor_id, and returns whether the operation was successful.
bool GetVendorID(const std::string hardware_id, uint32_t* vendor_id) {
  std::string vendor_id_str;
  return RE2::PartialMatch(hardware_id, "VID_([0-9a-fA-F]+)", &vendor_id_str) &&
         base::HexStringToUInt(vendor_id_str, vendor_id);
}

// Searches for the product ID in the device's product ID, assigns its value to
// product_id, and returns whether the operation was successful.
bool GetProductID(const std::string hardware_id, uint32_t* product_id) {
  std::string product_id_str;
  return RE2::PartialMatch(hardware_id, "PID_([0-9a-fA-F]+)",
                           &product_id_str) &&
         base::HexStringToUInt(product_id_str, product_id);
}

// Returns value clamped to the range of [min, max].
int Clamp(int value, int min, int max) {
  return std::min(std::max(value, min), max);
}

// Returns an array of devices as retrieved through the new method of
// enumerating serial devices (SetupDi).  This new method gives more information
// about the devices than the old method.
std::vector<mojom::SerialDeviceInfoPtr> GetDevicesNew() {
  std::vector<mojom::SerialDeviceInfoPtr> devices;

  // Make a device interface query to find all serial devices.
  HDEVINFO dev_info =
      SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
  if (dev_info == INVALID_HANDLE_VALUE)
    return devices;

  SP_DEVINFO_DATA dev_info_data;
  dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
    std::string friendly_name, com_port;
    // SPDRP_FRIENDLYNAME looks like "USB_SERIAL_PORT (COM3)".
    if (!GetProperty(dev_info, dev_info_data, SPDRP_FRIENDLYNAME,
                     &friendly_name) ||
        !GetCOMPort(friendly_name, &com_port))
      // In Windows, the COM port is the path used to uniquely identify the
      // serial device. If the COM can't be found, ignore the device.
      continue;

    mojom::SerialDeviceInfoPtr info(mojom::SerialDeviceInfo::New());
    info->path = com_port;

    std::string display_name;
    if (GetDisplayName(friendly_name, &display_name))
      info->display_name = std::move(display_name);

    std::string hardware_id;
    // SPDRP_HARDWAREID looks like "FTDIBUS\COMPORT&VID_0403&PID_6001".
    if (GetProperty(dev_info, dev_info_data, SPDRP_HARDWAREID, &hardware_id)) {
      uint32_t vendor_id, product_id;
      if (GetVendorID(hardware_id, &vendor_id)) {
        info->has_vendor_id = true;
        info->vendor_id = vendor_id;
      }
      if (GetProductID(hardware_id, &product_id)) {
        info->has_product_id = true;
        info->product_id = product_id;
      }
    }

    devices.push_back(std::move(info));
  }

  SetupDiDestroyDeviceInfoList(dev_info);
  return devices;
}

// Returns an array of devices as retrieved through the old method of
// enumerating serial devices (searching the registry). This old method gives
// less information about the devices than the new method.
std::vector<mojom::SerialDeviceInfoPtr> GetDevicesOld() {
  base::win::RegistryValueIterator iter_key(
      HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM\\");
  std::vector<mojom::SerialDeviceInfoPtr> devices;
  for (; iter_key.Valid(); ++iter_key) {
    mojom::SerialDeviceInfoPtr info(mojom::SerialDeviceInfo::New());
    info->path = base::UTF16ToASCII(iter_key.Value());
    devices.push_back(std::move(info));
  }
  return devices;
}

}  // namespace

// static
std::unique_ptr<SerialDeviceEnumerator> SerialDeviceEnumerator::Create() {
  return std::unique_ptr<SerialDeviceEnumerator>(
      new SerialDeviceEnumeratorWin());
}

SerialDeviceEnumeratorWin::SerialDeviceEnumeratorWin() {}

SerialDeviceEnumeratorWin::~SerialDeviceEnumeratorWin() {}

std::vector<mojom::SerialDeviceInfoPtr>
SerialDeviceEnumeratorWin::GetDevices() {
  std::vector<mojom::SerialDeviceInfoPtr> devices = GetDevicesNew();
  std::vector<mojom::SerialDeviceInfoPtr> old_devices = GetDevicesOld();

  UMA_HISTOGRAM_SPARSE_SLOWLY(
      "Hardware.Serial.NewMinusOldDeviceListSize",
      Clamp(devices.size() - old_devices.size(), -10, 10));

  // Add devices found from both the new and old methods of enumeration. If a
  // device is found using both the new and the old enumeration method, then we
  // take the device from the new enumeration method because it's able to
  // collect more information. We do this by inserting the new devices first,
  // because insertions are ignored if the key already exists.
  std::unordered_set<std::string> devices_seen;
  for (const auto& device : devices) {
    bool inserted = devices_seen.insert(device->path).second;
    DCHECK(inserted);
  }
  for (auto& device : old_devices) {
    if (devices_seen.insert(device->path).second)
      devices.push_back(std::move(device));
  }
  return devices;
}

}  // namespace device
