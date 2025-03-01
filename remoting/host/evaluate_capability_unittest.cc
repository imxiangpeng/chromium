// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/evaluate_capability.h"

#include "base/strings/string_util.h"
#include "remoting/host/switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

// TODO(zijiehe): Find out the root cause of the unexpected failure of this test
// case. See http://crbug.com/750330.
TEST(EvaluateCapabilityTest, DISABLED_ShouldReturnCrashResult) {
  ASSERT_NE(EvaluateCapability(kEvaluateCrash), 0);
}

TEST(EvaluateCapabilityTest, ShouldReturnExitCodeAndOutput) {
  std::string output;
  ASSERT_EQ(EvaluateCapability(kEvaluateTest, &output), 234);
  // New line character varies on different platform, so normalize the output
  // here.
  base::ReplaceSubstringsAfterOffset(&output, 0, "\r\n", "\n");
  base::ReplaceSubstringsAfterOffset(&output, 0, "\r", "\n");
  ASSERT_EQ("In EvaluateTest(): Line 1\n"
            "In EvaluateTest(): Line 2",
            output);
}

TEST(EvaluateCapabilityTest, ShouldForwardExitCodeAndOutput) {
  std::string output;
  ASSERT_EQ(EvaluateCapability(kEvaluateForward, &output), 234);
  // New line character varies on different platform, so normalize the output
  // here.
  base::ReplaceSubstringsAfterOffset(&output, 0, "\r\n", "\n");
  base::ReplaceSubstringsAfterOffset(&output, 0, "\r", "\n");
  // Windows (evilly) use \r\n to replace \n, so we will end up with two \n.
  base::ReplaceSubstringsAfterOffset(&output, 0, "\n\n", "\n");
  ASSERT_EQ("In EvaluateTest(): Line 1\n"
            "In EvaluateTest(): Line 2",
            output);
}

}  // namespace remoting
