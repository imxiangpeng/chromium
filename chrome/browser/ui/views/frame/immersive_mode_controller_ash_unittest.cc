// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/immersive_mode_controller_ash.h"

#include "ash/public/cpp/immersive/immersive_fullscreen_controller_test_api.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller_test.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller_ash.h"
#include "chrome/browser/ui/views/frame/test_with_browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "ui/aura/window.h"
#include "ui/views/controls/webview/webview.h"

class ImmersiveModeControllerAshTest : public TestWithBrowserView {
 public:
  ImmersiveModeControllerAshTest()
      : TestWithBrowserView(Browser::TYPE_TABBED, false) {}
  ImmersiveModeControllerAshTest(Browser::Type browser_type, bool hosted_app)
      : TestWithBrowserView(browser_type, hosted_app) {}
  ~ImmersiveModeControllerAshTest() override {}

  // TestWithBrowserView override:
  void SetUp() override {
    TestWithBrowserView::SetUp();

    browser()->window()->Show();

    controller_ = browser_view()->immersive_mode_controller();
    ASSERT_EQ(ImmersiveModeController::Type::ASH, controller_->type());
    ash::ImmersiveFullscreenControllerTestApi(
        static_cast<ImmersiveModeControllerAsh*>(controller_)->controller())
        .SetupForTest();
  }

  // Returns the bounds of |view| in widget coordinates.
  gfx::Rect GetBoundsInWidget(views::View* view) {
    return view->ConvertRectToWidget(view->GetLocalBounds());
  }

  // Toggle the browser's fullscreen state.
  void ToggleFullscreen() {
    // NOTIFICATION_FULLSCREEN_CHANGED is sent asynchronously. The notification
    // is used to trigger changes in whether the shelf is auto hidden and
    // whether a "light bar" version of the tab strip is used when the
    // top-of-window views are hidden.
    std::unique_ptr<FullscreenNotificationObserver> waiter(
        new FullscreenNotificationObserver());
    chrome::ToggleFullscreenMode(browser());
    waiter->Wait();
  }

  // Set whether the browser is in tab fullscreen.
  void SetTabFullscreen(bool tab_fullscreen) {
    content::WebContents* web_contents =
        browser_view()->GetContentsWebViewForTest()->GetWebContents();
    std::unique_ptr<FullscreenNotificationObserver> waiter(
        new FullscreenNotificationObserver());
    if (tab_fullscreen) {
      browser()
          ->exclusive_access_manager()
          ->fullscreen_controller()
          ->EnterFullscreenModeForTab(web_contents, GURL());
    } else {
      browser()
          ->exclusive_access_manager()
          ->fullscreen_controller()
          ->ExitFullscreenModeForTab(web_contents);
    }
    waiter->Wait();
  }

  // Attempt revealing the top-of-window views.
  void AttemptReveal() {
    if (!revealed_lock_.get()) {
      revealed_lock_.reset(controller_->GetRevealedLock(
          ImmersiveModeControllerAsh::ANIMATE_REVEAL_NO));
    }
  }

  // Attempt unrevealing the top-of-window views.
  void AttemptUnreveal() {
    revealed_lock_.reset();
  }

  ImmersiveModeController* controller() { return controller_; }

 private:
  // Not owned.
  ImmersiveModeController* controller_;

  std::unique_ptr<ImmersiveRevealedLock> revealed_lock_;

  DISALLOW_COPY_AND_ASSIGN(ImmersiveModeControllerAshTest);
};

// Test the layout and visibility of the tabstrip, toolbar and TopContainerView
// in immersive fullscreen.
TEST_F(ImmersiveModeControllerAshTest, Layout) {
  AddTab(browser(), GURL("about:blank"));

  TabStrip* tabstrip = browser_view()->tabstrip();
  ToolbarView* toolbar = browser_view()->toolbar();
  views::WebView* contents_web_view =
      browser_view()->GetContentsWebViewForTest();

  // Immersive fullscreen starts out disabled.
  ASSERT_FALSE(browser_view()->GetWidget()->IsFullscreen());
  ASSERT_FALSE(controller()->IsEnabled());

  // By default, the tabstrip and toolbar should be visible.
  EXPECT_TRUE(tabstrip->visible());
  EXPECT_TRUE(toolbar->visible());

  ToggleFullscreen();
  EXPECT_TRUE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(toolbar->visible());
  // The browser's top chrome is completely offscreen with tapstrip visible.
  EXPECT_TRUE(tabstrip->visible());
  // Tabstrip and top container view should be completely offscreen.
  EXPECT_EQ(0, GetBoundsInWidget(tabstrip).bottom());
  EXPECT_EQ(0, GetBoundsInWidget(browser_view()->top_container()).bottom());

  // Since the tab strip and tool bar are both hidden in immersive fullscreen
  // mode, the web contents should extend to the edge of screen.
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // Revealing the top-of-window views should set the tab strip back to the
  // normal style and show the toolbar.
  AttemptReveal();
  EXPECT_TRUE(controller()->IsRevealed());
  EXPECT_TRUE(tabstrip->visible());
  EXPECT_TRUE(toolbar->visible());

  // The TopContainerView should be flush with the top edge of the widget. If
  // it is not flush with the top edge the immersive reveal animation looks
  // wonky.
  EXPECT_EQ(0, GetBoundsInWidget(browser_view()->top_container()).y());

  // The web contents should be at the same y position as they were when the
  // top-of-window views were hidden.
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // Repeat the test for when in both immersive fullscreen and tab fullscreen.
  SetTabFullscreen(true);
  // Hide and reveal the top-of-window views so that they get relain out.
  AttemptUnreveal();
  AttemptReveal();

  // The tab strip and toolbar should still be visible and the TopContainerView
  // should still be flush with the top edge of the widget.
  EXPECT_TRUE(controller()->IsRevealed());
  EXPECT_TRUE(tabstrip->visible());
  EXPECT_TRUE(toolbar->visible());
  EXPECT_EQ(0, GetBoundsInWidget(browser_view()->top_container()).y());

  // The web contents should be flush with the top edge of the widget when in
  // both immersive and tab fullscreen.
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // Hide the top-of-window views. Tabstrip is still considered as visible.
  AttemptUnreveal();
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_FALSE(toolbar->visible());
  EXPECT_TRUE(tabstrip->visible());

  // The web contents should still be flush with the edge of the widget.
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // Exiting both immersive and tab fullscreen should show the tab strip and
  // toolbar.
  ToggleFullscreen();
  EXPECT_FALSE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());
  EXPECT_TRUE(tabstrip->visible());
  EXPECT_TRUE(toolbar->visible());
}

// Test that the browser commands which are usually disabled in fullscreen are
// are enabled in immersive fullscreen.
TEST_F(ImmersiveModeControllerAshTest, EnabledCommands) {
  ASSERT_FALSE(controller()->IsEnabled());
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_OPEN_CURRENT_URL));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ABOUT));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FOCUS_LOCATION));

  ToggleFullscreen();
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_OPEN_CURRENT_URL));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_ABOUT));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FOCUS_LOCATION));
}

// Test that restoring a window properly exits immersive fullscreen.
TEST_F(ImmersiveModeControllerAshTest, ExitUponRestore) {
  ASSERT_FALSE(controller()->IsEnabled());
  ToggleFullscreen();
  AttemptReveal();
  ASSERT_TRUE(controller()->IsEnabled());
  ASSERT_TRUE(controller()->IsRevealed());
  ASSERT_TRUE(browser_view()->GetWidget()->IsFullscreen());

  browser_view()->GetWidget()->Restore();
  EXPECT_FALSE(controller()->IsEnabled());
}

// Test the shelf visibility affected by entering and exiting tab fullscreen and
// immersive fullscreen.
TEST_F(ImmersiveModeControllerAshTest, TabAndBrowserFullscreen) {
  AddTab(browser(), GURL("about:blank"));

  // The shelf should start out as visible.
  ash::ShelfLayoutManager* shelf =
      ash::Shell::GetPrimaryRootWindowController()->GetShelfLayoutManager();
  ASSERT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());

  // 1) Test that entering tab fullscreen from immersive fullscreen hides
  // the shelf.
  ToggleFullscreen();
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());

  SetTabFullscreen(true);
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_HIDDEN, shelf->visibility_state());

  // 2) Test that exiting tab fullscreen autohides the shelf.
  SetTabFullscreen(false);
  ASSERT_TRUE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_AUTO_HIDE, shelf->visibility_state());

  // 3) Test that exiting tab fullscreen and immersive fullscreen correctly
  // updates the shelf visibility.
  SetTabFullscreen(true);
  ToggleFullscreen();
  ASSERT_FALSE(controller()->IsEnabled());
  EXPECT_EQ(ash::SHELF_VISIBLE, shelf->visibility_state());
}

// Ensure the circular tab-loading throbbers are not painted as layers in
// immersive fullscreen, since the tab strip may animate in or out without
// moving the layers.
TEST_F(ImmersiveModeControllerAshTest, LayeredSpinners) {
  AddTab(browser(), GURL("about:blank"));

  TabStrip* tabstrip = browser_view()->tabstrip();

  // Immersive fullscreen starts out disabled; layers are OK.
  EXPECT_FALSE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_TRUE(tabstrip->CanPaintThrobberToLayer());

  ToggleFullscreen();
  EXPECT_TRUE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(tabstrip->CanPaintThrobberToLayer());

  ToggleFullscreen();
  EXPECT_TRUE(tabstrip->CanPaintThrobberToLayer());
}

class ImmersiveModeControllerAshTestHostedApp
    : public ImmersiveModeControllerAshTest {
 public:
  ImmersiveModeControllerAshTestHostedApp()
      : ImmersiveModeControllerAshTest(Browser::TYPE_POPUP, true) {}
  ~ImmersiveModeControllerAshTestHostedApp() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ImmersiveModeControllerAshTestHostedApp);
};

// Test the layout and visibility of the TopContainerView and web contents when
// a hosted app is put into immersive fullscreen.
TEST_F(ImmersiveModeControllerAshTestHostedApp, Layout) {
  // Add a tab because the browser starts out without any tabs at all.
  AddTab(browser(), GURL("about:blank"));

  TabStrip* tabstrip = browser_view()->tabstrip();
  ToolbarView* toolbar = browser_view()->toolbar();
  views::WebView* contents_web_view =
      browser_view()->GetContentsWebViewForTest();
  views::View* top_container = browser_view()->top_container();

  // Immersive fullscreen starts out disabled.
  ASSERT_FALSE(browser_view()->GetWidget()->IsFullscreen());
  ASSERT_FALSE(controller()->IsEnabled());

  // The tabstrip and toolbar are not visible for hosted apps.
  EXPECT_FALSE(tabstrip->visible());
  EXPECT_FALSE(toolbar->visible());

  // The window header should be above the web contents.
  int header_height = GetBoundsInWidget(contents_web_view).y();

  ToggleFullscreen();
  EXPECT_TRUE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_TRUE(controller()->IsEnabled());
  EXPECT_FALSE(controller()->IsRevealed());

  // Entering immersive fullscreen should make the web contents flush with the
  // top of the widget. The popup browser type doesn't support tabstrip and
  // toolbar feature, thus invisible.
  EXPECT_FALSE(tabstrip->visible());
  EXPECT_FALSE(toolbar->visible());
  EXPECT_TRUE(top_container->GetVisibleBounds().IsEmpty());
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // Reveal the window header.
  AttemptReveal();

  // The tabstrip and toolbar should still be hidden and the web contents should
  // still be flush with the top of the screen.
  EXPECT_FALSE(tabstrip->visible());
  EXPECT_FALSE(toolbar->visible());
  EXPECT_EQ(0, GetBoundsInWidget(contents_web_view).y());

  // During an immersive reveal, the window header should be painted to the
  // TopContainerView. The TopContainerView should be flush with the top of the
  // widget and have |header_height|.
  gfx::Rect top_container_bounds_in_widget(GetBoundsInWidget(top_container));
  EXPECT_EQ(0, top_container_bounds_in_widget.y());
  EXPECT_EQ(header_height, top_container_bounds_in_widget.height());

  // Exit immersive fullscreen. The web contents should be back below the window
  // header.
  ToggleFullscreen();
  EXPECT_FALSE(browser_view()->GetWidget()->IsFullscreen());
  EXPECT_FALSE(controller()->IsEnabled());
  EXPECT_FALSE(tabstrip->visible());
  EXPECT_FALSE(toolbar->visible());
  EXPECT_EQ(header_height, GetBoundsInWidget(contents_web_view).y());
}
