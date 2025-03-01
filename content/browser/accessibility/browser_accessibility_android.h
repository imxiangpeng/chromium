// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_ANDROID_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_ANDROID_H_

#include <stddef.h>
#include <stdint.h>

#include "base/android/scoped_java_ref.h"
#include "base/macros.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "ui/accessibility/platform/ax_platform_node.h"

namespace content {

class CONTENT_EXPORT BrowserAccessibilityAndroid : public BrowserAccessibility {
 public:
  static BrowserAccessibilityAndroid* GetFromUniqueId(int32_t unique_id);
  int32_t unique_id() const { return unique_id_; }

  // Overrides from BrowserAccessibility.
  void OnDataChanged() override;
  bool IsNative() const override;
  void OnLocationChanged() override;
  base::string16 GetValue() const override;

  bool PlatformIsLeaf() const override;

  bool IsCheckable() const;
  bool IsChecked() const;
  bool IsClickable() const override;
  bool IsCollapsed() const;
  bool IsCollection() const;
  bool IsCollectionItem() const;
  bool IsContentInvalid() const;
  bool IsDismissable() const;
  bool IsEditableText() const;
  bool IsEnabled() const;
  bool IsExpanded() const;
  bool IsFocusable() const;
  bool IsFocused() const;
  bool IsHeading() const;
  bool IsHierarchical() const;
  bool IsLink() const;
  bool IsMultiLine() const;
  bool IsPassword() const;
  bool IsRangeType() const;
  bool IsScrollable() const;
  bool IsSelected() const;
  bool IsSlider() const;
  bool IsVisibleToUser() const;

  // This returns true for all nodes that we should navigate to.
  // Nodes that have a generic role, no accessible name, and aren't
  // focusable or clickable aren't interesting.
  bool IsInterestingOnAndroid() const;

  // If this node is interesting (IsInterestingOnAndroid() returns true),
  // returns |this|. If not, it recursively checks all of the
  // platform children of this node, and if just a single one is
  // interesting, returns that one. If no descendants are interesting, or
  // if more than one is interesting, returns nullptr.
  const BrowserAccessibilityAndroid* GetSoleInterestingNodeFromSubtree() const;

  bool CanOpenPopup() const;

  bool HasFocusableNonOptionChild() const;
  bool HasNonEmptyValue() const;

  const char* GetClassName() const;
  base::string16 GetText() const override;

  base::string16 GetRoleDescription() const;

  int GetItemIndex() const;
  int GetItemCount() const;

  bool CanScrollForward() const;
  bool CanScrollBackward() const;
  bool CanScrollUp() const;
  bool CanScrollDown() const;
  bool CanScrollLeft() const;
  bool CanScrollRight() const;
  int GetScrollX() const;
  int GetScrollY() const;
  int GetMinScrollX() const;
  int GetMinScrollY() const;
  int GetMaxScrollX() const;
  int GetMaxScrollY() const;
  bool Scroll(int direction) const;

  int GetTextChangeFromIndex() const;
  int GetTextChangeAddedCount() const;
  int GetTextChangeRemovedCount() const;
  base::string16 GetTextChangeBeforeText() const;

  int GetSelectionStart() const;
  int GetSelectionEnd() const;
  int GetEditableTextLength() const;

  int AndroidInputType() const;
  int AndroidLiveRegionType() const;
  int AndroidRangeType() const;

  int RowCount() const;
  int ColumnCount() const;

  int RowIndex() const;
  int RowSpan() const;
  int ColumnIndex() const;
  int ColumnSpan() const;

  float RangeMin() const;
  float RangeMax() const;
  float RangeCurrentValue() const;

  // Calls GetLineBoundaries or GetWordBoundaries depending on the value
  // of |granularity|, or fails if anything else is passed in |granularity|.
  void GetGranularityBoundaries(int granularity,
                                std::vector<int32_t>* starts,
                                std::vector<int32_t>* ends,
                                int offset);

  // Append line start and end indices for the text of this node
  // (as returned by GetText()), adding |offset| to each one.
  void GetLineBoundaries(std::vector<int32_t>* line_starts,
                         std::vector<int32_t>* line_ends,
                         int offset);

  // Append word start and end indices for the text of this node
  // (as returned by GetText()) to |word_starts| and |word_ends|,
  // adding |offset| to each one.
  void GetWordBoundaries(std::vector<int32_t>* word_starts,
                         std::vector<int32_t>* word_ends,
                         int offset);

 private:
  // This gives BrowserAccessibility::Create access to the class constructor.
  friend class BrowserAccessibility;

  BrowserAccessibilityAndroid();
  ~BrowserAccessibilityAndroid() override;

  bool HasOnlyTextChildren() const;
  bool HasOnlyTextAndImageChildren() const;
  bool IsIframe() const;

  void NotifyLiveRegionUpdate(base::string16& aria_live);

  int CountChildrenWithRole(ui::AXRole role) const;

  static size_t CommonPrefixLength(const base::string16 a,
                                   const base::string16 b);
  static size_t CommonSuffixLength(const base::string16 a,
                                   const base::string16 b);
  static size_t CommonEndLengths(const base::string16 a,
                                 const base::string16 b);

  base::string16 cached_text_;
  bool first_time_;
  base::string16 old_value_;
  base::string16 new_value_;
  int32_t unique_id_;

  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityAndroid);
};

}  // namespace content

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_ANDROID_H_
