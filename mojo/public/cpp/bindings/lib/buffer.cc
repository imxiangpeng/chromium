// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/lib/buffer.h"

#include "base/logging.h"
#include "base/numerics/safe_math.h"
#include "mojo/public/c/system/message_pipe.h"
#include "mojo/public/cpp/bindings/lib/bindings_internal.h"

namespace mojo {
namespace internal {

Buffer::Buffer() = default;

Buffer::Buffer(void* data, size_t size, size_t cursor)
    : data_(data), size_(size), cursor_(cursor) {
  DCHECK(IsAligned(data_));
}

Buffer::Buffer(MessageHandle message, void* data, size_t size)
    : message_(message), data_(data), size_(size), cursor_(0) {
  DCHECK(IsAligned(data_));
}

Buffer::Buffer(Buffer&& other) {
  *this = std::move(other);
}

Buffer::~Buffer() = default;

Buffer& Buffer::operator=(Buffer&& other) {
  message_ = other.message_;
  data_ = other.data_;
  size_ = other.size_;
  cursor_ = other.cursor_;
  other.Reset();
  return *this;
}

size_t Buffer::Allocate(size_t num_bytes) {
  const size_t aligned_num_bytes = Align(num_bytes);
  const size_t new_cursor = cursor_ + aligned_num_bytes;
  if (new_cursor < cursor_ || (new_cursor > size_ && !message_.is_valid())) {
    // Either we've overflowed or exceeded a fixed capacity.
    NOTREACHED();
    return 0;
  }

  if (new_cursor > size_) {
    // If we have an underlying message object we can extend its payload to
    // obtain more storage capacity.
    DCHECK(base::IsValueInRangeForNumericType<uint32_t>(new_cursor));
    uint32_t new_size;
    MojoResult rv = MojoExtendSerializedMessagePayload(
        message_.value(), static_cast<uint32_t>(new_cursor), &data_, &new_size);
    DCHECK_EQ(MOJO_RESULT_OK, rv);
    size_ = new_size;
  }

  DCHECK_LE(new_cursor, size_);
  size_t block_start = cursor_;
  cursor_ = new_cursor;

  // Ensure that all the allocated space is zeroed to avoid uninitialized bits
  // leaking into messages.
  //
  // TODO(rockot): We should consider only clearing the alignment padding. This
  // means being careful about generated bindings zeroing padding explicitly,
  // which itself gets particularly messy with e.g. packed bool bitfields.
  memset(static_cast<uint8_t*>(data_) + block_start, 0, aligned_num_bytes);

  return block_start;
}

void Buffer::Seal() {
  if (!message_.is_valid())
    return;

  // Ensure that the backing message has the final accumulated payload size.
  DCHECK(base::IsValueInRangeForNumericType<uint32_t>(cursor_));
  void* data;
  uint32_t size;
  MojoResult rv = MojoExtendSerializedMessagePayload(
      message_.value(), static_cast<uint32_t>(cursor_), &data, &size);
  DCHECK_EQ(MOJO_RESULT_OK, rv);

  // The buffer size should remain the same, as the final cursor position was
  // necessarily within the previous allocated payload range.
  DCHECK_EQ(size, size_);
  DCHECK_EQ(data, data_);
  message_ = MessageHandle();
}

void Buffer::Reset() {
  message_ = MessageHandle();
  data_ = nullptr;
  size_ = 0;
  cursor_ = 0;
}

}  // namespace internal
}  // namespace mojo
