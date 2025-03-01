// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/usb/usb_device_win.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/device_event_log/device_event_log.h"
#include "device/usb/usb_device_handle_win.h"

namespace device {

UsbDeviceWin::UsbDeviceWin(
    const std::string& device_path,
    const std::string& hub_path,
    int port_number,
    const std::string& driver_name,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner)
    : device_path_(device_path),
      hub_path_(hub_path),
      port_number_(port_number),
      driver_name_(driver_name),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      blocking_task_runner_(std::move(blocking_task_runner)) {}

UsbDeviceWin::~UsbDeviceWin() {}

void UsbDeviceWin::Open(OpenCallback callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  task_runner_->PostTask(FROM_HERE,
                         base::BindOnce(std::move(callback), nullptr));
}

void UsbDeviceWin::ReadDescriptors(base::OnceCallback<void(bool)> callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  scoped_refptr<UsbDeviceHandle> device_handle;
  base::win::ScopedHandle handle(
      CreateFileA(hub_path_.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  if (handle.IsValid()) {
    device_handle =
        new UsbDeviceHandleWin(this, std::move(handle), blocking_task_runner_);
  } else {
    USB_PLOG(ERROR) << "Failed to open " << hub_path_;
    std::move(callback).Run(false);
    return;
  }

  ReadUsbDescriptors(device_handle,
                     base::BindOnce(&UsbDeviceWin::OnReadDescriptors, this,
                                    std::move(callback), device_handle));
}

void UsbDeviceWin::OnReadDescriptors(
    base::OnceCallback<void(bool)> callback,
    scoped_refptr<UsbDeviceHandle> device_handle,
    std::unique_ptr<UsbDeviceDescriptor> descriptor) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!descriptor) {
    USB_LOG(ERROR) << "Failed to read descriptors from " << device_path_ << ".";
    device_handle->Close();
    std::move(callback).Run(false);
    return;
  }

  descriptor_ = *descriptor;

  auto string_map = base::MakeUnique<std::map<uint8_t, base::string16>>();
  if (descriptor_.i_manufacturer)
    (*string_map)[descriptor_.i_manufacturer] = base::string16();
  if (descriptor_.i_product)
    (*string_map)[descriptor_.i_product] = base::string16();
  if (descriptor_.i_serial_number)
    (*string_map)[descriptor_.i_serial_number] = base::string16();

  ReadUsbStringDescriptors(
      device_handle, std::move(string_map),
      base::BindOnce(&UsbDeviceWin::OnReadStringDescriptors, this,
                     std::move(callback), device_handle));
}

void UsbDeviceWin::OnReadStringDescriptors(
    base::OnceCallback<void(bool)> callback,
    scoped_refptr<UsbDeviceHandle> device_handle,
    std::unique_ptr<std::map<uint8_t, base::string16>> string_map) {
  DCHECK(thread_checker_.CalledOnValidThread());
  device_handle->Close();

  if (descriptor_.i_manufacturer)
    manufacturer_string_ = (*string_map)[descriptor_.i_manufacturer];
  if (descriptor_.i_product)
    product_string_ = (*string_map)[descriptor_.i_product];
  if (descriptor_.i_serial_number)
    serial_number_ = (*string_map)[descriptor_.i_serial_number];

  std::move(callback).Run(true);
}

}  // namespace device
