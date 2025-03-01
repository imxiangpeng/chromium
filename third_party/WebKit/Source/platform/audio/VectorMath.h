/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef VectorMath_h
#define VectorMath_h

#include <cstddef>
#include "platform/PlatformExport.h"

// Defines the interface for several vector math functions whose implementation
// will ideally be optimized.

namespace blink {
namespace VectorMath {

// Vector scalar multiply and then add.
PLATFORM_EXPORT void Vsma(const float* source_p,
                          int source_stride,
                          const float* scale,
                          float* dest_p,
                          int dest_stride,
                          size_t frames_to_process);

PLATFORM_EXPORT void Vsmul(const float* source_p,
                           int source_stride,
                           const float* scale,
                           float* dest_p,
                           int dest_stride,
                           size_t frames_to_process);
PLATFORM_EXPORT void Vadd(const float* source1p,
                          int source_stride1,
                          const float* source2p,
                          int source_stride2,
                          float* dest_p,
                          int dest_stride,
                          size_t frames_to_process);

// Finds the maximum magnitude of a float vector.
PLATFORM_EXPORT void Vmaxmgv(const float* source_p,
                             int source_stride,
                             float* max_p,
                             size_t frames_to_process);

// Sums the squares of a float vector's elements.
PLATFORM_EXPORT void Vsvesq(const float* source_p,
                            int source_stride,
                            float* sum_p,
                            size_t frames_to_process);

// For an element-by-element multiply of two float vectors.
PLATFORM_EXPORT void Vmul(const float* source1p,
                          int source_stride1,
                          const float* source2p,
                          int source_stride2,
                          float* dest_p,
                          int dest_stride,
                          size_t frames_to_process);

// Multiplies two complex vectors.
PLATFORM_EXPORT void Zvmul(const float* real1p,
                           const float* imag1p,
                           const float* real2p,
                           const float* imag2p,
                           float* real_dest_p,
                           float* imag_dest_p,
                           size_t frames_to_process);

// Copies elements while clipping values to the threshold inputs.
PLATFORM_EXPORT void Vclip(const float* source_p,
                           int source_stride,
                           const float* low_threshold_p,
                           const float* high_threshold_p,
                           float* dest_p,
                           int dest_stride,
                           size_t frames_to_process);

}  // namespace VectorMath
}  // namespace blink

#endif  // VectorMath_h
