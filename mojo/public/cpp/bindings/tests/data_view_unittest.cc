// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/message_loop/message_loop.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/lib/fixed_buffer.h"
#include "mojo/public/cpp/bindings/lib/serialization.h"
#include "mojo/public/interfaces/bindings/tests/test_data_view.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace test {
namespace data_view {
namespace {

class DataViewTest : public testing::Test {
 private:
  base::MessageLoop message_loop_;
};

struct DataViewHolder {
  std::unique_ptr<TestStructDataView> data_view;
  mojo::Message message;
  mojo::internal::SerializationContext context;
};

std::unique_ptr<DataViewHolder> SerializeTestStruct(TestStructPtr input) {
  auto result = base::MakeUnique<DataViewHolder>();
  mojo::internal::PrepareToSerialize<TestStructDataView>(input,
                                                         &result->context);
  result->message = Message(0, 0, 0, 0, nullptr);
  internal::TestStruct_Data::BufferWriter writer;
  mojo::internal::Serialize<TestStructDataView>(
      input, result->message.payload_buffer(), &writer, &result->context);
  result->data_view =
      base::MakeUnique<TestStructDataView>(writer.data(), &result->context);
  return result;
}

class TestInterfaceImpl : public TestInterface {
 public:
  explicit TestInterfaceImpl(TestInterfaceRequest request)
      : binding_(this, std::move(request)) {}
  ~TestInterfaceImpl() override {}

  // TestInterface implementation:
  void Echo(int32_t value, const EchoCallback& callback) override {
    callback.Run(value);
  }

 private:
  Binding<TestInterface> binding_;
};

}  // namespace

TEST_F(DataViewTest, String) {
  TestStructPtr obj(TestStruct::New());
  obj->f_string = "hello";

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  StringDataView string_data_view;
  data_view.GetFStringDataView(&string_data_view);

  ASSERT_FALSE(string_data_view.is_null());
  EXPECT_EQ(std::string("hello"),
            std::string(string_data_view.storage(), string_data_view.size()));
}

TEST_F(DataViewTest, NestedStruct) {
  TestStructPtr obj(TestStruct::New());
  obj->f_struct = NestedStruct::New();
  obj->f_struct->f_int32 = 42;

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  NestedStructDataView struct_data_view;
  data_view.GetFStructDataView(&struct_data_view);

  ASSERT_FALSE(struct_data_view.is_null());
  EXPECT_EQ(42, struct_data_view.f_int32());
}

TEST_F(DataViewTest, NativeStruct) {
  TestStructPtr obj(TestStruct::New());
  obj->f_native_struct = NativeStruct::New();
  obj->f_native_struct->data = std::vector<uint8_t>({3, 2, 1});

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  NativeStructDataView struct_data_view;
  data_view.GetFNativeStructDataView(&struct_data_view);

  ASSERT_FALSE(struct_data_view.is_null());
  ASSERT_EQ(3u, struct_data_view.size());
  EXPECT_EQ(3, struct_data_view[0]);
  EXPECT_EQ(2, struct_data_view[1]);
  EXPECT_EQ(1, struct_data_view[2]);
  EXPECT_EQ(3, *struct_data_view.data());
}

TEST_F(DataViewTest, BoolArray) {
  TestStructPtr obj(TestStruct::New());
  obj->f_bool_array = {true, false};

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<bool> array_data_view;
  data_view.GetFBoolArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(2u, array_data_view.size());
  EXPECT_TRUE(array_data_view[0]);
  EXPECT_FALSE(array_data_view[1]);
}

TEST_F(DataViewTest, IntegerArray) {
  TestStructPtr obj(TestStruct::New());
  obj->f_int32_array = {1024, 128};

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<int32_t> array_data_view;
  data_view.GetFInt32ArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(2u, array_data_view.size());
  EXPECT_EQ(1024, array_data_view[0]);
  EXPECT_EQ(128, array_data_view[1]);
  EXPECT_EQ(1024, *array_data_view.data());
}

TEST_F(DataViewTest, EnumArray) {
  TestStructPtr obj(TestStruct::New());
  obj->f_enum_array = {TestEnum::VALUE_1, TestEnum::VALUE_0};

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<TestEnum> array_data_view;
  data_view.GetFEnumArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(2u, array_data_view.size());
  EXPECT_EQ(TestEnum::VALUE_1, array_data_view[0]);
  EXPECT_EQ(TestEnum::VALUE_0, array_data_view[1]);
  EXPECT_EQ(TestEnum::VALUE_0, *(array_data_view.data() + 1));

  TestEnum output;
  ASSERT_TRUE(array_data_view.Read(0, &output));
  EXPECT_EQ(TestEnum::VALUE_1, output);
}

TEST_F(DataViewTest, InterfaceArray) {
  TestInterfacePtr ptr;
  TestInterfaceImpl impl(MakeRequest(&ptr));

  TestStructPtr obj(TestStruct::New());
  obj->f_interface_array.push_back(std::move(ptr));

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<TestInterfacePtrDataView> array_data_view;
  data_view.GetFInterfaceArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(1u, array_data_view.size());

  TestInterfacePtr ptr2 = array_data_view.Take<TestInterfacePtr>(0);
  ASSERT_TRUE(ptr2);
  int32_t result = 0;
  ASSERT_TRUE(ptr2->Echo(42, &result));
  EXPECT_EQ(42, result);
}

TEST_F(DataViewTest, NestedArray) {
  TestStructPtr obj(TestStruct::New());
  obj->f_nested_array = {{3, 4}, {2}};

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<ArrayDataView<int32_t>> array_data_view;
  data_view.GetFNestedArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(2u, array_data_view.size());

  ArrayDataView<int32_t> nested_array_data_view;
  array_data_view.GetDataView(0, &nested_array_data_view);
  ASSERT_FALSE(nested_array_data_view.is_null());
  ASSERT_EQ(2u, nested_array_data_view.size());
  EXPECT_EQ(4, nested_array_data_view[1]);

  std::vector<int32_t> vec;
  ASSERT_TRUE(array_data_view.Read(1, &vec));
  ASSERT_EQ(1u, vec.size());
  EXPECT_EQ(2, vec[0]);
}

TEST_F(DataViewTest, StructArray) {
  NestedStructPtr nested_struct(NestedStruct::New());
  nested_struct->f_int32 = 42;

  TestStructPtr obj(TestStruct::New());
  obj->f_struct_array.push_back(std::move(nested_struct));

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<NestedStructDataView> array_data_view;
  data_view.GetFStructArrayDataView(&array_data_view);

  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(1u, array_data_view.size());

  NestedStructDataView struct_data_view;
  array_data_view.GetDataView(0, &struct_data_view);
  ASSERT_FALSE(struct_data_view.is_null());
  EXPECT_EQ(42, struct_data_view.f_int32());

  NestedStructPtr nested_struct2;
  ASSERT_TRUE(array_data_view.Read(0, &nested_struct2));
  ASSERT_TRUE(nested_struct2);
  EXPECT_EQ(42, nested_struct2->f_int32);
}

TEST_F(DataViewTest, Map) {
  TestStructPtr obj(TestStruct::New());
  obj->f_map["1"] = 1;
  obj->f_map["2"] = 2;

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  MapDataView<StringDataView, int32_t> map_data_view;
  data_view.GetFMapDataView(&map_data_view);

  ASSERT_FALSE(map_data_view.is_null());
  ASSERT_EQ(2u, map_data_view.size());

  ASSERT_FALSE(map_data_view.keys().is_null());
  ASSERT_EQ(2u, map_data_view.keys().size());

  ASSERT_FALSE(map_data_view.values().is_null());
  ASSERT_EQ(2u, map_data_view.values().size());

  std::vector<std::string> keys;
  ASSERT_TRUE(map_data_view.ReadKeys(&keys));
  std::vector<int32_t> values;
  ASSERT_TRUE(map_data_view.ReadValues(&values));

  std::unordered_map<std::string, int32_t> map;
  for (size_t i = 0; i < 2; ++i)
    map[keys[i]] = values[i];

  EXPECT_EQ(1, map["1"]);
  EXPECT_EQ(2, map["2"]);
}

TEST_F(DataViewTest, UnionArray) {
  TestUnionPtr union_ptr(TestUnion::New());
  union_ptr->set_f_int32(1024);

  TestStructPtr obj(TestStruct::New());
  obj->f_union_array.push_back(std::move(union_ptr));

  auto data_view_holder = SerializeTestStruct(std::move(obj));
  auto& data_view = *data_view_holder->data_view;

  ArrayDataView<TestUnionDataView> array_data_view;
  data_view.GetFUnionArrayDataView(&array_data_view);
  ASSERT_FALSE(array_data_view.is_null());
  ASSERT_EQ(1u, array_data_view.size());

  TestUnionDataView union_data_view;
  array_data_view.GetDataView(0, &union_data_view);
  ASSERT_FALSE(union_data_view.is_null());

  TestUnionPtr union_ptr2;
  ASSERT_TRUE(array_data_view.Read(0, &union_ptr2));
  ASSERT_TRUE(union_ptr2->is_f_int32());
  EXPECT_EQ(1024, union_ptr2->get_f_int32());
}

}  // namespace data_view
}  // namespace test
}  // namespace mojo
