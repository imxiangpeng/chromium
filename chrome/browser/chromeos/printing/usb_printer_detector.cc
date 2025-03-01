// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/observer_list_threadsafe.h"
#include "base/scoped_observer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/printing/ppd_provider_factory.h"
#include "chrome/browser/chromeos/printing/printer_configurer.h"
#include "chrome/browser/chromeos/printing/synced_printers_manager_factory.h"
#include "chrome/browser/chromeos/printing/usb_printer_detector.h"
#include "chrome/browser/chromeos/printing/usb_printer_util.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/debug_daemon_client.h"
#include "chromeos/printing/ppd_provider.h"
#include "content/public/browser/browser_thread.h"
#include "device/base/device_client.h"
#include "device/usb/usb_device.h"
#include "device/usb/usb_service.h"

namespace chromeos {
namespace {

// Aggregates the information needed for printer setup so it's easier to pass it
// around.
struct SetUpPrinterData {
  // The configurer running the SetUpPrinter call.
  std::unique_ptr<PrinterConfigurer> configurer;

  // The printer being set up.
  std::unique_ptr<Printer> printer;

  // Additional metadata for driver finding.
  PpdProvider::PrinterSearchData ppd_search_data;

  // The usb device causing this setup flow.
  scoped_refptr<device::UsbDevice> device;

  // True if this printer is one that the user doesn't already have a
  // configuration for.
  bool is_new;

  // True if this was a printer that was plugged in during the session, false if
  // it was already plugged in when the session started.
  bool hotplugged;
};

// Given a usb device, guesses the make and model for a driver lookup.
//
// TODO(justincarlson): Possibly go deeper and query the IEEE1284 fields
// for make and model if we determine those are more likely to contain
// what we want.
std::string GuessEffectiveMakeAndModel(const device::UsbDevice& device) {
  return base::UTF16ToUTF8(device.manufacturer_string()) + " " +
         base::UTF16ToUTF8(device.product_string());
}

// The PrinterDetector that drives the flow for setting up a USB printer to use
// CUPS backend.
class UsbPrinterDetectorImpl : public UsbPrinterDetector,
                               public device::UsbService::Observer {
 public:
  explicit UsbPrinterDetectorImpl(Profile* profile)
      : profile_(profile),
        usb_observer_(this),
        observer_list_(
            new base::ObserverListThreadSafe<UsbPrinterDetector::Observer>),
        weak_ptr_factory_(this) {
    device::UsbService* usb_service =
        device::DeviceClient::Get()->GetUsbService();
    if (usb_service) {
      usb_observer_.Add(usb_service);
      usb_service->GetDevices(base::Bind(&UsbPrinterDetectorImpl::OnGetDevices,
                                         weak_ptr_factory_.GetWeakPtr()));
    }
  }
  ~UsbPrinterDetectorImpl() override = default;

  // PrinterDetector interface function.
  void AddObserver(UsbPrinterDetector::Observer* observer) override {
    observer_list_->AddObserver(observer);
  }

  // PrinterDetector interface function.
  void RemoveObserver(UsbPrinterDetector::Observer* observer) override {
    observer_list_->RemoveObserver(observer);
  }

  // PrinterDetector interface function.
  std::vector<DetectedPrinter> GetPrinters() override {
    base::AutoLock auto_lock(pp_lock_);
    return GetPrintersLocked();
  }

  void Start() override {
    started_ = true;
    observer_list_->Notify(FROM_HERE,
                           &PrinterDetector::Observer::OnPrintersFound,
                           GetPrintersLocked());
    observer_list_->Notify(FROM_HERE,
                           &PrinterDetector::Observer::OnPrinterScanComplete);
  }

 private:
  std::vector<DetectedPrinter> GetPrintersLocked() {
    pp_lock_.AssertAcquired();
    std::vector<DetectedPrinter> printers;
    printers.reserve(present_printers_.size());
    for (const auto& entry : present_printers_) {
      printers.push_back(*entry.second);
    }
    return printers;
  }

  // Callback for initial enumeration of usb devices.
  void OnGetDevices(
      const std::vector<scoped_refptr<device::UsbDevice>>& devices) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    for (const auto& device : devices) {
      MaybeSetUpDevice(device, false);
    }
  }

  // UsbService::observer override.
  void OnDeviceAdded(scoped_refptr<device::UsbDevice> device) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    MaybeSetUpDevice(device, true);
  }

  // UsbService::observer override.
  void OnDeviceRemoved(scoped_refptr<device::UsbDevice> device) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!UsbDeviceIsPrinter(*device)) {
      return;
    }

    base::AutoLock auto_lock(pp_lock_);
    if (base::ContainsKey(present_printers_, device->guid())) {
      present_printers_.erase(device->guid());
      if (started_) {
        auto printers = GetPrintersLocked();
        // We already have pp_lock_, so need to call the pre-locked version of
        // GetPrinters to prevent deadlock.
        observer_list_->Notify(FROM_HERE,
                               &PrinterDetector::Observer::OnPrintersFound,
                               GetPrintersLocked());
      }
    } else {
      // If the device has been removed but it's not in present_printers_, it
      // must still be in the setup flow.
      deferred_printer_removals_.insert(device->guid());
    }
  }

  // If this device is a printer and we haven't already tried to set it up,
  // starts the process of setting the printer up.  |hotplugged|
  // should be true if this was plugged in during the session.
  void MaybeSetUpDevice(scoped_refptr<device::UsbDevice> device,
                        bool hotplugged) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    if (!UsbDeviceIsPrinter(*device)) {
      return;
    }

    // If we got this far, we want to try to auto-configure this printer.
    auto data = base::MakeUnique<SetUpPrinterData>();
    data->configurer = PrinterConfigurer::Create(profile_);
    data->device = device;
    data->hotplugged = hotplugged;

    data->printer = UsbDeviceToPrinter(*device);
    if (data->printer == nullptr) {
      // We've failed to understand this printer device, an error will already
      // have been logged, so just bail.
      return;
    }

    // If the user already has a configuration for this device, substitute that
    // one for the one we generated automatically and skip the parts where we
    // try to automagically figure out the driver.
    std::unique_ptr<Printer> existing_printer_configuration =
        SyncedPrintersManagerFactory::GetForBrowserContext(profile_)
            ->GetPrinter(data->printer->id());
    if (existing_printer_configuration != nullptr) {
      data->is_new = false;
      data->printer = std::move(existing_printer_configuration);
      OnPrinterResolved(std::move(data));
      return;
    }

    // It's not a device we have configured previously.
    //
    // TODO(justincarlson): Add a notification that we are attempting to set up
    // this printer at this point.
    data->is_new = true;

    // Look for an exact match based on USB ids.
    //
    // TODO(justincarlson) -- Ppd resolution should move into the top level
    // printers manager.
    data->ppd_search_data.usb_vendor_id = device->vendor_id();
    data->ppd_search_data.usb_product_id = device->product_id();
    scoped_refptr<PpdProvider> ppd_provider = CreatePpdProvider(profile_);
    ppd_provider->ResolvePpdReference(
        data->ppd_search_data,
        base::Bind(&UsbPrinterDetectorImpl::ResolveUsbIdsDone,
                   weak_ptr_factory_.GetWeakPtr(), ppd_provider,
                   base::Passed(std::move(data))));
  }

  void OnPrinterResolved(std::unique_ptr<SetUpPrinterData> data) {
    // |data| will be invalidated by the move below, so we have to latch it
    // before the call.
    SetUpPrinterData* data_ptr = data.get();
    data_ptr->configurer->SetUpPrinter(
        *(data_ptr->printer),
        base::Bind(&UsbPrinterDetectorImpl::SetUpPrinterDone,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Passed(std::move(data))));
  }

  // Called when the query for a driver based on usb ids completes.
  //
  // Note |provider| is not used in this function, it's just passed along to
  // keep it alive during the USB resolution.
  void ResolveUsbIdsDone(scoped_refptr<PpdProvider> provider,
                         std::unique_ptr<SetUpPrinterData> data,
                         PpdProvider::CallbackResultCode result,
                         const Printer::PpdReference& ppd_reference) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (result == PpdProvider::SUCCESS) {
      // Got something based on usb ids.  Go with it.
      *data->printer->mutable_ppd_reference() = ppd_reference;
    } else {
      // Couldn't figure this printer out based on usb ids, fall back to
      // guessing the make/model string from what the USB system reports.
      //
      // TODO(justincarlson): Consider adding a mechanism for aggregating data
      // about which usb devices are in the wild but unsupported?
      data->printer->mutable_ppd_reference()->effective_make_and_model =
          GuessEffectiveMakeAndModel(*data->device);
    }
    OnPrinterResolved(std::move(data));
  }

  // Called with the result of asking to have a printer configured for CUPS.  If
  // |printer_to_register| is non-null and we successfully configured, then the
  // printer is registered with the printers manager.
  void SetUpPrinterDone(std::unique_ptr<SetUpPrinterData> data,
                        PrinterSetupResult result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (result == PrinterSetupResult::kSuccess) {
      if (data->is_new) {
        SyncedPrintersManagerFactory::GetForBrowserContext(profile_)
            ->UpdateConfiguredPrinter(*data->printer);
      }
      // TODO(justincarlson): If the device was hotplugged, pop a timed
      // notification that says the printer is now available for printing.
    }
    // TODO(justincarlson): If this doesn't succeed, pop a notification that
    // tells the user automatic setup failed and offers to open the CUPS printer
    // configuration settings.

    if (base::ContainsKey(deferred_printer_removals_, data->device->guid())) {
      // The device was removed before we finished the flow, so just don't add
      // it to present_printers_;
      deferred_printer_removals_.erase(data->device->guid());
    } else {
      auto detected_printer = base::MakeUnique<DetectedPrinter>();
      detected_printer->printer = *(data->printer);
      detected_printer->ppd_search_data = data->ppd_search_data;
      base::AutoLock auto_lock(pp_lock_);
      present_printers_.emplace(data->device->guid(),
                                std::move(detected_printer));
      if (started_) {
        observer_list_->Notify(FROM_HERE,
                               &PrinterDetector::Observer::OnPrintersFound,
                               GetPrintersLocked());
      }
    }
  }
  // Has Start() been called yet?
  bool started_ = false;

  // Map from USB GUID to Printer that we have detected as being currently
  // plugged in and have finished processing.  Note present_printers_ may be
  // accessed from multiple threads, so is protected by pp_lock_.
  std::map<std::string, std::unique_ptr<DetectedPrinter>> present_printers_;
  base::Lock pp_lock_;

  // If the usb device is removed before we've finished processing it, we'll
  // defer the cleanup until the setup flow finishes.  This is the set of
  // guids which have been removed before the flow finished.
  std::set<std::string> deferred_printer_removals_;

  Profile* profile_;
  ScopedObserver<device::UsbService, device::UsbService::Observer>
      usb_observer_;
  scoped_refptr<base::ObserverListThreadSafe<UsbPrinterDetector::Observer>>
      observer_list_;
  base::WeakPtrFactory<UsbPrinterDetectorImpl> weak_ptr_factory_;
};

}  // namespace

// static
std::unique_ptr<UsbPrinterDetector> UsbPrinterDetector::Create(
    Profile* profile) {
  return base::MakeUnique<UsbPrinterDetectorImpl>(profile);
}

}  // namespace chromeos
