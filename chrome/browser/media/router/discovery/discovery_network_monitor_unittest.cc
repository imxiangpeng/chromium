// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/discovery/discovery_network_monitor.h"

#include <memory>

#include "base/run_loop.h"
#include "base/task_scheduler/task_scheduler.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using content::BrowserThread;

using testing::Invoke;
using testing::_;

class MockDiscoveryObserver : public DiscoveryNetworkMonitor::Observer {
 public:
  MOCK_METHOD1(OnNetworksChanged, void(const std::string&));
};

}  // namespace

class DiscoveryNetworkMonitorTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_network_info.clear();
    discovery_network_monitor->SetNetworkInfoFunctionForTest(
        &FakeGetNetworkInfo);
  }

  void TearDown() override { delete discovery_network_monitor; }

  static std::vector<DiscoveryNetworkInfo> FakeGetNetworkInfo() {
    return fake_network_info;
  }

  content::TestBrowserThreadBundle test_browser_thread_bundle;
  MockDiscoveryObserver mock_observer;

  std::vector<DiscoveryNetworkInfo> fake_ethernet_info{
      {{std::string("enp0s2"), std::string("ethernet1")}}};
  std::vector<DiscoveryNetworkInfo> fake_wifi_info{
      {DiscoveryNetworkInfo{std::string("wlp3s0"), std::string("wifi1")},
       DiscoveryNetworkInfo{std::string("wlp3s1"), std::string("wifi2")}}};

  std::unique_ptr<net::NetworkChangeNotifier> network_change_notifier =
      base::WrapUnique(net::NetworkChangeNotifier::CreateMock());

  static std::vector<DiscoveryNetworkInfo> fake_network_info;
  // A raw pointer is used here with the delete in TearDown to avoid making the
  // destructor public.
  DiscoveryNetworkMonitor* discovery_network_monitor =
      new DiscoveryNetworkMonitor();
};

// static
std::vector<DiscoveryNetworkInfo>
    DiscoveryNetworkMonitorTest::fake_network_info;

TEST_F(DiscoveryNetworkMonitorTest, NetworkIdIsConsistent) {
  fake_network_info = fake_ethernet_info;
  std::string current_network_id;

  auto capture_network_id =
      [&current_network_id](const std::string& network_id) {
        current_network_id = network_id;
      };
  discovery_network_monitor->AddObserver(&mock_observer);
  EXPECT_CALL(mock_observer, OnNetworksChanged(_))
      .WillOnce(Invoke(capture_network_id));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  content::RunAllBlockingPoolTasksUntilIdle();

  std::string ethernet_network_id = current_network_id;

  fake_network_info.clear();
  EXPECT_CALL(mock_observer, OnNetworksChanged(_))
      .WillOnce(Invoke(capture_network_id));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_NONE);
  content::RunAllBlockingPoolTasksUntilIdle();

  fake_network_info = fake_wifi_info;
  EXPECT_CALL(mock_observer, OnNetworksChanged(_))
      .WillOnce(Invoke(capture_network_id));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_WIFI);
  content::RunAllBlockingPoolTasksUntilIdle();

  std::string wifi_network_id = current_network_id;
  fake_network_info = fake_ethernet_info;
  EXPECT_CALL(mock_observer, OnNetworksChanged(_))
      .WillOnce(Invoke(capture_network_id));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  content::RunAllBlockingPoolTasksUntilIdle();

  EXPECT_EQ(ethernet_network_id, current_network_id);
  EXPECT_NE(ethernet_network_id, wifi_network_id);

  discovery_network_monitor->RemoveObserver(&mock_observer);
}

TEST_F(DiscoveryNetworkMonitorTest, RemoveObserverStopsNotifications) {
  fake_network_info = fake_ethernet_info;

  discovery_network_monitor->AddObserver(&mock_observer);
  EXPECT_CALL(mock_observer, OnNetworksChanged(_));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  content::RunAllBlockingPoolTasksUntilIdle();

  discovery_network_monitor->RemoveObserver(&mock_observer);
  fake_network_info.clear();

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_NONE);
  content::RunAllBlockingPoolTasksUntilIdle();
}

TEST_F(DiscoveryNetworkMonitorTest, RefreshIndependentOfChangeObserver) {
  fake_network_info = fake_ethernet_info;

  discovery_network_monitor->AddObserver(&mock_observer);
  EXPECT_CALL(mock_observer, OnNetworksChanged(_)).Times(testing::AtMost(1));
  auto force_refresh_callback = [](const std::string& network_id) {
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdDisconnected),
              network_id);
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdUnknown),
              network_id);
  };

  discovery_network_monitor->Refresh(base::BindOnce(force_refresh_callback));
  content::RunAllBlockingPoolTasksUntilIdle();
}

TEST_F(DiscoveryNetworkMonitorTest, GetNetworkIdWithoutRefresh) {
  fake_network_info = fake_ethernet_info;

  auto check_network_id = [](const std::string& network_id) {
    EXPECT_EQ(DiscoveryNetworkMonitor::kNetworkIdDisconnected, network_id);
  };
  discovery_network_monitor->GetNetworkId(base::BindOnce(check_network_id));
  content::RunAllBlockingPoolTasksUntilIdle();
}

TEST_F(DiscoveryNetworkMonitorTest, GetNetworkIdWithRefresh) {
  fake_network_info = fake_ethernet_info;

  std::string current_network_id;
  auto capture_network_id = [](std::string* network_id_result,
                               const std::string& network_id) {
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdDisconnected),
              network_id);
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdUnknown),
              network_id);
    *network_id_result = network_id;
  };
  discovery_network_monitor->Refresh(
      base::BindOnce(capture_network_id, &current_network_id));
  content::RunAllBlockingPoolTasksUntilIdle();

  auto check_network_id = [](const std::string& refresh_network_id,
                             const std::string& network_id) {
    EXPECT_EQ(refresh_network_id, network_id);
  };
  discovery_network_monitor->GetNetworkId(
      base::BindOnce(check_network_id, base::ConstRef(current_network_id)));
  content::RunAllBlockingPoolTasksUntilIdle();
}

TEST_F(DiscoveryNetworkMonitorTest, GetNetworkIdWithObserver) {
  fake_network_info = fake_ethernet_info;

  discovery_network_monitor->AddObserver(&mock_observer);
  EXPECT_CALL(mock_observer, OnNetworksChanged(_));

  net::NetworkChangeNotifier::NotifyObserversOfNetworkChangeForTests(
      net::NetworkChangeNotifier::CONNECTION_ETHERNET);
  content::RunAllBlockingPoolTasksUntilIdle();

  std::string current_network_id;
  auto check_network_id = [](const std::string& network_id) {
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdDisconnected),
              network_id);
    EXPECT_NE(std::string(DiscoveryNetworkMonitor::kNetworkIdUnknown),
              network_id);
  };
  discovery_network_monitor->GetNetworkId(base::BindOnce(check_network_id));
  content::RunAllBlockingPoolTasksUntilIdle();
}
