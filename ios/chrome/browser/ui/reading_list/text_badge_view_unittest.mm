// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/reading_list/text_badge_view.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

// Test that the |text| property is set during initialization.
TEST(TextBadgeViewTest, CreateBadge) {
  TextBadgeView* badge = [[TextBadgeView alloc] initWithText:@"text"];
  EXPECT_NSEQ(@"text", badge.text);
}

// Test setting the |text| property.
TEST(TextBadgeViewTest, SetText) {
  TextBadgeView* badge = [[TextBadgeView alloc] initWithText:@"text 1"];
  [badge setText:@"text 2"];
  EXPECT_NSEQ(@"text 2", badge.text);
}

// Test that the accessibility label matches the display text.
TEST(TextBadgeViewTest, Accessibility) {
  TextBadgeView* badge = [[TextBadgeView alloc] initWithText:@"display"];
  EXPECT_NSEQ(@"display", badge.accessibilityLabel);
}
