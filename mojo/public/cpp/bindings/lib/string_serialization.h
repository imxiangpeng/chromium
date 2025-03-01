// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_STRING_SERIALIZATION_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_STRING_SERIALIZATION_H_

#include <stddef.h>
#include <string.h>

#include "mojo/public/cpp/bindings/lib/array_internal.h"
#include "mojo/public/cpp/bindings/lib/serialization_forward.h"
#include "mojo/public/cpp/bindings/lib/serialization_util.h"
#include "mojo/public/cpp/bindings/string_data_view.h"
#include "mojo/public/cpp/bindings/string_traits.h"

namespace mojo {
namespace internal {

template <typename MaybeConstUserType>
struct Serializer<StringDataView, MaybeConstUserType> {
  using UserType = typename std::remove_const<MaybeConstUserType>::type;
  using Traits = StringTraits<UserType>;

  static void PrepareToSerialize(MaybeConstUserType& input,
                                 SerializationContext* context) {}

  static void Serialize(MaybeConstUserType& input,
                        Buffer* buffer,
                        String_Data::BufferWriter* writer,
                        SerializationContext* context) {
    if (CallIsNullIfExists<Traits>(input))
      return;

    void* custom_context = CustomContextHelper<Traits>::SetUp(input, context);
    const size_t size = CallWithContext(Traits::GetSize, input, custom_context);
    writer->Allocate(size, buffer);
    memcpy((*writer)->storage(),
           CallWithContext(Traits::GetData, input, custom_context), size);
    CustomContextHelper<Traits>::TearDown(input, custom_context);
  }

  static bool Deserialize(String_Data* input,
                          UserType* output,
                          SerializationContext* context) {
    if (!input)
      return CallSetToNullIfExists<Traits>(output);
    return Traits::Read(StringDataView(input, context), output);
  }
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_STRING_SERIALIZATION_H_
