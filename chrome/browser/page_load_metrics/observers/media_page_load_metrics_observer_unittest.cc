// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/media_page_load_metrics_observer.h"

#include <memory>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/test/histogram_tester.h"
#include "base/time/time.h"
#include "chrome/browser/page_load_metrics/observers/page_load_metrics_observer_test_harness.h"
#include "chrome/browser/page_load_metrics/page_load_metrics_observer.h"
#include "chrome/browser/page_load_metrics/page_load_tracker.h"
#include "chrome/common/page_load_metrics/page_load_timing.h"
#include "chrome/common/page_load_metrics/test/page_load_metrics_test_util.h"
#include "third_party/WebKit/public/platform/WebLoadingBehaviorFlag.h"
#include "url/gurl.h"

namespace {

const char kDefaultTestUrl[] = "https://google.com";

}  // namespace

class MediaPageLoadMetricsObserverTest
    : public page_load_metrics::PageLoadMetricsObserverTestHarness {
 public:
  MediaPageLoadMetricsObserverTest() {}
  ~MediaPageLoadMetricsObserverTest() override = default;

  void ResetTest() {
    page_load_metrics::InitPageLoadTimingForTest(&timing_);
    // Reset to the default testing state. Does not reset histogram state.
    timing_.navigation_start = base::Time::FromDoubleT(1);
    timing_.response_start = base::TimeDelta::FromSeconds(2);
    timing_.parse_timing->parse_start = base::TimeDelta::FromSeconds(3);
    timing_.paint_timing->first_contentful_paint =
        base::TimeDelta::FromSeconds(4);
    timing_.paint_timing->first_image_paint = base::TimeDelta::FromSeconds(5);
    timing_.paint_timing->first_text_paint = base::TimeDelta::FromSeconds(6);
    timing_.document_timing->load_event_start = base::TimeDelta::FromSeconds(7);
    PopulateRequiredTimingFields(&timing_);

    network_bytes_ = 0;
    cache_bytes_ = 0;
  }

  void SimulatePageLoad(bool simulate_play_media,
                        bool simulate_app_background) {
    NavigateAndCommit(GURL(kDefaultTestUrl));

    if (simulate_play_media)
      SimulateMediaPlayed();

    SimulateTimingUpdate(timing_);

    // Prepare 4 resources of varying size and configurations.
    page_load_metrics::ExtraRequestCompleteInfo resources[] = {
        // Cached request.
        {GURL(kResourceUrl), net::HostPortPair(), -1 /* frame_tree_node_id */,
         true /*was_cached*/, 1024 * 40 /* raw_body_bytes */,
         0 /* original_network_content_length */,
         nullptr /* data_reduction_proxy_data */,
         content::ResourceType::RESOURCE_TYPE_SCRIPT, 0},
        // Uncached non-proxied request.
        {GURL(kResourceUrl), net::HostPortPair(), -1 /* frame_tree_node_id */,
         false /*was_cached*/, 1024 * 40 /* raw_body_bytes */,
         1024 * 40 /* original_network_content_length */,
         nullptr /* data_reduction_proxy_data */,
         content::ResourceType::RESOURCE_TYPE_SCRIPT, 0},
        // Uncached proxied request with .1 compression ratio.
        {GURL(kResourceUrl), net::HostPortPair(), -1 /* frame_tree_node_id */,
         false /*was_cached*/, 1024 * 40 /* raw_body_bytes */,
         1024 * 40 /* original_network_content_length */,
         nullptr /* data_reduction_proxy_data */,
         content::ResourceType::RESOURCE_TYPE_SCRIPT, 0},
        // Uncached proxied request with .5 compression ratio.
        {GURL(kResourceUrl), net::HostPortPair(), -1 /* frame_tree_node_id */,
         false /*was_cached*/, 1024 * 40 /* raw_body_bytes */,
         1024 * 40 /* original_network_content_length */,
         nullptr /* data_reduction_proxy_data */,
         content::ResourceType::RESOURCE_TYPE_SCRIPT, 0},
    };

    for (const auto& request : resources) {
      SimulateLoadedResource(request);
      if (!request.was_cached) {
        network_bytes_ += request.raw_body_bytes;
      } else {
        cache_bytes_ += request.raw_body_bytes;
      }
    }

    if (simulate_app_background) {
      // The histograms should be logged when the app is backgrounded.
      SimulateAppEnterBackground();
    } else {
      NavigateToUntrackedUrl();
    }
  }

 protected:
  void RegisterObservers(page_load_metrics::PageLoadTracker* tracker) override {
    tracker->AddObserver(base::MakeUnique<MediaPageLoadMetricsObserver>());
  }

  // Simulated byte usage since the last time the test was reset.
  int64_t network_bytes_;
  int64_t cache_bytes_;

 private:
  page_load_metrics::mojom::PageLoadTiming timing_;

  DISALLOW_COPY_AND_ASSIGN(MediaPageLoadMetricsObserverTest);
};

TEST_F(MediaPageLoadMetricsObserverTest, MediaPlayed) {
  ResetTest();
  SimulatePageLoad(true /* simulate_play_media */,
                   false /* simulate_app_background */);

  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Network",
      static_cast<int>(network_bytes_ / 1024), 1);
  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Cache",
      static_cast<int>(cache_bytes_ / 1024), 1);
  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Total",
      static_cast<int>((network_bytes_ + cache_bytes_) / 1024), 1);
}

TEST_F(MediaPageLoadMetricsObserverTest, MediaPlayedAppBackground) {
  ResetTest();
  SimulatePageLoad(true /* simulate_play_media */,
                   true /* simulate_app_background */);

  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Network",
      static_cast<int>(network_bytes_ / 1024), 1);
  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Cache",
      static_cast<int>(cache_bytes_ / 1024), 1);
  histogram_tester().ExpectUniqueSample(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Total",
      static_cast<int>((network_bytes_ + cache_bytes_) / 1024), 1);
}

TEST_F(MediaPageLoadMetricsObserverTest, MediaNotPlayed) {
  ResetTest();
  SimulatePageLoad(false /* simulate_play_media */,
                   false /* simulate_app_background */);

  histogram_tester().ExpectTotalCount(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Network", 0);
  histogram_tester().ExpectTotalCount(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Cache", 0);
  histogram_tester().ExpectTotalCount(
      "PageLoad.Clients.MediaPageLoad.Experimental.Bytes.Total", 0);
}
