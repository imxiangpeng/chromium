// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/views/search_box_view.h"

#include <cctype>
#include <map>

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "ui/app_list/app_list_constants.h"
#include "ui/app_list/app_list_features.h"
#include "ui/app_list/test/app_list_test_view_delegate.h"
#include "ui/app_list/vector_icons/vector_icons.h"
#include "ui/app_list/views/app_list_view.h"
#include "ui/app_list/views/search_box_view_delegate.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/widget_test.h"

namespace app_list {
namespace test {

class KeyPressCounterView : public views::View {
 public:
  KeyPressCounterView() : count_(0) {}
  ~KeyPressCounterView() override {}

  int GetCountAndReset() {
    int count = count_;
    count_ = 0;
    return count;
  }

 private:
  // Overridden from views::View:
  bool OnKeyPressed(const ui::KeyEvent& key_event) override {
    if (!::isalnum(static_cast<int>(key_event.key_code()))) {
      ++count_;
      return true;
    }
    return false;
  }
  int count_;

  DISALLOW_COPY_AND_ASSIGN(KeyPressCounterView);
};

// These tests run with both FullscreenAppList enabled and disabled.
// TODO(crbug.com/743113) Unify the two test classes.
class SearchBoxViewTest : public views::test::WidgetTest,
                          public SearchBoxViewDelegate,
                          public testing::WithParamInterface<bool> {
 public:
  SearchBoxViewTest() = default;
  ~SearchBoxViewTest() override = default;

  // Overridden from testing::Test:
  void SetUp() override {
    views::test::WidgetTest::SetUp();

    if (testing::UnitTest::GetInstance()->current_test_info()->value_param()) {
      // Current test is parameterized.
      test_with_fullscreen_ = GetParam();
      if (test_with_fullscreen_) {
        scoped_feature_list_.InitAndEnableFeature(
            features::kEnableFullscreenAppList);
      }
    }

    gfx::NativeView parent = GetContext();
    app_list_view_ = new AppListView(&view_delegate_);
    app_list_view()->Initialize(parent, 0, false, false);

    widget_ = CreateTopLevelPlatformWidget();
    view_.reset(new SearchBoxView(this, &view_delegate_, app_list_view()));
    counter_view_ = new KeyPressCounterView();
    widget_->GetContentsView()->AddChildView(view());
    widget_->GetContentsView()->AddChildView(counter_view_);
    view()->set_contents_view(counter_view_);
  }

  void TearDown() override {
    view_.reset();
    app_list_view_->GetWidget()->Close();
    widget_->CloseNow();
    views::test::WidgetTest::TearDown();
  }

 protected:
  SearchBoxView* view() { return view_.get(); }
  AppListView* app_list_view() { return app_list_view_; }

  bool test_with_fullscreen() { return test_with_fullscreen_; }

  void SetLongAutoLaunchTimeout() {
    // Sets a long timeout that lasts longer than the test run.
    view_delegate_.set_auto_launch_timeout(base::TimeDelta::FromDays(1));
  }

  base::TimeDelta GetAutoLaunchTimeout() {
    return view_delegate_.GetAutoLaunchTimeout();
  }

  void ResetAutoLaunchTimeout() {
    view_delegate_.set_auto_launch_timeout(base::TimeDelta());
  }

  int GetContentsViewKeyPressCountAndReset() {
    return counter_view_->GetCountAndReset();
  }

  void KeyPress(ui::KeyboardCode key_code) {
    ui::KeyEvent event(ui::ET_KEY_PRESSED, key_code, ui::EF_NONE);
    view()->search_box()->OnKeyEvent(&event);
    // Emulates the input method.
    if (::isalnum(static_cast<int>(key_code))) {
      base::char16 character = ::tolower(static_cast<int>(key_code));
      view()->search_box()->InsertText(base::string16(1, character));
    }
  }

  std::string GetLastQueryAndReset() {
    base::string16 query = last_query_;
    last_query_.clear();
    return base::UTF16ToUTF8(query);
  }

  int GetQueryChangedCountAndReset() {
    int result = query_changed_count_;
    query_changed_count_ = 0;
    return result;
  }

 private:
  // Overridden from SearchBoxViewDelegate:
  void QueryChanged(SearchBoxView* sender) override {
    ++query_changed_count_;
    last_query_ = sender->search_box()->text();
  }

  void BackButtonPressed() override {}

  void SetSearchResultSelection(bool select) override {}

  AppListTestViewDelegate view_delegate_;
  views::Widget* widget_;
  AppListView* app_list_view_ = nullptr;
  std::unique_ptr<SearchBoxView> view_;
  KeyPressCounterView* counter_view_;
  base::string16 last_query_;
  int query_changed_count_ = 0;
  bool test_with_fullscreen_ = false;
  base::test::ScopedFeatureList scoped_feature_list_;

  DISALLOW_COPY_AND_ASSIGN(SearchBoxViewTest);
};

class SearchBoxViewFullscreenTest : public views::test::WidgetTest,
                                    public SearchBoxViewDelegate {
 public:
  SearchBoxViewFullscreenTest() {}
  ~SearchBoxViewFullscreenTest() override {}

  // Overridden from testing::Test:
  void SetUp() override {
    views::test::WidgetTest::SetUp();
    scoped_feature_list_.InitAndEnableFeature(
        app_list::features::kEnableFullscreenAppList);

    gfx::NativeView parent = GetContext();
    app_list_view_ = new AppListView(&view_delegate_);
    app_list_view_->Initialize(parent, 0, false, false);

    widget_ = CreateTopLevelPlatformWidget();
    view_.reset(new SearchBoxView(this, &view_delegate_, app_list_view()));
    widget_->SetBounds(gfx::Rect(0, 0, 300, 200));
    widget_->GetContentsView()->AddChildView(view());
  }

  void TearDown() override {
    view_.reset();
    app_list_view_->GetWidget()->Close();
    widget_->CloseNow();
    views::test::WidgetTest::TearDown();
  }

 protected:
  views::Widget* widget() { return widget_; }
  SearchBoxView* view() { return view_.get(); }
  AppListView* app_list_view() { return app_list_view_; }

  void SetSearchEngineIsGoogle(bool is_google) {
    view_delegate_.SetSearchEngineIsGoogle(is_google);
  }

  void SetSearchBoxActive(bool active) { view()->SetSearchBoxActive(active); }

  void KeyPress(ui::KeyboardCode key_code) {
    ui::KeyEvent event(ui::ET_KEY_PRESSED, key_code, ui::EF_NONE);
    view()->search_box()->OnKeyEvent(&event);
    // Emulates the input method.
    if (::isalnum(static_cast<int>(key_code))) {
      base::char16 character = ::tolower(static_cast<int>(key_code));
      view()->search_box()->InsertText(base::string16(1, character));
    }
  }

 private:
  // Overridden from SearchBoxViewDelegate:
  void QueryChanged(SearchBoxView* sender) override {}

  void BackButtonPressed() override {}

  void SetSearchResultSelection(bool select) override {}

  base::test::ScopedFeatureList scoped_feature_list_;

  AppListTestViewDelegate view_delegate_;
  views::Widget* widget_;
  AppListView* app_list_view_ = nullptr;
  std::unique_ptr<SearchBoxView> view_;

  DISALLOW_COPY_AND_ASSIGN(SearchBoxViewFullscreenTest);
};

// Instantiate the Boolean which is used to toggle the Fullscreen app list in
// the parameterized tests.
INSTANTIATE_TEST_CASE_P(, SearchBoxViewTest, testing::Bool());

TEST_P(SearchBoxViewTest, Basic) {
  KeyPress(ui::VKEY_A);
  EXPECT_EQ("a", GetLastQueryAndReset());
  EXPECT_EQ(1, GetQueryChangedCountAndReset());
  EXPECT_EQ(0, GetContentsViewKeyPressCountAndReset());

  KeyPress(ui::VKEY_DOWN);
  EXPECT_EQ(0, GetQueryChangedCountAndReset());
  EXPECT_EQ(1, GetContentsViewKeyPressCountAndReset());

  view()->ClearSearch();
  EXPECT_EQ(1, GetQueryChangedCountAndReset());
  EXPECT_TRUE(GetLastQueryAndReset().empty());
}

TEST_P(SearchBoxViewTest, CancelAutoLaunch) {
  SetLongAutoLaunchTimeout();
  ASSERT_NE(base::TimeDelta(), GetAutoLaunchTimeout());

  // Normal key event cancels the timeout.
  KeyPress(ui::VKEY_A);
  EXPECT_EQ(base::TimeDelta(), GetAutoLaunchTimeout());
  ResetAutoLaunchTimeout();

  // Unusual key event doesn't cancel -- it will be canceled in
  // SearchResultListView.
  SetLongAutoLaunchTimeout();
  KeyPress(ui::VKEY_DOWN);
  EXPECT_NE(base::TimeDelta(), GetAutoLaunchTimeout());
  ResetAutoLaunchTimeout();

  // Clearing search box also cancels.
  SetLongAutoLaunchTimeout();
  view()->ClearSearch();
  EXPECT_EQ(base::TimeDelta(), GetAutoLaunchTimeout());
}

TEST_F(SearchBoxViewFullscreenTest, CloseButtonTest) {
  EXPECT_FALSE(view()->close_button()->visible());
  EXPECT_EQ(AppListView::PEEKING, app_list_view()->app_list_state());

  KeyPress(ui::VKEY_A);
  EXPECT_TRUE(view()->close_button()->visible());
  EXPECT_EQ(AppListView::HALF, app_list_view()->app_list_state());

  // Click the close button in search box view.
  view()->ButtonPressed(
      view()->close_button(),
      ui::MouseEvent(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                     base::TimeTicks(), ui::EF_LEFT_MOUSE_BUTTON,
                     ui::EF_LEFT_MOUSE_BUTTON));
  EXPECT_FALSE(view()->close_button()->visible());
  EXPECT_EQ(AppListView::PEEKING, app_list_view()->app_list_state());
}

// Tests that the search box is inactive by default.
TEST_F(SearchBoxViewFullscreenTest, SearchBoxInactiveByDefault) {
  ASSERT_FALSE(view()->is_search_box_active());
}

// Tests that the black Google icon is used for an inactive Google search.
TEST_F(SearchBoxViewFullscreenTest, SearchBoxInactiveSearchBoxGoogle) {
  SetSearchEngineIsGoogle(true);
  SetSearchBoxActive(false);
  const gfx::ImageSkia expected_icon = gfx::CreateVectorIcon(
      kIcGoogleBlackIcon, kSearchIconSize, kDefaultSearchboxColor);
  view()->ModelChanged();

  const gfx::ImageSkia actual_icon =
      view()->get_search_icon_for_test()->GetImage();

  EXPECT_TRUE(gfx::test::AreBitmapsEqual(*expected_icon.bitmap(),
                                         *actual_icon.bitmap()));
}

// Tests that the colored Google icon is used for an active Google search.
TEST_F(SearchBoxViewFullscreenTest, SearchBoxActiveSearchEngineGoogle) {
  SetSearchEngineIsGoogle(true);
  SetSearchBoxActive(true);
  const gfx::ImageSkia expected_icon = gfx::CreateVectorIcon(
      kIcGoogleColorIcon, kSearchIconSize, kDefaultSearchboxColor);
  view()->ModelChanged();

  const gfx::ImageSkia actual_icon =
      view()->get_search_icon_for_test()->GetImage();

  EXPECT_TRUE(gfx::test::AreBitmapsEqual(*expected_icon.bitmap(),
                                         *actual_icon.bitmap()));
}

// Tests that the non-Google icon is used for an inactive non-Google search.
TEST_F(SearchBoxViewFullscreenTest, SearchBoxInactiveSearchEngineNotGoogle) {
  SetSearchEngineIsGoogle(false);
  SetSearchBoxActive(false);
  const gfx::ImageSkia expected_icon = gfx::CreateVectorIcon(
      kIcSearchEngineNotGoogleIcon, kSearchIconSize, kDefaultSearchboxColor);
  view()->ModelChanged();

  const gfx::ImageSkia actual_icon =
      view()->get_search_icon_for_test()->GetImage();

  EXPECT_TRUE(gfx::test::AreBitmapsEqual(*expected_icon.bitmap(),
                                         *actual_icon.bitmap()));
}

// Tests that the non-Google icon is used for an active non-Google search.
TEST_F(SearchBoxViewFullscreenTest, SearchBoxActiveSearchEngineNotGoogle) {
  SetSearchEngineIsGoogle(false);
  SetSearchBoxActive(true);
  const gfx::ImageSkia expected_icon = gfx::CreateVectorIcon(
      kIcSearchEngineNotGoogleIcon, kSearchIconSize, kDefaultSearchboxColor);
  view()->ModelChanged();

  const gfx::ImageSkia actual_icon =
      view()->get_search_icon_for_test()->GetImage();

  EXPECT_TRUE(gfx::test::AreBitmapsEqual(*expected_icon.bitmap(),
                                         *actual_icon.bitmap()));
}

}  // namespace test
}  // namespace app_list
