// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/authentication/signin_promo_item.h"

#import "ios/chrome/browser/ui/authentication/signin_promo_view.h"
#import "ios/chrome/browser/ui/authentication/signin_promo_view_configurator.h"
#import "ios/chrome/browser/ui/util/constraints_ui_util.h"
#include "ios/chrome/grit/ios_chromium_strings.h"
#include "ui/base/l10n/l10n_util.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@implementation SigninPromoItem

@synthesize configurator = _configurator;

- (instancetype)initWithType:(NSInteger)type {
  self = [super initWithType:type];
  if (self) {
    self.cellClass = [SigninPromoCell class];
  }
  return self;
}

#pragma mark - CollectionViewItem

- (void)configureCell:(SigninPromoCell*)cell {
  [super configureCell:cell];
  cell.signinPromoView.textLabel.text =
      l10n_util::GetNSString(IDS_IOS_SIGNIN_PROMO_SETTINGS);
  [_configurator configureSigninPromoView:cell.signinPromoView];
}

@end

@implementation SigninPromoCell

@synthesize signinPromoView = _signinPromoView;

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    UIView* contentView = self.contentView;
    _signinPromoView = [[SigninPromoView alloc] initWithFrame:self.bounds];
    _signinPromoView.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_signinPromoView];
    AddSameConstraints(_signinPromoView, contentView);
  }
  return self;
}

// Implements -layoutSubviews as per instructions in documentation for
// +[MDCCollectionViewCell cr_preferredHeightForWidth:forItem:].
- (void)layoutSubviews {
  [super layoutSubviews];

  // Adjust the text label preferredMaxLayoutWidth when the parent's width
  // changes, for instance on screen rotation.
  CGFloat parentWidth = CGRectGetWidth(self.bounds);
  _signinPromoView.textLabel.preferredMaxLayoutWidth =
      parentWidth - 2 * _signinPromoView.horizontalPadding;

  // Re-layout with the new preferred width to allow the label to adjust its
  // height.
  [super layoutSubviews];
}

@end
