// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/secure_channel.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/cryptauth/cryptauth_service.h"
#include "components/cryptauth/wire_message.h"
#include "components/proximity_auth/logging/logging.h"

namespace cryptauth {

// static
SecureChannel::Factory* SecureChannel::Factory::factory_instance_ = nullptr;

// static
std::unique_ptr<SecureChannel> SecureChannel::Factory::NewInstance(
    std::unique_ptr<Connection> connection,
    CryptAuthService* cryptauth_service) {
  if (!factory_instance_) {
    factory_instance_ = new Factory();
  }
  return factory_instance_->BuildInstance(std::move(connection),
                                          cryptauth_service);
}

// static
void SecureChannel::Factory::SetInstanceForTesting(Factory* factory) {
  factory_instance_ = factory;
}

std::unique_ptr<SecureChannel> SecureChannel::Factory::BuildInstance(
    std::unique_ptr<Connection> connection,
    CryptAuthService* cryptauth_service) {
  return base::WrapUnique(
      new SecureChannel(std::move(connection), cryptauth_service));
}

// static
std::string SecureChannel::StatusToString(const Status& status) {
  switch (status) {
    case Status::DISCONNECTED:
      return "[disconnected]";
    case Status::CONNECTING:
      return "[connecting]";
    case Status::CONNECTED:
      return "[connected]";
    case Status::AUTHENTICATING:
      return "[authenticating]";
    case Status::AUTHENTICATED:
      return "[authenticated]";
    default:
      return "[unknown status]";
  }
}

SecureChannel::PendingMessage::PendingMessage(const std::string& feature,
                                              const std::string& payload,
                                              int sequence_number)
    : feature(feature), payload(payload), sequence_number(sequence_number) {}

SecureChannel::PendingMessage::~PendingMessage() {}

SecureChannel::SecureChannel(std::unique_ptr<Connection> connection,
                             CryptAuthService* cryptauth_service)
    : status_(Status::DISCONNECTED),
      connection_(std::move(connection)),
      cryptauth_service_(cryptauth_service),
      weak_ptr_factory_(this) {
  DCHECK(connection_);
  DCHECK(!connection_->IsConnected());
  DCHECK(!connection_->remote_device().user_id.empty());
  DCHECK(cryptauth_service);

  connection_->AddObserver(this);
}

SecureChannel::~SecureChannel() {
  connection_->RemoveObserver(this);
}

void SecureChannel::Initialize() {
  DCHECK(status_ == Status::DISCONNECTED);
  connection_->Connect();
  TransitionToStatus(Status::CONNECTING);
}

int SecureChannel::SendMessage(const std::string& feature,
                               const std::string& payload) {
  DCHECK(status_ == Status::AUTHENTICATED);

  int sequence_number = next_sequence_number_;
  next_sequence_number_++;

  queued_messages_.push_back(PendingMessage(feature, payload, sequence_number));
  ProcessMessageQueue();

  return sequence_number;
}

void SecureChannel::Disconnect() {
  if (connection_->IsConnected()) {
    connection_->Disconnect();
  }

  TransitionToStatus(Status::DISCONNECTED);
}

void SecureChannel::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void SecureChannel::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void SecureChannel::OnConnectionStatusChanged(
    Connection* connection,
    Connection::Status old_status,
    Connection::Status new_status) {
  DCHECK(connection == connection_.get());

  if (new_status == Connection::Status::CONNECTED) {
    TransitionToStatus(Status::CONNECTED);

    // Once the connection has succeeded, authenticate the connection by
    // initiating the security handshake.
    Authenticate();
    return;
  }

  if (new_status == Connection::Status::DISCONNECTED) {
    // If the connection is no longer active, disconnect.
    Disconnect();
    return;
  }
}

void SecureChannel::OnMessageReceived(const Connection& connection,
                                      const WireMessage& wire_message) {
  DCHECK(&connection == const_cast<const Connection*>(connection_.get()));
  if (wire_message.feature() == Authenticator::kAuthenticationFeature) {
    // If the message received was part of the authentication handshake, it
    // is a low-level message and should not be forwarded to observers.
    return;
  }

  secure_context_->Decode(wire_message.payload(),
                          base::Bind(&SecureChannel::OnMessageDecoded,
                                     weak_ptr_factory_.GetWeakPtr(),
                                     wire_message.feature()));
}

void SecureChannel::OnSendCompleted(const cryptauth::Connection& connection,
                                    const cryptauth::WireMessage& wire_message,
                                    bool success) {
  DCHECK(pending_message_->feature == wire_message.feature());

  if (success && status_ != Status::DISCONNECTED) {
    pending_message_.reset();

    for (auto& observer : observer_list_)
      observer.OnMessageSent(this, wire_message.sequence_number());

    ProcessMessageQueue();
    return;
  }

  PA_LOG(ERROR) << "Could not send message: {"
                << "payload: \"" << pending_message_->payload << "\", "
                << "feature: \"" << pending_message_->feature << "\""
                << "}";
  pending_message_.reset();

  // The connection automatically retries failed messges, so if |success| is
  // |false| here, a fatal error has occurred. Thus, there is no need to retry
  // the message; instead, disconnect.
  Disconnect();
}

void SecureChannel::TransitionToStatus(const Status& new_status) {
  if (new_status == status_) {
    // Only report changes to state.
    return;
  }

  Status old_status = status_;
  status_ = new_status;

  for (auto& observer : observer_list_)
    observer.OnSecureChannelStatusChanged(this, old_status, status_);
}

void SecureChannel::Authenticate() {
  DCHECK(status_ == Status::CONNECTED);
  DCHECK(!authenticator_);

  authenticator_ = DeviceToDeviceAuthenticator::Factory::NewInstance(
      connection_.get(), connection_->remote_device().user_id,
      cryptauth_service_->CreateSecureMessageDelegate());
  authenticator_->Authenticate(
      base::Bind(&SecureChannel::OnAuthenticationResult,
                 weak_ptr_factory_.GetWeakPtr()));

  TransitionToStatus(Status::AUTHENTICATING);
}

void SecureChannel::ProcessMessageQueue() {
  if (pending_message_ || queued_messages_.empty())  {
    return;
  }

  DCHECK(!connection_->is_sending_message());

  pending_message_.reset(new PendingMessage(queued_messages_.front()));
  queued_messages_.pop_front();

  PA_LOG(INFO) << "Sending message to " << connection_->GetDeviceAddress()
               << ": {"
               << "feature: \"" << pending_message_->feature << "\", "
               << "payload: \"" << pending_message_->payload << "\""
               << "}";

  secure_context_->Encode(
      pending_message_->payload,
      base::Bind(&SecureChannel::OnMessageEncoded,
                 weak_ptr_factory_.GetWeakPtr(), pending_message_->feature,
                 pending_message_->sequence_number));
}

void SecureChannel::OnMessageEncoded(const std::string& feature,
                                     int sequence_number,
                                     const std::string& encoded_message) {
  connection_->SendMessage(base::MakeUnique<cryptauth::WireMessage>(
      encoded_message, feature, sequence_number));
}

void SecureChannel::OnMessageDecoded(
    const std::string& feature, const std::string& decoded_message) {
  PA_LOG(INFO) << "Received message from " << connection_->GetDeviceAddress()
               << ": {"
               << "feature: \"" << feature << "\", "
               << "payload: \"" << decoded_message << "\""
               << "}";

  for (auto& observer : observer_list_)
    observer.OnMessageReceived(this, feature, decoded_message);
}

void SecureChannel::OnAuthenticationResult(
    Authenticator::Result result,
    std::unique_ptr<SecureContext> secure_context) {
  DCHECK(status_ == Status::AUTHENTICATING);

  // The authenticator is no longer needed, so release it.
  authenticator_.reset();

  if (result != Authenticator::Result::SUCCESS) {
    PA_LOG(WARNING) << "Failed to authenticate connection to device with ID "
        << connection_->remote_device().GetTruncatedDeviceIdForLogs();
    Disconnect();
    return;
  }

  secure_context_ = std::move(secure_context);
  TransitionToStatus(Status::AUTHENTICATED);
}

}  // namespace cryptauth
