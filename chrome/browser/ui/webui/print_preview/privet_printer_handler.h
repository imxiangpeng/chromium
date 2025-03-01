// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRIVET_PRINTER_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRIVET_PRINTER_HANDLER_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "chrome/browser/local_discovery/service_discovery_shared_client.h"
#include "chrome/browser/printing/cloud_print/privet_local_printer_lister.h"
#include "chrome/browser/ui/webui/print_preview/printer_handler.h"

namespace base {
class DictionaryValue;
class OneShotTimer;
class RefCountedBytes;
}  // namespace base

namespace gfx {
class Size;
}

// Implementation of PrinterHandler interface
class PrivetPrinterHandler
    : public PrinterHandler,
      public cloud_print::PrivetLocalPrinterLister::Delegate,
      public cloud_print::PrivetLocalPrintOperation::Delegate {
 public:
  explicit PrivetPrinterHandler(Profile* profile);

  ~PrivetPrinterHandler() override;

  // PrinterHandler implementation:
  void Reset() override;
  void StartGetPrinters(
      const PrinterHandler::GetPrintersCallback& callback) override;
  void StartGetCapability(
      const std::string& destination_id,
      const PrinterHandler::GetCapabilityCallback& calback) override;
  // TODO(tbarzic): It might make sense to have the strings in a single struct.
  void StartPrint(const std::string& destination_id,
                  const std::string& capability,
                  const base::string16& job_title,
                  const std::string& ticket_json,
                  const gfx::Size& page_size,
                  const scoped_refptr<base::RefCountedBytes>& print_data,
                  const PrinterHandler::PrintCallback& callback) override;
  void StartGrantPrinterAccess(
      const std::string& printer_id,
      const PrinterHandler::GetPrinterInfoCallback& callback) override;

  // PrivetLocalPrinterLister::Delegate implementation.
  void LocalPrinterChanged(
      const std::string& name,
      bool has_local_printing,
      const cloud_print::DeviceDescription& description) override;
  void LocalPrinterRemoved(const std::string& name) override;
  void LocalPrinterCacheFlushed() override;

  // PrivetLocalPrintOperation::Delegate implementation.
  void OnPrivetPrintingDone(
      const cloud_print::PrivetLocalPrintOperation* print_operation) override;
  void OnPrivetPrintingError(
      const cloud_print::PrivetLocalPrintOperation* print_operation,
      int http_code) override;

 private:
  void StartLister(
      const scoped_refptr<local_discovery::ServiceDiscoverySharedClient>&
          client);
  void StopLister();
  void CapabilitiesUpdateClient(
      const PrinterHandler::GetCapabilityCallback& callback,
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  void OnGotCapabilities(const PrinterHandler::GetCapabilityCallback& callback,
                         const base::DictionaryValue* capabilities);
  void PrintUpdateClient(
      const PrinterHandler::PrintCallback& callback,
      const base::string16& job_title,
      const scoped_refptr<base::RefCountedBytes>& print_data,
      const std::string& print_ticket,
      const std::string& capabilities,
      const gfx::Size& page_size,
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  bool UpdateClient(std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  void StartPrint(const base::string16& job_title,
                  const scoped_refptr<base::RefCountedBytes>& print_data,
                  const std::string& print_ticket,
                  const std::string& capabilities,
                  const gfx::Size& page_size);
  bool CreateHTTP(
      const std::string& name,
      const cloud_print::PrivetHTTPAsynchronousFactory::ResultCallback&
          callback);

  Profile* profile_;
  scoped_refptr<local_discovery::ServiceDiscoverySharedClient>
      service_discovery_client_;
  std::unique_ptr<cloud_print::PrivetLocalPrinterLister> printer_lister_;
  std::unique_ptr<base::OneShotTimer> privet_lister_timer_;
  std::unique_ptr<cloud_print::PrivetHTTPAsynchronousFactory>
      privet_http_factory_;
  std::unique_ptr<cloud_print::PrivetHTTPResolution> privet_http_resolution_;
  std::unique_ptr<cloud_print::PrivetV1HTTPClient> privet_http_client_;
  std::unique_ptr<cloud_print::PrivetJSONOperation>
      privet_capabilities_operation_;
  std::unique_ptr<cloud_print::PrivetLocalPrintOperation>
      privet_local_print_operation_;
  PrinterHandler::GetPrintersCallback get_printers_callback_;
  PrinterHandler::PrintCallback print_callback_;
  base::WeakPtrFactory<PrivetPrinterHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PrivetPrinterHandler);
};
#endif  // CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRIVET_PRINTER_HANDLER_H_
