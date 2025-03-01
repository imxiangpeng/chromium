// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZUCCHINI_TYPED_VALUE_H_
#define CHROME_INSTALLER_ZUCCHINI_TYPED_VALUE_H_

namespace zucchini {

// Strong typed values, with compare and convert functions for underlying data.
// Typically one would use strongly typed enums for this. However, for Zucchini,
// the number of bytes is not fixed, and must be represented as an integer for
// iteration.
// |Tag| is a type tag used to uniquely identify TypedValue.
// |T| is an integral type used to hold values.
// Example:
//    struct Foo : TypedValue<Foo, int> {
//      using Foo::TypedValue::TypedValue; // inheriting constructor.
//    };
// Foo will be used to hold values of type |int|, but with a distinct type from
// any other TypedValue.
template <class Tag, class T>
class TypedValue {
 public:
  constexpr TypedValue() = default;
  explicit constexpr TypedValue(const T& value) : value_(value) {}

  explicit operator T() const { return value_; }
  const T value() const { return value_; }

  friend bool operator==(const TypedValue& a, const TypedValue& b) {
    return a.value_ == b.value_;
  }
  friend bool operator!=(const TypedValue& a, const TypedValue& b) {
    return !(a == b);
  }

 private:
  T value_ = {};
};

}  // namespace zucchini

#endif  // CHROME_INSTALLER_ZUCCHINI_TYPED_VALUE_H_
