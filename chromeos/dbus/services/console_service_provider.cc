// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/services/console_service_provider.h"

#include <utility>

#include "base/bind.h"
#include "dbus/message.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {
namespace {

void OnDisplayOwnershipChanged(
    const dbus::ExportedObject::ResponseSender& response_sender,
    std::unique_ptr<dbus::Response> response,
    bool status) {
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(status);
  response_sender.Run(std::move(response));
}

}  // namespace

ConsoleServiceProvider::ConsoleServiceProvider(
    const std::string& service_interface,
    Delegate* delegate)
    : service_interface_(service_interface),
      delegate_(delegate),
      weak_ptr_factory_(this) {}

ConsoleServiceProvider::~ConsoleServiceProvider() {
}

void ConsoleServiceProvider::Start(
    scoped_refptr<dbus::ExportedObject> exported_object) {
  // TODO(lannm): Remove this once methods are removed from LibCrosService.
  bool use_libcros_methods = (service_interface_ == kLibCrosServiceInterface);

  exported_object->ExportMethod(
      service_interface_,
      (use_libcros_methods ? kTakeDisplayOwnership
                           : kDisplayServiceTakeOwnershipMethod),
      base::Bind(&ConsoleServiceProvider::TakeDisplayOwnership,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ConsoleServiceProvider::OnExported,
                 weak_ptr_factory_.GetWeakPtr()));

  exported_object->ExportMethod(
      service_interface_,
      (use_libcros_methods ? kReleaseDisplayOwnership
                           : kDisplayServiceReleaseOwnershipMethod),
      base::Bind(&ConsoleServiceProvider::ReleaseDisplayOwnership,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ConsoleServiceProvider::OnExported,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ConsoleServiceProvider::TakeDisplayOwnership(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  delegate_->TakeDisplayOwnership(
      base::Bind(&OnDisplayOwnershipChanged, response_sender,
                 base::Passed(dbus::Response::FromMethodCall(method_call))));
}

void ConsoleServiceProvider::ReleaseDisplayOwnership(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  delegate_->ReleaseDisplayOwnership(
      base::Bind(&OnDisplayOwnershipChanged, response_sender,
                 base::Passed(dbus::Response::FromMethodCall(method_call))));
}

void ConsoleServiceProvider::OnExported(const std::string& interface_name,
                                        const std::string& method_name,
                                        bool success) {
  if (!success)
    LOG(ERROR) << "failed to export " << interface_name << "." << method_name;
}

}  // namespace chromeos
