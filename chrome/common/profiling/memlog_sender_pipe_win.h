// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_PROFILING_MEMLOG_SENDER_PIPE_WIN_H_
#define CHROME_COMMON_PROFILING_MEMLOG_SENDER_PIPE_WIN_H_

#include <windows.h>

#include <string>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

namespace profiling {

class MemlogSenderPipe {
 public:
  explicit MemlogSenderPipe(mojo::edk::ScopedPlatformHandle handle);
  ~MemlogSenderPipe();

  bool Send(const void* data, size_t sz);

 private:
  mojo::edk::ScopedPlatformHandle handle_;

  DISALLOW_COPY_AND_ASSIGN(MemlogSenderPipe);
};

}  // namespace profiling

#endif  // CHROME_COMMON_PROFILING_MEMLOG_SENDER_PIPE_WIN_H_
