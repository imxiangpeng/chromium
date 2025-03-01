// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/page_load_metrics_observer_test_harness.h"

#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/ptr_util.h"
#include "chrome/test/base/testing_browser_process.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/web_contents_tester.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace page_load_metrics {

PageLoadMetricsObserverTestHarness::PageLoadMetricsObserverTestHarness()
    : ChromeRenderViewHostTestHarness() {}

PageLoadMetricsObserverTestHarness::~PageLoadMetricsObserverTestHarness() {}

void PageLoadMetricsObserverTestHarness::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();
  TestingBrowserProcess::GetGlobal()->SetUkmRecorder(&test_ukm_recorder_);
  SetContents(CreateTestWebContents());
  NavigateAndCommit(GURL("http://www.google.com"));
  tester_ = base::MakeUnique<PageLoadMetricsObserverTester>(
      web_contents(),
      base::BindRepeating(
          &PageLoadMetricsObserverTestHarness::RegisterObservers,
          base::Unretained(this)));
  web_contents()->WasShown();
}

void PageLoadMetricsObserverTestHarness::StartNavigation(const GURL& gurl) {
  content::WebContentsTester* web_contents_tester =
      content::WebContentsTester::For(web_contents());
  web_contents_tester->StartNavigation(gurl);
}

void PageLoadMetricsObserverTestHarness::SimulateTimingUpdate(
    const mojom::PageLoadTiming& timing) {
  tester_->SimulateTimingAndMetadataUpdate(timing, mojom::PageLoadMetadata());
}

void PageLoadMetricsObserverTestHarness::SimulateTimingAndMetadataUpdate(
    const mojom::PageLoadTiming& timing,
    const mojom::PageLoadMetadata& metadata) {
  tester_->SimulateTimingAndMetadataUpdate(timing, metadata);
}

void PageLoadMetricsObserverTestHarness::SimulateFeaturesUpdate(
    const mojom::PageLoadFeatures& new_features) {
  tester_->SimulateFeaturesUpdate(new_features);
}

void PageLoadMetricsObserverTestHarness::SimulateLoadedResource(
    const ExtraRequestCompleteInfo& info) {
  tester_->SimulateLoadedResource(info, content::GlobalRequestID());
}

void PageLoadMetricsObserverTestHarness::SimulateLoadedResource(
    const ExtraRequestCompleteInfo& info,
    const content::GlobalRequestID& request_id) {
  tester_->SimulateLoadedResource(info, request_id);
}

void PageLoadMetricsObserverTestHarness::SimulateInputEvent(
    const blink::WebInputEvent& event) {
  tester_->SimulateInputEvent(event);
}

void PageLoadMetricsObserverTestHarness::SimulateAppEnterBackground() {
  tester_->SimulateAppEnterBackground();
}

void PageLoadMetricsObserverTestHarness::SimulateMediaPlayed() {
  tester_->SimulateMediaPlayed();
}

const base::HistogramTester&
PageLoadMetricsObserverTestHarness::histogram_tester() const {
  return histogram_tester_;
}

MetricsWebContentsObserver* PageLoadMetricsObserverTestHarness::observer()
    const {
  return tester_->observer();
}

const PageLoadExtraInfo
PageLoadMetricsObserverTestHarness::GetPageLoadExtraInfoForCommittedLoad() {
  return tester_->GetPageLoadExtraInfoForCommittedLoad();
}

void PageLoadMetricsObserverTestHarness::NavigateWithPageTransitionAndCommit(
    const GURL& url,
    ui::PageTransition transition) {
  controller().LoadURL(url, content::Referrer(), transition, std::string());
  content::WebContentsTester::For(web_contents())->CommitPendingNavigation();
}

void PageLoadMetricsObserverTestHarness::NavigateToUntrackedUrl() {
  NavigateAndCommit(GURL(url::kAboutBlankURL));
}

const char PageLoadMetricsObserverTestHarness::kResourceUrl[] =
    "https://www.example.com/resource";

}  // namespace page_load_metrics
