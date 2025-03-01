// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/biod/biod_client.h"

#include <map>
#include <string>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "chromeos/dbus/biod/messages.pb.h"
#include "chromeos/dbus/biod/test_utils.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_object_proxy.h"
#include "dbus/object_path.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace chromeos {

namespace {

// Shorthand for a commonly-used constant.
const char* kInterface = biod::kBiometricsManagerInterface;

// Value used to intialize dbus::ObjectPath objects in tests to make it easier
// to determine when empty values have been assigned.
const char kInvalidTestPath[] = "/invalid/test/path";

// Value used to intialize string objects in tests to make it easier to
// determine when empty values have been assigned.
const char kInvalidString[] = "invalidString";

// Matcher that verifies that a dbus::Message has member |name|.
MATCHER_P(HasMember, name, "") {
  if (arg->GetMember() != name) {
    *result_listener << "has member " << arg->GetMember();
    return false;
  }
  return true;
}

// Runs |callback| with |response|. Needed due to ResponseCallback expecting a
// bare pointer rather than an std::unique_ptr.
void RunResponseCallback(dbus::ObjectProxy::ResponseCallback callback,
                         std::unique_ptr<dbus::Response> response) {
  std::move(callback).Run(response.get());
}

}  // namespace

class BiodClientTest : public testing::Test {
 public:
  BiodClientTest() {}
  ~BiodClientTest() override {}

  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);

    dbus::ObjectPath fpc_bio_path = dbus::ObjectPath(base::StringPrintf(
        "%s/%s", biod::kBiodServicePath, biod::kFpcBiometricsManagerName));
    proxy_ = new dbus::MockObjectProxy(bus_.get(), biod::kBiodServiceName,
                                       fpc_bio_path);

    // |client_|'s Init() method should request a proxy for communicating with
    // biometrics api.
    EXPECT_CALL(*bus_.get(), GetObjectProxy(biod::kBiodServiceName, _))
        .WillRepeatedly(Return(proxy_.get()));

    // Save |client_|'s signal callback.
    EXPECT_CALL(*proxy_.get(), ConnectToSignal(kInterface, _, _, _))
        .WillRepeatedly(Invoke(this, &BiodClientTest::ConnectToSignal));

    client_.reset(BiodClient::Create(REAL_DBUS_CLIENT_IMPLEMENTATION));
    client_->Init(bus_.get());

    // Execute callbacks posted by Init().
    base::RunLoop().RunUntilIdle();
  }

  void GetBiometricType(uint32_t type) {
    biometric_type_ = static_cast<biod::BiometricType>(type);
  }

 protected:
  // Add an expectation for method with |method_name| to be called. When the
  // method is called the response should match |response|.
  void AddMethodExpectation(const std::string& method_name,
                            std::unique_ptr<dbus::Response> response) {
    ASSERT_FALSE(pending_method_calls_.count(method_name));
    pending_method_calls_[method_name] = std::move(response);
    EXPECT_CALL(*proxy_.get(), DoCallMethod(HasMember(method_name), _, _))
        .WillOnce(Invoke(this, &BiodClientTest::OnCallMethod));
  }

  // Synchronously passes |signal| to |client_|'s handler, simulating the signal
  // from biometrics.
  void EmitSignal(dbus::Signal* signal) {
    const std::string signal_name = signal->GetMember();
    const auto it = signal_callbacks_.find(signal_name);
    ASSERT_TRUE(it != signal_callbacks_.end())
        << "Client didn't register for signal " << signal_name;
    it->second.Run(signal);
  }

  // Passes a enroll scan done signal to |client_|.
  void EmitEnrollScanDoneSignal(biod::ScanResult scan_result,
                                bool enroll_session_complete,
                                int percent_complete) {
    dbus::Signal signal(kInterface,
                        biod::kBiometricsManagerEnrollScanDoneSignal);
    dbus::MessageWriter writer(&signal);
    biod::EnrollScanDone protobuf;
    protobuf.set_scan_result(scan_result);
    protobuf.set_done(enroll_session_complete);
    protobuf.set_percent_complete(percent_complete);
    writer.AppendProtoAsArrayOfBytes(protobuf);
    EmitSignal(&signal);
  }

  // Passes a auth scan done signal to |client_|.
  void EmitAuthScanDoneSignal(biod::ScanResult scan_result,
                              const AuthScanMatches& matches) {
    dbus::Signal signal(kInterface, biod::kBiometricsManagerAuthScanDoneSignal);
    dbus::MessageWriter writer(&signal);
    writer.AppendUint32(static_cast<uint32_t>(scan_result));

    dbus::MessageWriter array_writer(nullptr);
    writer.OpenArray("{sx}", &array_writer);
    for (auto& match : matches) {
      dbus::MessageWriter entry_writer(nullptr);
      array_writer.OpenDictEntry(&entry_writer);
      entry_writer.AppendString(match.first);
      entry_writer.AppendArrayOfObjectPaths(match.second);
      array_writer.CloseContainer(&entry_writer);
    }
    writer.CloseContainer(&array_writer);
    EmitSignal(&signal);
  }

  // Passes a scan failed signal to |client_|.
  void EmitScanFailedSignal() {
    dbus::Signal signal(kInterface,
                        biod::kBiometricsManagerSessionFailedSignal);
    EmitSignal(&signal);
  }

  std::map<std::string, std::unique_ptr<dbus::Response>> pending_method_calls_;

  base::MessageLoop message_loop_;

  // Mock bus and proxy for simulating calls.
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;

  std::unique_ptr<BiodClient> client_;

  // Maps from biod signal name to the corresponding callback provided by
  // |client_|.
  std::map<std::string, dbus::ObjectProxy::SignalCallback> signal_callbacks_;

  biod::BiometricType biometric_type_;

 private:
  // Handles calls to |proxy_|'s ConnectToSignal() method.
  void ConnectToSignal(
      const std::string& interface_name,
      const std::string& signal_name,
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
    EXPECT_EQ(interface_name, kInterface);
    signal_callbacks_[signal_name] = signal_callback;
    message_loop_.task_runner()->PostTask(
        FROM_HERE, base::Bind(on_connected_callback, interface_name,
                              signal_name, true /* success */));
  }

  // Handles calls to |proxy_|'s CallMethod().
  void OnCallMethod(dbus::MethodCall* method_call,
                    int timeout_ms,
                    dbus::ObjectProxy::ResponseCallback* callback) {
    auto it = pending_method_calls_.find(method_call->GetMember());
    ASSERT_TRUE(it != pending_method_calls_.end());
    auto pending_response = std::move(it->second);
    pending_method_calls_.erase(it);

    message_loop_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&RunResponseCallback, std::move(*callback),
                                  base::Passed(&pending_response)));
  }

  DISALLOW_COPY_AND_ASSIGN(BiodClientTest);
};

TEST_F(BiodClientTest, TestStartEnrollSession) {
  const std::string kFakeId("fakeId");
  const std::string kFakeLabel("fakeLabel");
  const dbus::ObjectPath kFakeObjectPath(std::string("/fake/object/path"));
  const dbus::ObjectPath kFakeObjectPath2(std::string("/fake/object/path2"));

  // Verify that by sending a empty reponse or a improperly formatted one, the
  // response is an empty object path.
  AddMethodExpectation(biod::kBiometricsManagerStartEnrollSessionMethod,
                       nullptr);
  dbus::ObjectPath returned_path(kInvalidTestPath);
  client_->StartEnrollSession(
      kFakeId, kFakeLabel,
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(dbus::ObjectPath(), returned_path);

  std::unique_ptr<dbus::Response> bad_response(dbus::Response::CreateEmpty());
  dbus::MessageWriter bad_writer(bad_response.get());
  bad_writer.AppendString("");
  AddMethodExpectation(biod::kBiometricsManagerStartEnrollSessionMethod,
                       std::move(bad_response));
  returned_path = dbus::ObjectPath(kInvalidTestPath);
  client_->StartEnrollSession(
      kFakeId, kFakeLabel,
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(dbus::ObjectPath(), returned_path);

  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendObjectPath(kFakeObjectPath);

  // Create a fake response with a fake object path. The start enroll
  // call should return this object path.
  AddMethodExpectation(biod::kBiometricsManagerStartEnrollSessionMethod,
                       std::move(response));
  returned_path = dbus::ObjectPath(kInvalidTestPath);
  client_->StartEnrollSession(
      kFakeId, kFakeLabel,
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kFakeObjectPath, returned_path);
}

TEST_F(BiodClientTest, TestGetRecordsForUser) {
  const std::string kFakeId("fakeId");
  const dbus::ObjectPath kFakeObjectPath(std::string("/fake/object/path"));
  const dbus::ObjectPath kFakeObjectPath2(std::string("/fake/object/path2"));
  const std::vector<dbus::ObjectPath> kFakeObjectPaths = {kFakeObjectPath,
                                                          kFakeObjectPath2};

  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendArrayOfObjectPaths(kFakeObjectPaths);

  // Create a fake response with an array of fake object paths. The get
  // records for user call should return this array of object paths.
  AddMethodExpectation(biod::kBiometricsManagerGetRecordsForUserMethod,
                       std::move(response));
  std::vector<dbus::ObjectPath> returned_object_paths = {
      dbus::ObjectPath(kInvalidTestPath)};
  client_->GetRecordsForUser(
      kFakeId,
      base::Bind(&test_utils::CopyObjectPathArray, &returned_object_paths));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kFakeObjectPaths, returned_object_paths);

  // Verify that by sending a empty reponse the response is an empty array of
  // object paths.
  AddMethodExpectation(biod::kBiometricsManagerGetRecordsForUserMethod,
                       nullptr);
  returned_object_paths = {dbus::ObjectPath(kInvalidTestPath)};
  client_->GetRecordsForUser(
      kFakeId,
      base::Bind(&test_utils::CopyObjectPathArray, &returned_object_paths));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(std::vector<dbus::ObjectPath>(), returned_object_paths);
}

TEST_F(BiodClientTest, TestDestroyAllRecords) {
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());

  // Create an empty response to simulate success.
  AddMethodExpectation(biod::kBiometricsManagerDestroyAllRecordsMethod,
                       std::move(response));
  DBusMethodCallStatus returned_status = static_cast<DBusMethodCallStatus>(-1);
  client_->DestroyAllRecords(
      base::Bind(&test_utils::CopyDBusMethodCallStatus, &returned_status));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DBUS_METHOD_CALL_SUCCESS, returned_status);

  // Return a null response to simulate failure.
  AddMethodExpectation(biod::kBiometricsManagerDestroyAllRecordsMethod,
                       nullptr);
  returned_status = static_cast<DBusMethodCallStatus>(-1);
  client_->DestroyAllRecords(
      base::Bind(&test_utils::CopyDBusMethodCallStatus, &returned_status));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DBUS_METHOD_CALL_FAILURE, returned_status);
}

TEST_F(BiodClientTest, TestStartAuthentication) {
  const dbus::ObjectPath kFakeObjectPath(std::string("/fake/object/path"));

  // Verify that by sending a empty reponse or a improperly formatted one, the
  // response is an empty object path.
  AddMethodExpectation(biod::kBiometricsManagerStartAuthSessionMethod, nullptr);
  dbus::ObjectPath returned_path(kInvalidTestPath);
  client_->StartAuthSession(
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(dbus::ObjectPath(), returned_path);

  std::unique_ptr<dbus::Response> bad_response(dbus::Response::CreateEmpty());
  dbus::MessageWriter bad_writer(bad_response.get());
  bad_writer.AppendString("");
  AddMethodExpectation(biod::kBiometricsManagerStartAuthSessionMethod,
                       std::move(bad_response));
  returned_path = dbus::ObjectPath(kInvalidTestPath);
  client_->StartAuthSession(
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(dbus::ObjectPath(), returned_path);

  // Create a fake response with a fake object path. The start authentication
  // call should return this object path.
  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendObjectPath(kFakeObjectPath);

  AddMethodExpectation(biod::kBiometricsManagerStartAuthSessionMethod,
                       std::move(response));
  returned_path = dbus::ObjectPath(kInvalidTestPath);
  client_->StartAuthSession(
      base::Bind(&test_utils::CopyObjectPath, &returned_path));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kFakeObjectPath, returned_path);
}

TEST_F(BiodClientTest, TestRequestBiometricType) {
  const biod::BiometricType kFakeBiometricType =
      biod::BIOMETRIC_TYPE_FINGERPRINT;

  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendVariantOfUint32(static_cast<uint32_t>(kFakeBiometricType));

  // Create a fake response with biometric type. The get label call should
  // return this exact biometric type.
  biometric_type_ = biod::BIOMETRIC_TYPE_MAX;
  AddMethodExpectation(dbus::kDBusPropertiesGet, std::move(response));
  client_->RequestType(
      base::Bind(&BiodClientTest::GetBiometricType, base::Unretained(this)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kFakeBiometricType, biometric_type_);

  // Verify that by sending a null reponse, the result is an unknown biometric
  // type.
  biometric_type_ = biod::BIOMETRIC_TYPE_MAX;
  AddMethodExpectation(dbus::kDBusPropertiesGet, nullptr);
  client_->RequestType(
      base::Bind(&BiodClientTest::GetBiometricType, base::Unretained(this)));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(biod::BiometricType::BIOMETRIC_TYPE_UNKNOWN, biometric_type_);
}

TEST_F(BiodClientTest, TestRequestRecordLabel) {
  const std::string kFakeLabel("fakeLabel");
  const dbus::ObjectPath kFakeRecordPath(std::string("/fake/record/path"));

  std::unique_ptr<dbus::Response> response(dbus::Response::CreateEmpty());
  dbus::MessageWriter writer(response.get());
  writer.AppendVariantOfString(kFakeLabel);

  // Create a fake response with string. The get label call should return this
  // exact string.
  std::string returned_label = kInvalidString;
  AddMethodExpectation(dbus::kDBusPropertiesGet, std::move(response));
  client_->RequestRecordLabel(
      kFakeRecordPath, base::Bind(&test_utils::CopyString, &returned_label));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kFakeLabel, returned_label);

  // Verify that by sending a null reponse, the result is an empty string.
  returned_label = kInvalidString;
  AddMethodExpectation(dbus::kDBusPropertiesGet, nullptr);
  client_->RequestRecordLabel(
      kFakeRecordPath, base::Bind(&test_utils::CopyString, &returned_label));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ("", returned_label);
}

// Verify when signals are mocked, an observer will catch the signals as
// expected.
TEST_F(BiodClientTest, TestNotifyObservers) {
  test_utils::TestBiodObserver observer;
  client_->AddObserver(&observer);
  EXPECT_TRUE(client_->HasObserver(&observer));

  const biod::ScanResult scan_signal = biod::ScanResult::SCAN_RESULT_SUCCESS;
  const bool enroll_session_complete = false;
  const int percent_complete = 0;
  const AuthScanMatches test_attempt;
  EXPECT_EQ(0, observer.NumEnrollScansReceived());
  EXPECT_EQ(0, observer.NumAuthScansReceived());
  EXPECT_EQ(0, observer.num_failures_received());

  EmitEnrollScanDoneSignal(scan_signal, enroll_session_complete,
                           percent_complete);
  EXPECT_EQ(1, observer.NumEnrollScansReceived());

  EmitAuthScanDoneSignal(scan_signal, test_attempt);
  EXPECT_EQ(1, observer.NumAuthScansReceived());

  EmitScanFailedSignal();
  EXPECT_EQ(1, observer.num_failures_received());

  client_->RemoveObserver(&observer);

  EmitEnrollScanDoneSignal(scan_signal, enroll_session_complete,
                           percent_complete);
  EmitAuthScanDoneSignal(scan_signal, test_attempt);
  EXPECT_EQ(1, observer.NumEnrollScansReceived());
  EXPECT_EQ(1, observer.NumAuthScansReceived());
  EXPECT_EQ(1, observer.num_failures_received());
}
}  // namespace chromeos
