// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/use_counter_page_load_metrics_observer.h"

#include "base/metrics/histogram_macros.h"

using WebFeature = blink::mojom::WebFeature;
using Features = page_load_metrics::mojom::PageLoadFeatures;

UseCounterPageLoadMetricsObserver::UseCounterPageLoadMetricsObserver() {}
UseCounterPageLoadMetricsObserver::~UseCounterPageLoadMetricsObserver() {}

void UseCounterPageLoadMetricsObserver::OnFeaturesUsageObserved(
    const Features& features) {
  for (auto feature : features.features) {
    // The usage of each feature should be only measured once. With OOPIF,
    // multiple child frames may send the same feature to the browser, skip if
    // feature has already been measured.
    if (features_recorded_.test(static_cast<size_t>(feature)))
      continue;
    UMA_HISTOGRAM_ENUMERATION(internal::kFeaturesHistogramName, feature,
                              WebFeature::kNumberOfFeatures);
    features_recorded_.set(static_cast<size_t>(feature));
  }
}
