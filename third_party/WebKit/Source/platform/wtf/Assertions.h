/*
 * Copyright (C) 2003, 2006, 2007 Apple Inc.  All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WTF_Assertions_h
#define WTF_Assertions_h

#include <stdarg.h>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/WTFExport.h"

// New code shouldn't use this function. This function will be deprecated.
void vprintf_stderr_common(const char* format, va_list args);

// WTFLogAlways() is deprecated. crbug.com/638849
WTF_EXPORT PRINTF_FORMAT(1, 2)  // NOLINT
    void WTFLogAlways(const char* format, ...);

#define DCHECK_AT(assertion, file, line)                            \
  LAZY_STREAM(logging::LogMessage(file, line, #assertion).stream(), \
              DCHECK_IS_ON() ? !(assertion) : false)

// Users must test "#if ENABLE_SECURITY_ASSERT", which helps ensure that code
// testing this macro has included this header.
#if defined(ADDRESS_SANITIZER) || DCHECK_IS_ON()
#define ENABLE_SECURITY_ASSERT 1
#else
#define ENABLE_SECURITY_ASSERT 0
#endif

// SECURITY_DCHECK and SECURITY_CHECK
// Use in places where failure of the assertion indicates a possible security
// vulnerability. Classes of these vulnerabilities include bad casts, out of
// bounds accesses, use-after-frees, etc. Please be sure to file bugs for these
// failures using the security template:
//    https://bugs.chromium.org/p/chromium/issues/entry?template=Security%20Bug
#if ENABLE_SECURITY_ASSERT
#define SECURITY_DCHECK(condition) \
  LOG_IF(FATAL, !(condition)) << "Security DCHECK failed: " #condition ". "
// A SECURITY_CHECK failure is actually not vulnerable.
#define SECURITY_CHECK(condition) \
  LOG_IF(FATAL, !(condition)) << "Security CHECK failed: " #condition ". "
#else
#define SECURITY_DCHECK(condition) ((void)0)
#define SECURITY_CHECK(condition) CHECK(condition)
#endif

// DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES
// Allow equality comparisons of Objects by reference or pointer,
// interchangeably.  This can be only used on types whose equality makes no
// other sense than pointer equality.
#define DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES(thisType)    \
  inline bool operator==(const thisType& a, const thisType& b) { \
    return &a == &b;                                             \
  }                                                              \
  inline bool operator==(const thisType& a, const thisType* b) { \
    return &a == b;                                              \
  }                                                              \
  inline bool operator==(const thisType* a, const thisType& b) { \
    return a == &b;                                              \
  }                                                              \
  inline bool operator!=(const thisType& a, const thisType& b) { \
    return !(a == b);                                            \
  }                                                              \
  inline bool operator!=(const thisType& a, const thisType* b) { \
    return !(a == b);                                            \
  }                                                              \
  inline bool operator!=(const thisType* a, const thisType& b) { \
    return !(a == b);                                            \
  }

// DEFINE_TYPE_CASTS
//
// toType() functions are static_cast<> wrappers with SECURITY_DCHECK. It's
// helpful to find bad casts.
//
// toTypeOrDie() has a runtime type check, and it crashes if the specified
// object is not an instance of the destination type. It is used if
// * it's hard to prevent from passing unexpected objects,
// * proceeding with the following code doesn't make sense, and
// * cost of runtime type check is acceptable.
#define DEFINE_TYPE_CASTS(thisType, argumentType, argument, pointerPredicate, \
                          referencePredicate)                                 \
  inline thisType* To##thisType(argumentType* argument) {                     \
    SECURITY_DCHECK(!argument || (pointerPredicate));                         \
    return static_cast<thisType*>(argument);                                  \
  }                                                                           \
  inline const thisType* To##thisType(const argumentType* argument) {         \
    SECURITY_DCHECK(!argument || (pointerPredicate));                         \
    return static_cast<const thisType*>(argument);                            \
  }                                                                           \
  inline thisType& To##thisType(argumentType& argument) {                     \
    SECURITY_DCHECK(referencePredicate);                                      \
    return static_cast<thisType&>(argument);                                  \
  }                                                                           \
  inline const thisType& To##thisType(const argumentType& argument) {         \
    SECURITY_DCHECK(referencePredicate);                                      \
    return static_cast<const thisType&>(argument);                            \
  }                                                                           \
  void To##thisType(const thisType*);                                         \
  void To##thisType(const thisType&);                                         \
  inline thisType* To##thisType##OrDie(argumentType* argument) {              \
    CHECK(!argument || (pointerPredicate));                                   \
    return static_cast<thisType*>(argument);                                  \
  }                                                                           \
  inline const thisType* To##thisType##OrDie(const argumentType* argument) {  \
    CHECK(!argument || (pointerPredicate));                                   \
    return static_cast<const thisType*>(argument);                            \
  }                                                                           \
  inline thisType& To##thisType##OrDie(argumentType& argument) {              \
    CHECK(referencePredicate);                                                \
    return static_cast<thisType&>(argument);                                  \
  }                                                                           \
  inline const thisType& To##thisType##OrDie(const argumentType& argument) {  \
    CHECK(referencePredicate);                                                \
    return static_cast<const thisType&>(argument);                            \
  }                                                                           \
  void To##thisType##OrDie(const thisType*);                                  \
  void To##thisType##OrDie(const thisType&)

// Check at compile time that related enums stay in sync.
#define STATIC_ASSERT_ENUM(a, b)                            \
  static_assert(static_cast<int>(a) == static_cast<int>(b), \
                "mismatching enum: " #a)

#endif  // WTF_Assertions_h
