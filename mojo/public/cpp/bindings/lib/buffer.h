// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_BUFFER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "mojo/public/cpp/bindings/bindings_export.h"
#include "mojo/public/cpp/system/message.h"

namespace mojo {
namespace internal {

// Buffer provides an interface to allocate memory blocks which are 8-byte
// aligned. It doesn't own the underlying memory. Users must ensure that the
// memory stays valid while using the allocated blocks from Buffer.
//
// A Buffer may be moved around. A moved-from Buffer is reset and may no longer
// be used to Allocate memory unless re-Initialized.
class MOJO_CPP_BINDINGS_EXPORT Buffer {
 public:
  // Constructs an invalid Buffer. May not call Allocate().
  Buffer();

  // Constructs a Buffer which can Allocate() blocks from a buffer of fixed size
  // |size| at |data|. Allocations start at |cursor|, so if |cursor| == |size|
  // then no allocations are allowed.
  //
  // |data| is not owned.
  Buffer(void* data, size_t size, size_t cursor);

  // Like above, but gives the Buffer an underlying message object which can
  // have its payload extended to acquire more storage capacity on Allocate().
  //
  // |data| and |size| must correspond to |message|'s serialized buffer contents
  // at the time of construction.
  //
  // |message| is NOT owned and must outlive this Buffer.
  Buffer(MessageHandle message, void* data, size_t size);

  Buffer(Buffer&& other);
  ~Buffer();

  Buffer& operator=(Buffer&& other);

  void* data() const { return data_; }
  size_t size() const { return size_; }
  size_t cursor() const { return cursor_; }

  bool is_valid() const {
    return data_ != nullptr || (size_ == 0 && !message_.is_valid());
  }

  // Allocates |num_bytes| from the buffer and returns an index to the start of
  // the allocated block. The resulting index is 8-byte aligned and can be
  // resolved to an address using Get<T>() below.
  size_t Allocate(size_t num_bytes);

  // Returns a typed address within the Buffer corresponding to |index|. Note
  // that this address is NOT stable across calls to |Allocate()| and thus must
  // not be cached accordingly.
  template <typename T>
  T* Get(size_t index) {
    DCHECK_LT(index, cursor_);
    return reinterpret_cast<T*>(static_cast<uint8_t*>(data_) + index);
  }

  // A template helper combining Allocate() and Get<T>() above to allocate and
  // return a block of size |sizeof(T)|.
  template <typename T>
  T* AllocateAndGet() {
    return Get<T>(Allocate(sizeof(T)));
  }

  // A helper which combines Allocate() and Get<void>() for a specified number
  // of bytes.
  void* AllocateAndGet(size_t num_bytes) {
    return Get<void>(Allocate(num_bytes));
  }

  // Seals this Buffer so it can no longer be used for allocation, and ensures
  // the backing message object has a complete accounting of the size of the
  // meaningful payload bytes.
  void Seal();

  // Resets the buffer to an invalid state. Can no longer be used to Allocate().
  void Reset();

 private:
  MessageHandle message_;
  void* data_ = nullptr;
  size_t size_ = 0;
  size_t cursor_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Buffer);
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_BUFFER_H_
