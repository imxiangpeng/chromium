// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/serial/serial_io_handler.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_traits.h"
#include "build/build_config.h"

#if defined(OS_CHROMEOS)
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/permission_broker_client.h"
#endif  // defined(OS_CHROMEOS)

namespace device {

SerialIoHandler::SerialIoHandler(
    scoped_refptr<base::SingleThreadTaskRunner> ui_thread_task_runner)
    : ui_thread_task_runner_(ui_thread_task_runner) {
  options_.bitrate = 9600;
  options_.data_bits = mojom::SerialDataBits::EIGHT;
  options_.parity_bit = mojom::SerialParityBit::NO_PARITY;
  options_.stop_bits = mojom::SerialStopBits::ONE;
  options_.cts_flow_control = false;
  options_.has_cts_flow_control = true;
}

SerialIoHandler::~SerialIoHandler() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Close();
}

void SerialIoHandler::Open(const std::string& port,
                           const mojom::SerialConnectionOptions& options,
                           const OpenCompleteCallback& callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(open_complete_.is_null());
  open_complete_ = callback;
  DCHECK(ui_thread_task_runner_.get());
  MergeConnectionOptions(options);
  port_ = port;

#if defined(OS_CHROMEOS)
  chromeos::PermissionBrokerClient* client =
      chromeos::DBusThreadManager::Get()->GetPermissionBrokerClient();
  DCHECK(client) << "Could not get permission_broker client.";
  // PermissionBrokerClient should be called on the UI thread.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      base::ThreadTaskRunnerHandle::Get();
  ui_thread_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &chromeos::PermissionBrokerClient::OpenPath, base::Unretained(client),
          port, base::Bind(&SerialIoHandler::OnPathOpened, this, task_runner),
          base::Bind(&SerialIoHandler::OnPathOpenError, this, task_runner)));
#else
  base::PostTaskWithTraits(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::Bind(&SerialIoHandler::StartOpen, this, port,
                 base::ThreadTaskRunnerHandle::Get()));
#endif  // defined(OS_CHROMEOS)
}

#if defined(OS_CHROMEOS)

void SerialIoHandler::OnPathOpened(
    scoped_refptr<base::SingleThreadTaskRunner> io_thread_task_runner,
    base::ScopedFD fd) {
  base::File file(fd.release());
  io_thread_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&SerialIoHandler::FinishOpen, this, base::Passed(&file)));
}

void SerialIoHandler::OnPathOpenError(
    scoped_refptr<base::SingleThreadTaskRunner> io_thread_task_runner,
    const std::string& error_name,
    const std::string& error_message) {
  io_thread_task_runner->PostTask(
      FROM_HERE, base::Bind(&SerialIoHandler::ReportPathOpenError, this,
                            error_name, error_message));
}

void SerialIoHandler::ReportPathOpenError(const std::string& error_name,
                                          const std::string& error_message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!open_complete_.is_null());
  LOG(ERROR) << "Permission broker failed to open '" << port_
             << "': " << error_name << ": " << error_message;
  OpenCompleteCallback callback = open_complete_;
  open_complete_.Reset();
  callback.Run(false);
}

#endif

void SerialIoHandler::MergeConnectionOptions(
    const mojom::SerialConnectionOptions& options) {
  if (options.bitrate) {
    options_.bitrate = options.bitrate;
  }
  if (options.data_bits != mojom::SerialDataBits::NONE) {
    options_.data_bits = options.data_bits;
  }
  if (options.parity_bit != mojom::SerialParityBit::NONE) {
    options_.parity_bit = options.parity_bit;
  }
  if (options.stop_bits != mojom::SerialStopBits::NONE) {
    options_.stop_bits = options.stop_bits;
  }
  if (options.has_cts_flow_control) {
    DCHECK(options_.has_cts_flow_control);
    options_.cts_flow_control = options.cts_flow_control;
  }
}

void SerialIoHandler::StartOpen(
    const std::string& port,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner) {
  DCHECK(!open_complete_.is_null());
  DCHECK(!file_.IsValid());
  // It's the responsibility of the API wrapper around SerialIoHandler to
  // validate the supplied path against the set of valid port names, and
  // it is a reasonable assumption that serial port names are ASCII.
  DCHECK(base::IsStringASCII(port));
  base::FilePath path(base::FilePath::FromUTF8Unsafe(MaybeFixUpPortName(port)));
  int flags = base::File::FLAG_OPEN | base::File::FLAG_READ |
              base::File::FLAG_EXCLUSIVE_READ | base::File::FLAG_WRITE |
              base::File::FLAG_EXCLUSIVE_WRITE | base::File::FLAG_ASYNC |
              base::File::FLAG_TERMINAL_DEVICE;
  base::File file(path, flags);
  io_task_runner->PostTask(FROM_HERE, base::Bind(&SerialIoHandler::FinishOpen,
                                                 this, base::Passed(&file)));
}

void SerialIoHandler::FinishOpen(base::File file) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!open_complete_.is_null());
  OpenCompleteCallback callback = open_complete_;
  open_complete_.Reset();

  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open serial port: "
               << base::File::ErrorToString(file.error_details());
    callback.Run(false);
    return;
  }

  file_ = std::move(file);

  bool success = PostOpen() && ConfigurePortImpl();
  if (!success)
    Close();

  callback.Run(success);
}

bool SerialIoHandler::PostOpen() {
  return true;
}

void SerialIoHandler::Close() {
  if (file_.IsValid()) {
    base::PostTaskWithTraits(
        FROM_HERE,
        {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::Bind(&SerialIoHandler::DoClose, Passed(std::move(file_))));
  }
}

// static
void SerialIoHandler::DoClose(base::File port) {
  // port closed by destructor.
}

void SerialIoHandler::Read(std::unique_ptr<WritableBuffer> buffer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!IsReadPending());
  pending_read_buffer_ = std::move(buffer);
  read_canceled_ = false;
  AddRef();
  ReadImpl();
}

void SerialIoHandler::Write(std::unique_ptr<ReadOnlyBuffer> buffer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!IsWritePending());
  pending_write_buffer_ = std::move(buffer);
  write_canceled_ = false;
  AddRef();
  WriteImpl();
}

void SerialIoHandler::ReadCompleted(int bytes_read,
                                    mojom::SerialReceiveError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(IsReadPending());
  std::unique_ptr<WritableBuffer> pending_read_buffer =
      std::move(pending_read_buffer_);
  if (error == mojom::SerialReceiveError::NONE) {
    pending_read_buffer->Done(bytes_read);
  } else {
    pending_read_buffer->DoneWithError(bytes_read, static_cast<int32_t>(error));
  }
  Release();
}

void SerialIoHandler::WriteCompleted(int bytes_written,
                                     mojom::SerialSendError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(IsWritePending());
  std::unique_ptr<ReadOnlyBuffer> pending_write_buffer =
      std::move(pending_write_buffer_);
  if (error == mojom::SerialSendError::NONE) {
    pending_write_buffer->Done(bytes_written);
  } else {
    pending_write_buffer->DoneWithError(bytes_written,
                                        static_cast<int32_t>(error));
  }
  Release();
}

bool SerialIoHandler::IsReadPending() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_read_buffer_ != NULL;
}

bool SerialIoHandler::IsWritePending() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_write_buffer_ != NULL;
}

void SerialIoHandler::CancelRead(mojom::SerialReceiveError reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsReadPending() && !read_canceled_) {
    read_canceled_ = true;
    read_cancel_reason_ = reason;
    CancelReadImpl();
  }
}

void SerialIoHandler::CancelWrite(mojom::SerialSendError reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsWritePending() && !write_canceled_) {
    write_canceled_ = true;
    write_cancel_reason_ = reason;
    CancelWriteImpl();
  }
}

bool SerialIoHandler::ConfigurePort(
    const mojom::SerialConnectionOptions& options) {
  MergeConnectionOptions(options);
  return ConfigurePortImpl();
}

void SerialIoHandler::QueueReadCompleted(int bytes_read,
                                         mojom::SerialReceiveError error) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&SerialIoHandler::ReadCompleted, this, bytes_read, error));
}

void SerialIoHandler::QueueWriteCompleted(int bytes_written,
                                          mojom::SerialSendError error) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&SerialIoHandler::WriteCompleted, this, bytes_written, error));
}

}  // namespace device
