// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/tether_connector.h"

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "chromeos/components/tether/connect_tethering_operation.h"
#include "chromeos/components/tether/device_id_tether_network_guid_map.h"
#include "chromeos/components/tether/fake_active_host.h"
#include "chromeos/components/tether/fake_ble_connection_manager.h"
#include "chromeos/components/tether/fake_host_scan_cache.h"
#include "chromeos/components/tether/fake_notification_presenter.h"
#include "chromeos/components/tether/fake_tether_host_fetcher.h"
#include "chromeos/components/tether/fake_wifi_hotspot_connector.h"
#include "chromeos/components/tether/mock_host_connection_metrics_logger.h"
#include "chromeos/components/tether/mock_tether_host_response_recorder.h"
#include "chromeos/components/tether/tether_connector.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_test.h"
#include "components/cryptauth/remote_device.h"
#include "components/cryptauth/remote_device_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

using testing::StrictMock;

namespace chromeos {

namespace tether {

namespace {

const char kSuccessResult[] = "success";

const char kSsid[] = "ssid";
const char kPassword[] = "password";

const char kWifiNetworkGuid[] = "wifiNetworkGuid";

std::string CreateWifiConfigurationJsonString() {
  std::stringstream ss;
  ss << "{"
     << "  \"GUID\": \"" << kWifiNetworkGuid << "\","
     << "  \"Type\": \"" << shill::kTypeWifi << "\","
     << "  \"State\": \"" << shill::kStateIdle << "\""
     << "}";
  return ss.str();
}

class FakeConnectTetheringOperation : public ConnectTetheringOperation {
 public:
  FakeConnectTetheringOperation(
      const cryptauth::RemoteDevice& device_to_connect,
      BleConnectionManager* connection_manager,
      TetherHostResponseRecorder* tether_host_response_recorder,
      bool setup_required)
      : ConnectTetheringOperation(device_to_connect,
                                  connection_manager,
                                  tether_host_response_recorder,
                                  setup_required),
        setup_required_(setup_required) {}

  ~FakeConnectTetheringOperation() override {}

  void SendSuccessfulResponse(const std::string& ssid,
                              const std::string& password) {
    NotifyObserversOfSuccessfulResponse(ssid, password);
  }

  void SendFailedResponse(ConnectTetheringResponse_ResponseCode error_code) {
    NotifyObserversOfConnectionFailure(error_code);
  }

  cryptauth::RemoteDevice GetRemoteDevice() {
    EXPECT_EQ(1u, remote_devices().size());
    return remote_devices()[0];
  }

  bool setup_required() { return setup_required_; }

 private:
  bool setup_required_;
};

class FakeConnectTetheringOperationFactory
    : public ConnectTetheringOperation::Factory {
 public:
  FakeConnectTetheringOperationFactory() {}
  virtual ~FakeConnectTetheringOperationFactory() {}

  std::vector<FakeConnectTetheringOperation*>& created_operations() {
    return created_operations_;
  }

 protected:
  // ConnectTetheringOperation::Factory:
  std::unique_ptr<ConnectTetheringOperation> BuildInstance(
      const cryptauth::RemoteDevice& device_to_connect,
      BleConnectionManager* connection_manager,
      TetherHostResponseRecorder* tether_host_response_recorder,
      bool setup_required) override {
    FakeConnectTetheringOperation* operation =
        new FakeConnectTetheringOperation(device_to_connect, connection_manager,
                                          tether_host_response_recorder,
                                          setup_required);
    created_operations_.push_back(operation);
    return base::WrapUnique(operation);
  }

 private:
  std::vector<FakeConnectTetheringOperation*> created_operations_;
};

}  // namespace

class TetherConnectorTest : public NetworkStateTest {
 public:
  TetherConnectorTest()
      : test_devices_(cryptauth::GenerateTestRemoteDevices(2u)) {}
  ~TetherConnectorTest() override {}

  void SetUp() override {
    DBusThreadManager::Initialize();
    NetworkStateTest::SetUp();
    network_state_handler()->SetTetherTechnologyState(
        NetworkStateHandler::TECHNOLOGY_ENABLED);

    fake_operation_factory_ =
        base::WrapUnique(new FakeConnectTetheringOperationFactory());
    ConnectTetheringOperation::Factory::SetInstanceForTesting(
        fake_operation_factory_.get());

    fake_wifi_hotspot_connector_ =
        base::MakeUnique<FakeWifiHotspotConnector>(network_state_handler());
    fake_active_host_ = base::MakeUnique<FakeActiveHost>();
    fake_tether_host_fetcher_ = base::MakeUnique<FakeTetherHostFetcher>(
        test_devices_, false /* synchronously_reply_with_results */);
    fake_ble_connection_manager_ = base::MakeUnique<FakeBleConnectionManager>();
    mock_tether_host_response_recorder_ =
        base::MakeUnique<MockTetherHostResponseRecorder>();
    device_id_tether_network_guid_map_ =
        base::MakeUnique<DeviceIdTetherNetworkGuidMap>();
    fake_host_scan_cache_ = base::MakeUnique<FakeHostScanCache>();
    fake_notification_presenter_ =
        base::MakeUnique<FakeNotificationPresenter>();
    mock_host_connection_metrics_logger_ =
        base::WrapUnique(new StrictMock<MockHostConnectionMetricsLogger>);

    result_.clear();

    tether_connector_ = base::WrapUnique(new TetherConnector(
        network_state_handler(), fake_wifi_hotspot_connector_.get(),
        fake_active_host_.get(), fake_tether_host_fetcher_.get(),
        fake_ble_connection_manager_.get(),
        mock_tether_host_response_recorder_.get(),
        device_id_tether_network_guid_map_.get(), fake_host_scan_cache_.get(),
        fake_notification_presenter_.get(),
        mock_host_connection_metrics_logger_.get()));

    SetUpTetherNetworks();
  }

  void TearDown() override {
    // Must delete |fake_wifi_hotspot_connector_| before NetworkStateHandler is
    // destroyed to ensure that NetworkStateHandler has zero observers by the
    // time it reaches its destructor.
    fake_wifi_hotspot_connector_.reset();

    ShutdownNetworkState();
    NetworkStateTest::TearDown();
    DBusThreadManager::Shutdown();
  }

  std::string GetTetherNetworkGuid(const std::string& device_id) {
    return device_id_tether_network_guid_map_->GetTetherNetworkGuidForDeviceId(
        device_id);
  }

  void SetUpTetherNetworks() {
    // Add a tether network corresponding to both of the test devices. These
    // networks are expected to be added already before
    // TetherConnector::ConnectToNetwork is called.
    AddTetherNetwork(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
                     "TetherNetworkName1", "TetherNetworkCarrier1",
                     85 /* battery_percentage */, 75 /* signal_strength */,
                     true /* has_connected_to_host */,
                     false /* setup_required */);
    AddTetherNetwork(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
                     "TetherNetworkName2", "TetherNetworkCarrier2",
                     90 /* battery_percentage */, 50 /* signal_strength */,
                     true /* has_connected_to_host */,
                     true /* setup_required */);
  }

  virtual void AddTetherNetwork(const std::string& tether_network_guid,
                                const std::string& device_name,
                                const std::string& carrier,
                                int battery_percentage,
                                int signal_strength,
                                bool has_connected_to_host,
                                bool setup_required) {
    network_state_handler()->AddTetherNetworkState(
        tether_network_guid, device_name, carrier, battery_percentage,
        signal_strength, has_connected_to_host);
    fake_host_scan_cache_->SetHostScanResult(
        *HostScanCacheEntry::Builder()
             .SetTetherNetworkGuid(tether_network_guid)
             .SetDeviceName(device_name)
             .SetCarrier(carrier)
             .SetBatteryPercentage(battery_percentage)
             .SetSignalStrength(signal_strength)
             .SetSetupRequired(setup_required)
             .Build());
  }

  void SuccessfullyJoinWifiNetwork() {
    ConfigureService(CreateWifiConfigurationJsonString());
    fake_wifi_hotspot_connector_->CallMostRecentCallback(kWifiNetworkGuid);
  }

  void SuccessCallback() { result_ = kSuccessResult; }

  void ErrorCallback(const std::string& error_name) { result_ = error_name; }

  void CallConnect(const std::string& tether_network_guid) {
    tether_connector_->ConnectToNetwork(
        tether_network_guid,
        base::Bind(&TetherConnectorTest::SuccessCallback,
                   base::Unretained(this)),
        base::Bind(&TetherConnectorTest::ErrorCallback,
                   base::Unretained(this)));
  }

  void VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode response_code,
      bool setup_required,
      HostConnectionMetricsLogger::ConnectionToHostResult expected_event_type) {
    EXPECT_CALL(*mock_host_connection_metrics_logger_,
                RecordConnectionToHostResult(expected_event_type));

    EXPECT_FALSE(
        fake_notification_presenter_->is_setup_required_notification_shown());

    // test_devices_[0] does not require first-time setup, but test_devices_[1]
    // does require first-time setup. See SetUpTetherNetworks().
    cryptauth::RemoteDevice test_device = test_devices_[setup_required ? 1 : 0];

    CallConnect(GetTetherNetworkGuid(test_device.GetDeviceId()));
    EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
              fake_active_host_->GetActiveHostStatus());
    EXPECT_EQ(test_device.GetDeviceId(),
              fake_active_host_->GetActiveHostDeviceId());
    EXPECT_EQ(GetTetherNetworkGuid(test_device.GetDeviceId()),
              fake_active_host_->GetTetherNetworkGuid());
    EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

    EXPECT_EQ(
        setup_required,
        fake_notification_presenter_->is_setup_required_notification_shown());

    fake_tether_host_fetcher_->InvokePendingCallbacks();

    EXPECT_EQ(
        setup_required,
        fake_notification_presenter_->is_setup_required_notification_shown());
    EXPECT_EQ(
        setup_required,
        fake_operation_factory_->created_operations()[0]->setup_required());

    // Simulate a failed connection attempt (either the host cannot provide
    // tethering at this time or a timeout occurs).
    EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
    fake_operation_factory_->created_operations()[0]->SendFailedResponse(
        response_code);

    EXPECT_FALSE(
        fake_notification_presenter_->is_setup_required_notification_shown());

    // The failure should have resulted in the host being disconnected.
    EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
              fake_active_host_->GetActiveHostStatus());
    EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed,
              GetResultAndReset());
    EXPECT_TRUE(fake_notification_presenter_
                    ->is_connection_failed_notification_shown());
  }

  std::string GetResultAndReset() {
    std::string result;
    result.swap(result_);
    return result;
  }

  const std::vector<cryptauth::RemoteDevice> test_devices_;
  const base::MessageLoop message_loop_;

  std::unique_ptr<FakeConnectTetheringOperationFactory> fake_operation_factory_;
  std::unique_ptr<FakeWifiHotspotConnector> fake_wifi_hotspot_connector_;
  std::unique_ptr<FakeActiveHost> fake_active_host_;
  std::unique_ptr<FakeTetherHostFetcher> fake_tether_host_fetcher_;
  std::unique_ptr<FakeBleConnectionManager> fake_ble_connection_manager_;
  std::unique_ptr<MockTetherHostResponseRecorder>
      mock_tether_host_response_recorder_;
  // TODO(hansberry): Use a fake for this when a real mapping scheme is created.
  std::unique_ptr<DeviceIdTetherNetworkGuidMap>
      device_id_tether_network_guid_map_;
  std::unique_ptr<FakeHostScanCache> fake_host_scan_cache_;
  std::unique_ptr<FakeNotificationPresenter> fake_notification_presenter_;
  std::unique_ptr<StrictMock<MockHostConnectionMetricsLogger>>
      mock_host_connection_metrics_logger_;

  std::string result_;

  std::unique_ptr<TetherConnector> tether_connector_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TetherConnectorTest);
};

TEST_F(TetherConnectorTest, TestCannotFetchDevice) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_INTERNAL_ERROR));

  // Base64-encoded version of "nonexistentDeviceId".
  const char kNonexistentDeviceId[] = "bm9uZXhpc3RlbnREZXZpY2VJZA==";

  CallConnect(GetTetherNetworkGuid(kNonexistentDeviceId));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(kNonexistentDeviceId, fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(kNonexistentDeviceId),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Since an invalid device ID was used, no connection should have been
  // started.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed, GetResultAndReset());
  EXPECT_TRUE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest, TestCancelWhileOperationActive) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_CANCELED_BY_USER));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Simulate a failed connection attempt (either the host cannot provide
  // tethering at this time or a timeout occurs).
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_FALSE(
      fake_operation_factory_->created_operations()[0]->setup_required());
  tether_connector_->CancelConnectionAttempt(
      GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));

  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest,
       TestConnectTetheringOperationFails_SetupNotRequired) {
  VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_UNKNOWN_ERROR,
      false /* setup_required */,
      HostConnectionMetricsLogger::ConnectionToHostResult::
          CONNECTION_RESULT_FAILURE_UNKNOWN_ERROR);
}

TEST_F(TetherConnectorTest, TestConnectTetheringOperationFails_SetupRequired) {
  VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_UNKNOWN_ERROR,
      true /* setup_required */,
      HostConnectionMetricsLogger::ConnectionToHostResult::
          CONNECTION_RESULT_FAILURE_UNKNOWN_ERROR);
}

TEST_F(TetherConnectorTest,
       TestConnectTetheringOperationFails_ProvisioningFailed) {
  VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_PROVISIONING_FAILED,
      false /* setup_required */,
      HostConnectionMetricsLogger::ConnectionToHostResult::
          CONNECTION_RESULT_PROVISIONING_FAILED);
}

TEST_F(TetherConnectorTest,
       TestConnectTetheringOperationFails_TetheringTimeout_SetupNotRequired) {
  VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_TETHERING_TIMEOUT,
      false /* setup_required */,
      HostConnectionMetricsLogger::ConnectionToHostResult::
          CONNECTION_RESULT_FAILURE_TETHERING_TIMED_OUT_FIRST_TIME_SETUP_WAS_NOT_REQUIRED);
}

TEST_F(TetherConnectorTest,
       TestConnectTetheringOperationFails_TetheringTimeout_SetupRequired) {
  VerifyConnectTetheringOperationFails(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_TETHERING_TIMEOUT,
      true /* setup_required */,
      HostConnectionMetricsLogger::ConnectionToHostResult::
          CONNECTION_RESULT_FAILURE_TETHERING_TIMED_OUT_FIRST_TIME_SETUP_WAS_REQUIRED);
}

TEST_F(TetherConnectorTest,
       ConnectionToHostFailedNotificationRemovedWhenConnectionStarts) {
  // Start with the "connection to host failed" notification showing.
  fake_notification_presenter_->NotifyConnectionToHostFailed();

  // Starting a connection should result in it being removed.
  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest, TestConnectingToWifiFails) {
  EXPECT_CALL(*mock_host_connection_metrics_logger_,
              RecordConnectionToHostResult(
                  HostConnectionMetricsLogger::ConnectionToHostResult::
                      CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_TIMEOUT));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Receive a successful response. We should still be connecting.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_FALSE(
      fake_operation_factory_->created_operations()[0]->setup_required());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());

  // |fake_wifi_hotspot_connector_| should have received the SSID and password
  // above. Verify this, then return an empty string, signaling a failure to
  // connect.
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  EXPECT_EQ(fake_active_host_->GetTetherNetworkGuid(),
            fake_wifi_hotspot_connector_->most_recent_tether_network_guid());
  fake_wifi_hotspot_connector_->CallMostRecentCallback("");

  // The failure should have resulted in the host being disconnected.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed, GetResultAndReset());
  EXPECT_TRUE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest, TestCancelWhileConnectingToWifi) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_CANCELED_BY_USER));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Receive a successful response. We should still be connecting.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_FALSE(
      fake_operation_factory_->created_operations()[0]->setup_required());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());

  tether_connector_->CancelConnectionAttempt(
      GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));

  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest, TestSuccessfulConnection) {
  EXPECT_CALL(*mock_host_connection_metrics_logger_,
              RecordConnectionToHostResult(
                  HostConnectionMetricsLogger::ConnectionToHostResult::
                      CONNECTION_RESULT_SUCCESS));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());
  EXPECT_FALSE(
      fake_notification_presenter_->is_setup_required_notification_shown());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Receive a successful response. We should still be connecting.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_FALSE(
      fake_operation_factory_->created_operations()[0]->setup_required());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());

  // |fake_wifi_hotspot_connector_| should have received the SSID and password
  // above. Verify this, then return the GUID corresponding to the connected
  // Wi-Fi network.
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  EXPECT_EQ(fake_active_host_->GetTetherNetworkGuid(),
            fake_wifi_hotspot_connector_->most_recent_tether_network_guid());
  SuccessfullyJoinWifiNetwork();

  // The active host should now be connected.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_EQ(kWifiNetworkGuid, fake_active_host_->GetWifiNetworkGuid());

  EXPECT_EQ(kSuccessResult, GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest, TestSuccessfulConnection_SetupRequired) {
  EXPECT_CALL(*mock_host_connection_metrics_logger_,
              RecordConnectionToHostResult(
                  HostConnectionMetricsLogger::ConnectionToHostResult::
                      CONNECTION_RESULT_SUCCESS));

  EXPECT_FALSE(
      fake_notification_presenter_->is_setup_required_notification_shown());

  CallConnect(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));

  EXPECT_TRUE(
      fake_notification_presenter_->is_setup_required_notification_shown());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  EXPECT_TRUE(
      fake_notification_presenter_->is_setup_required_notification_shown());
  EXPECT_TRUE(
      fake_operation_factory_->created_operations()[0]->setup_required());

  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);

  EXPECT_TRUE(
      fake_notification_presenter_->is_setup_required_notification_shown());

  SuccessfullyJoinWifiNetwork();

  EXPECT_FALSE(
      fake_notification_presenter_->is_setup_required_notification_shown());

  EXPECT_EQ(kSuccessResult, GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringFetch_DifferentDevice) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_CANCELED_BY_USER));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));

  // Instead of invoking the pending callbacks on |fake_tether_host_fetcher_|,
  // attempt another connection attempt, this time to another device.
  CallConnect(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());

  // Now invoke the callbacks. An operation should have been created for the
  // device 1, not device 0.
  fake_tether_host_fetcher_->InvokePendingCallbacks();
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_EQ(
      test_devices_[1],
      fake_operation_factory_->created_operations()[0]->GetRemoteDevice());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringOperation_DifferentDevice) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_CANCELED_BY_USER));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // An operation should have been created.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());

  // Before the created operation replies, start a new connection to device 1.
  CallConnect(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Now, the active host should be the second device.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[1].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  // A second operation should have been created.
  EXPECT_EQ(2u, fake_operation_factory_->created_operations().size());

  // No connection should have been started.
  EXPECT_TRUE(fake_wifi_hotspot_connector_->most_recent_ssid().empty());
  EXPECT_TRUE(fake_wifi_hotspot_connector_->most_recent_password().empty());
  EXPECT_TRUE(
      fake_wifi_hotspot_connector_->most_recent_tether_network_guid().empty());

  // The second operation replies successfully, and this response should
  // result in a Wi-Fi connection attempt.
  fake_operation_factory_->created_operations()[1]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  EXPECT_EQ(fake_active_host_->GetTetherNetworkGuid(),
            fake_wifi_hotspot_connector_->most_recent_tether_network_guid());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringWifiConnection_DifferentDevice) {
  EXPECT_CALL(
      *mock_host_connection_metrics_logger_,
      RecordConnectionToHostResult(
          HostConnectionMetricsLogger::ConnectionToHostResult::
              CONNECTION_RESULT_FAILURE_CLIENT_CONNECTION_CANCELED_BY_USER));

  CallConnect(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  EXPECT_EQ(fake_active_host_->GetTetherNetworkGuid(),
            fake_wifi_hotspot_connector_->most_recent_tether_network_guid());

  // While the connection to the Wi-Fi network is in progress, start a new
  // connection attempt.
  CallConnect(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  EXPECT_FALSE(
      fake_notification_presenter_->is_connection_failed_notification_shown());
  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Connect successfully to the first Wi-Fi network. Even though a temporary
  // connection has succeeded, the active host should be CONNECTING to device 1.
  SuccessfullyJoinWifiNetwork();
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[1].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());
}

}  // namespace tether

}  // namespace chromeos
