// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/collection_view/collection_view_controller.h"
#import "ios/chrome/browser/ui/content_suggestions/content_suggestions_collection_controlling.h"

@class ContentSuggestionsSectionInformation;
@protocol ContentSuggestionsCommands;
@protocol ContentSuggestionsDataSource;
@protocol ContentSuggestionsHeaderSynchronizing;
@protocol ContentSuggestionsViewControllerAudience;
@protocol ContentSuggestionsViewControllerDelegate;
@protocol OverscrollActionsControllerDelegate;
@protocol SuggestedContent;

// CollectionViewController to display the suggestions items.
@interface ContentSuggestionsViewController
    : CollectionViewController<ContentSuggestionsCollectionControlling>

- (instancetype)initWithStyle:(CollectionViewControllerStyle)style
                   dataSource:(id<ContentSuggestionsDataSource>)dataSource
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithLayout:(UICollectionViewLayout*)layout
                         style:(CollectionViewControllerStyle)style
    NS_UNAVAILABLE;

// Handler for the commands sent by the ContentSuggestionsViewController.
@property(nonatomic, weak) id<ContentSuggestionsCommands>
    suggestionCommandHandler;
@property(nonatomic, weak) id<ContentSuggestionsHeaderSynchronizing>
    headerCommandHandler;
@property(nonatomic, weak) id<ContentSuggestionsViewControllerAudience>
    audience;
// Override from superclass to have a more specific type.
@property(nonatomic, readonly)
    CollectionViewModel<CollectionViewItem<SuggestedContent>*>*
        collectionViewModel;
// Delegate for the overscroll actions.
@property(nonatomic, weak) id<OverscrollActionsControllerDelegate>
    overscrollDelegate;

// Removes the entry at |indexPath|, from the collection and its model.
- (void)dismissEntryAtIndexPath:(NSIndexPath*)indexPath;
// Removes the |section|.
- (void)dismissSection:(NSInteger)section;
// Adds the |suggestions| to the collection and its model in the section
// corresponding to |sectionInfo|.
- (void)addSuggestions:
            (NSArray<CollectionViewItem<SuggestedContent>*>*)suggestions
         toSectionInfo:(ContentSuggestionsSectionInformation*)sectionInfo;

// Returns the accessibility identifier of the collection.
+ (NSString*)collectionAccessibilityIdentifier;

@end

#endif  // IOS_CHROME_BROWSER_UI_CONTENT_SUGGESTIONS_CONTENT_SUGGESTIONS_VIEW_CONTROLLER_H_
