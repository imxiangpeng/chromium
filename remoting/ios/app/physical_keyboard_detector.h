// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_IOS_PHYSICAL_KEYBOARD_DETECTOR_H_
#define REMOTING_IOS_PHYSICAL_KEYBOARD_DETECTOR_H_

#import <UIKit/UIKit.h>

// A class for detecting whether an physical keyboard is presented.
@interface PhysicalKeyboardDetector : NSObject

// |callback| will be called with YES if an physical keyboard is presented.
+ (void)detectOnView:(UIView*)view callback:(void (^)(BOOL))callback;

@end

#endif  // REMOTING_IOS_PHYSICAL_KEYBOARD_DETECTOR_H_
