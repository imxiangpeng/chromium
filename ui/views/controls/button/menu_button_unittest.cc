// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/menu_button.h"

#include <memory>
#include <utility>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/animation/test/ink_drop_host_view_test_api.h"
#include "ui/views/animation/test/test_ink_drop.h"
#include "ui/views/controls/button/menu_button_listener.h"
#include "ui/views/drag_controller.h"
#include "ui/views/test/views_test_base.h"

#if defined(USE_AURA)
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/drag_drop_client_observer.h"
#include "ui/events/event.h"
#include "ui/events/event_handler.h"
#endif

using base::ASCIIToUTF16;

namespace views {

using test::InkDropHostViewTestApi;
using test::TestInkDrop;

// A MenuButton subclass that provides access to some MenuButton internals.
class TestMenuButton : public MenuButton {
 public:
  explicit TestMenuButton(MenuButtonListener* menu_button_listener)
      : MenuButton(base::string16(ASCIIToUTF16("button")),
                   menu_button_listener,
                   false) {}

  ~TestMenuButton() override {}

  void SetInkDrop(std::unique_ptr<InkDrop> ink_drop) {
    InkDropHostViewTestApi(this).SetInkDrop(std::move(ink_drop));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestMenuButton);
};

class MenuButtonTest : public ViewsTestBase {
 public:
  MenuButtonTest() : widget_(nullptr), button_(nullptr), ink_drop_(nullptr) {}
  ~MenuButtonTest() override {}

  void TearDown() override {
    generator_.reset();
    if (widget_ && !widget_->IsClosed())
      widget_->Close();

    ViewsTestBase::TearDown();
  }

  Widget* widget() { return widget_; }
  TestMenuButton* button() { return button_; }
  ui::test::EventGenerator* generator() { return generator_.get(); }

 protected:
  TestInkDrop* ink_drop() { return ink_drop_; }

  // Creates a MenuButton with no button listener.
  void CreateMenuButtonWithNoListener() { CreateMenuButton(nullptr); }

  // Creates a MenuButton with a MenuButtonListener. In this case, when the
  // MenuButton is pushed, it notifies the MenuButtonListener to open a
  // drop-down menu.
  void CreateMenuButtonWithMenuButtonListener(
      MenuButtonListener* menu_button_listener) {
    CreateMenuButton(menu_button_listener);
  }

  gfx::Point GetOutOfButtonLocation() const {
    return gfx::Point(button_->x() - 1, button_->y() - 1);
  }

 private:
  void CreateMenuButton(MenuButtonListener* menu_button_listener) {
    CreateWidget();
    generator_.reset(new ui::test::EventGenerator(widget_->GetNativeWindow()));
    // Set initial mouse location in a consistent way so that the menu button we
    // are about to create initializes its hover state in a consistent manner.
    generator_->set_current_location(gfx::Point(10, 10));

    button_ = new TestMenuButton(menu_button_listener);
    button_->SetBoundsRect(gfx::Rect(0, 0, 200, 20));

    ink_drop_ = new test::TestInkDrop();
    test::InkDropHostViewTestApi(button_).SetInkDrop(
        base::WrapUnique(ink_drop_));

    widget_->SetContentsView(button_);
    widget_->Show();
  }

  void CreateWidget() {
    DCHECK(!widget_);

    widget_ = new Widget;
    Widget::InitParams params =
        CreateParams(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.bounds = gfx::Rect(0, 0, 200, 200);
    widget_->Init(params);
  }

  Widget* widget_;
  TestMenuButton* button_;
  std::unique_ptr<ui::test::EventGenerator> generator_;

  // Weak ptr, |button_| owns the instance.
  TestInkDrop* ink_drop_;

  DISALLOW_COPY_AND_ASSIGN(MenuButtonTest);
};

class TestButtonListener : public ButtonListener {
 public:
  TestButtonListener()
      : last_sender_(nullptr),
        last_sender_state_(Button::STATE_NORMAL),
        last_event_type_(ui::ET_UNKNOWN) {}
  ~TestButtonListener() override {}

  void ButtonPressed(Button* sender, const ui::Event& event) override {
    last_sender_ = sender;
    CustomButton* custom_button = CustomButton::AsCustomButton(sender);
    DCHECK(custom_button);
    last_sender_state_ = custom_button->state();
    last_event_type_ = event.type();
  }

  Button* last_sender() { return last_sender_; }
  Button::ButtonState last_sender_state() { return last_sender_state_; }
  ui::EventType last_event_type() { return last_event_type_; }

 private:
  Button* last_sender_;
  Button::ButtonState last_sender_state_;
  ui::EventType last_event_type_;

  DISALLOW_COPY_AND_ASSIGN(TestButtonListener);
};

class TestMenuButtonListener : public MenuButtonListener {
 public:
  TestMenuButtonListener()
      : last_source_(nullptr), last_source_state_(Button::STATE_NORMAL) {}
  ~TestMenuButtonListener() override {}

  void OnMenuButtonClicked(MenuButton* source,
                           const gfx::Point& point,
                           const ui::Event* event) override {
    last_source_ = source;
    CustomButton* custom_button = CustomButton::AsCustomButton(source);
    DCHECK(custom_button);
    last_source_state_ = custom_button->state();
  }

  View* last_source() { return last_source_; }
  Button::ButtonState last_source_state() { return last_source_state_; }

 private:
  View* last_source_;
  Button::ButtonState last_source_state_;

  DISALLOW_COPY_AND_ASSIGN(TestMenuButtonListener);
};

// A MenuButtonListener that will acquire a PressedLock in the
// OnMenuButtonClicked() method and optionally release it as well.
class PressStateMenuButtonListener : public MenuButtonListener {
 public:
  explicit PressStateMenuButtonListener(bool release_lock)
      : menu_button_(nullptr), release_lock_(release_lock) {}

  ~PressStateMenuButtonListener() override {}

  void set_menu_button(MenuButton* menu_button) { menu_button_ = menu_button; }

  void OnMenuButtonClicked(MenuButton* source,
                           const gfx::Point& point,
                           const ui::Event* event) override {
    pressed_lock_.reset(new MenuButton::PressedLock(menu_button_));
    if (release_lock_)
      pressed_lock_.reset();
  }

  void ReleasePressedLock() { pressed_lock_.reset(); }

 private:
  MenuButton* menu_button_;

  std::unique_ptr<MenuButton::PressedLock> pressed_lock_;

  // The |pressed_lock_| will be released when true.
  bool release_lock_;

  DISALLOW_COPY_AND_ASSIGN(PressStateMenuButtonListener);
};

// Basic implementation of a DragController, to test input behaviour for
// MenuButtons that can be dragged.
class TestDragController : public DragController {
 public:
  TestDragController() {}
  ~TestDragController() override {}

  void WriteDragDataForView(View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override {}

  int GetDragOperationsForView(View* sender, const gfx::Point& p) override {
    return ui::DragDropTypes::DRAG_MOVE;
  }

  bool CanStartDragForView(View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override {
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestDragController);
};

#if defined(USE_AURA)
// Basic implementation of a DragDropClient, tracking the state of the drag
// operation. While dragging addition mouse events are consumed, preventing the
// target view from receiving them.
class TestDragDropClient : public aura::client::DragDropClient,
                           public ui::EventHandler {
 public:
  TestDragDropClient();
  ~TestDragDropClient() override;

  // aura::client::DragDropClient:
  int StartDragAndDrop(const ui::OSExchangeData& data,
                       aura::Window* root_window,
                       aura::Window* source_window,
                       const gfx::Point& screen_location,
                       int operation,
                       ui::DragDropTypes::DragEventSource source) override;
  void DragCancel() override;
  bool IsDragDropInProgress() override;
  void AddObserver(aura::client::DragDropClientObserver* observer) override {}
  void RemoveObserver(aura::client::DragDropClientObserver* observer) override {
  }

  // ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override;

 private:
  // True while receiving ui::LocatedEvents for drag operations.
  bool drag_in_progress_;

  // Target window where drag operations are occuring.
  aura::Window* target_;

  DISALLOW_COPY_AND_ASSIGN(TestDragDropClient);
};

TestDragDropClient::TestDragDropClient()
    : drag_in_progress_(false), target_(nullptr) {
}

TestDragDropClient::~TestDragDropClient() {
}

int TestDragDropClient::StartDragAndDrop(
    const ui::OSExchangeData& data,
    aura::Window* root_window,
    aura::Window* source_window,
    const gfx::Point& screen_location,
    int operation,
    ui::DragDropTypes::DragEventSource source) {
  if (IsDragDropInProgress())
    return ui::DragDropTypes::DRAG_NONE;
  drag_in_progress_ = true;
  target_ = root_window;
  return operation;
}

void TestDragDropClient::DragCancel() {
  drag_in_progress_ = false;
}

bool TestDragDropClient::IsDragDropInProgress() {
  return drag_in_progress_;
}

void TestDragDropClient::OnMouseEvent(ui::MouseEvent* event) {
  if (!IsDragDropInProgress())
    return;
  switch (event->type()) {
    case ui::ET_MOUSE_DRAGGED:
      event->StopPropagation();
      break;
    case ui::ET_MOUSE_RELEASED:
      drag_in_progress_ = false;
      event->StopPropagation();
      break;
    default:
      break;
  }
}
#endif  // defined(USE_AURA)

class TestShowSiblingButtonListener : public MenuButtonListener {
 public:
  TestShowSiblingButtonListener() {}
  ~TestShowSiblingButtonListener() override {}

  void OnMenuButtonClicked(MenuButton* source,
                           const gfx::Point& point,
                           const ui::Event* event) override {
    // The MenuButton itself doesn't set the PRESSED state during Activate() or
    // OnMenuButtonClicked(). That should be handled by the MenuController or,
    // if no menu is shown, the listener.
    EXPECT_EQ(Button::STATE_HOVERED, static_cast<MenuButton*>(source)->state());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestShowSiblingButtonListener);
};

// Tests if the listener is notified correctly when a mouse click happens on a
// MenuButton that has a MenuButtonListener.
TEST_F(MenuButtonTest, ActivateDropDownOnMouseClick) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);

  generator()->ClickLeftButton();

  // Check that MenuButton has notified the listener, while it was in pressed
  // state.
  EXPECT_EQ(button(), menu_button_listener.last_source());
  EXPECT_EQ(Button::STATE_HOVERED, menu_button_listener.last_source_state());
}

// Tests that the ink drop center point is set from the mouse click point.
TEST_F(MenuButtonTest, InkDropCenterSetFromClick) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);

  gfx::Point click_point(6, 8);
  generator()->MoveMouseTo(click_point);
  generator()->ClickLeftButton();

  EXPECT_EQ(button(), menu_button_listener.last_source());
  EXPECT_EQ(
      click_point,
      InkDropHostViewTestApi(button()).GetInkDropCenterBasedOnLastEvent());
}

// Tests that the ink drop center point is set from the PressedLock constructor.
TEST_F(MenuButtonTest, InkDropCenterSetFromClickWithPressedLock) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);

  gfx::Point click_point(11, 7);
  ui::MouseEvent click_event(ui::EventType::ET_MOUSE_PRESSED, click_point,
                             click_point, base::TimeTicks(), 0, 0);
  MenuButton::PressedLock pressed_lock(button(), false, &click_event);

  EXPECT_EQ(Button::STATE_PRESSED, button()->state());
  EXPECT_EQ(
      click_point,
      InkDropHostViewTestApi(button()).GetInkDropCenterBasedOnLastEvent());
}

// Test that the MenuButton stays pressed while there are any PressedLocks.
TEST_F(MenuButtonTest, ButtonStateForMenuButtonsWithPressedLocks) {
  // Similarly for aura-mus-client the location of the cursor is not updated by
  // EventGenerator so that IsMouseHovered() checks the wrong thing.
  // https://crbug.com/615033.
  if (IsMus())
    return;

  CreateMenuButtonWithNoListener();

  // Move the mouse over the button; the button should be in a hovered state.
  generator()->MoveMouseTo(gfx::Point(10, 10));
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());

  // Introduce a PressedLock, which should make the button pressed.
  std::unique_ptr<MenuButton::PressedLock> pressed_lock1(
      new MenuButton::PressedLock(button()));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());

  // Even if we move the mouse outside of the button, it should remain pressed.
  generator()->MoveMouseTo(gfx::Point(300, 10));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());

  // Creating a new lock should obviously keep the button pressed.
  std::unique_ptr<MenuButton::PressedLock> pressed_lock2(
      new MenuButton::PressedLock(button()));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());

  // The button should remain pressed while any locks are active.
  pressed_lock1.reset();
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());

  // Reseting the final lock should return the button's state to normal...
  pressed_lock2.reset();
  EXPECT_EQ(Button::STATE_NORMAL, button()->state());

  // ...And it should respond to mouse movement again.
  generator()->MoveMouseTo(gfx::Point(10, 10));
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());

  // Test that the button returns to the appropriate state after the press; if
  // the mouse ends over the button, the button should be hovered.
  pressed_lock1.reset(new MenuButton::PressedLock(button()));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());
  pressed_lock1.reset();
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());

  // If the button is disabled before the pressed lock, it should be disabled
  // after the pressed lock.
  button()->SetState(Button::STATE_DISABLED);
  pressed_lock1.reset(new MenuButton::PressedLock(button()));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());
  pressed_lock1.reset();
  EXPECT_EQ(Button::STATE_DISABLED, button()->state());

  generator()->MoveMouseTo(gfx::Point(300, 10));

  // Edge case: the button is disabled, a pressed lock is added, and then the
  // button is re-enabled. It should be enabled after the lock is removed.
  pressed_lock1.reset(new MenuButton::PressedLock(button()));
  EXPECT_EQ(Button::STATE_PRESSED, button()->state());
  button()->SetState(Button::STATE_NORMAL);
  pressed_lock1.reset();
  EXPECT_EQ(Button::STATE_NORMAL, button()->state());
}

// Test that if a sibling menu is shown, the original menu button releases its
// PressedLock.
TEST_F(MenuButtonTest, PressedStateWithSiblingMenu) {
  TestShowSiblingButtonListener listener;
  CreateMenuButtonWithMenuButtonListener(&listener);

  // Move the mouse over the button; the button should be in a hovered state.
  generator()->MoveMouseTo(gfx::Point(10, 10));
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());
  generator()->ClickLeftButton();
  // Test is continued in TestShowSiblingButtonListener::OnMenuButtonClicked().
}

// Test that the MenuButton does not become pressed if it can be dragged, until
// a release occurs.
TEST_F(MenuButtonTest, DraggableMenuButtonActivatesOnRelease) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  TestDragController drag_controller;
  button()->set_drag_controller(&drag_controller);

  generator()->PressLeftButton();
  EXPECT_EQ(nullptr, menu_button_listener.last_source());

  generator()->ReleaseLeftButton();
  EXPECT_EQ(button(), menu_button_listener.last_source());
  EXPECT_EQ(Button::STATE_HOVERED, menu_button_listener.last_source_state());
}

TEST_F(MenuButtonTest, InkDropStateForMenuButtonActivationsWithoutListener) {
  CreateMenuButtonWithNoListener();
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  button()->Activate(nullptr);

  EXPECT_EQ(InkDropState::HIDDEN, ink_drop()->GetTargetInkDropState());
}

TEST_F(MenuButtonTest,
       InkDropStateForMenuButtonActivationsWithListenerThatDoesntAcquireALock) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  button()->Activate(nullptr);

  EXPECT_EQ(InkDropState::ACTION_TRIGGERED,
            ink_drop()->GetTargetInkDropState());
}

TEST_F(
    MenuButtonTest,
    InkDropStateForMenuButtonActivationsWithListenerThatDontReleaseAllLocks) {
  PressStateMenuButtonListener menu_button_listener(false);
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  menu_button_listener.set_menu_button(button());
  button()->Activate(nullptr);

  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());
}

TEST_F(MenuButtonTest,
       InkDropStateForMenuButtonActivationsWithListenerThatReleaseAllLocks) {
  PressStateMenuButtonListener menu_button_listener(true);
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  menu_button_listener.set_menu_button(button());
  button()->Activate(nullptr);

  EXPECT_EQ(InkDropState::DEACTIVATED, ink_drop()->GetTargetInkDropState());
}

TEST_F(MenuButtonTest, InkDropStateForMenuButtonsWithPressedLocks) {
  CreateMenuButtonWithNoListener();

  std::unique_ptr<MenuButton::PressedLock> pressed_lock1(
      new MenuButton::PressedLock(button()));

  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());

  std::unique_ptr<MenuButton::PressedLock> pressed_lock2(
      new MenuButton::PressedLock(button()));

  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());

  pressed_lock1.reset();
  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());

  pressed_lock2.reset();
  EXPECT_EQ(InkDropState::DEACTIVATED, ink_drop()->GetTargetInkDropState());
}

// Verifies only one ink drop animation is triggered when multiple PressedLocks
// are attached to a MenuButton.
TEST_F(MenuButtonTest, OneInkDropAnimationForReentrantPressedLocks) {
  CreateMenuButtonWithNoListener();

  std::unique_ptr<MenuButton::PressedLock> pressed_lock1(
      new MenuButton::PressedLock(button()));

  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);

  std::unique_ptr<MenuButton::PressedLock> pressed_lock2(
      new MenuButton::PressedLock(button()));

  EXPECT_EQ(InkDropState::ACTION_PENDING, ink_drop()->GetTargetInkDropState());
}

// Verifies the InkDropState is left as ACTIVATED if a PressedLock is active
// before another Activation occurs.
TEST_F(MenuButtonTest,
       InkDropStateForMenuButtonWithPressedLockBeforeActivation) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  MenuButton::PressedLock lock(button());

  button()->Activate(nullptr);

  EXPECT_EQ(InkDropState::ACTIVATED, ink_drop()->GetTargetInkDropState());
}

#if defined(USE_AURA)

// Tests that the MenuButton does not become pressed if it can be dragged, and a
// DragDropClient is processing the events.
TEST_F(MenuButtonTest, DraggableMenuButtonDoesNotActivateOnDrag) {
  // TODO: test uses GetContext(), which is not applicable to aura-mus.
  // http://crbug.com/663809.
  if (IsMus())
    return;
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  TestDragController drag_controller;
  button()->set_drag_controller(&drag_controller);

  TestDragDropClient drag_client;
  SetDragDropClient(GetContext(), &drag_client);
  button()->PrependPreTargetHandler(&drag_client);

  generator()->DragMouseBy(10, 0);
  EXPECT_EQ(nullptr, menu_button_listener.last_source());
  EXPECT_EQ(Button::STATE_NORMAL, menu_button_listener.last_source_state());
}

#endif  // USE_AURA

// No touch on desktop Mac. Tracked in http://crbug.com/445520.
#if !defined(OS_MACOSX) || defined(USE_AURA)

// Tests if the listener is notified correctly when a gesture tap happens on a
// MenuButton that has a MenuButtonListener.
TEST_F(MenuButtonTest, ActivateDropDownOnGestureTap) {
  // Similarly for aura-mus-client the location of the cursor is not updated by
  // EventGenerator so that IsMouseHovered() checks the wrong thing.
  // https://crbug.com/615033.
  if (IsMus())
    return;
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);

  // Move the mouse outside the menu button so that it doesn't impact the
  // button state.
  generator()->MoveMouseTo(400, 400);
  EXPECT_FALSE(button()->IsMouseHovered());

  generator()->GestureTapAt(gfx::Point(10, 10));

  // Check that MenuButton has notified the listener, while it was in pressed
  // state.
  EXPECT_EQ(button(), menu_button_listener.last_source());
  EXPECT_EQ(Button::STATE_HOVERED, menu_button_listener.last_source_state());

  // The button should go back to it's normal state since the gesture ended.
  EXPECT_EQ(Button::STATE_NORMAL, button()->state());
}

// Tests that the button enters a hovered state upon a tap down, before becoming
// pressed at activation.
TEST_F(MenuButtonTest, TouchFeedbackDuringTap) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  generator()->PressTouch();
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());

  generator()->ReleaseTouch();
  EXPECT_EQ(Button::STATE_HOVERED, menu_button_listener.last_source_state());
}

// Tests that a move event that exits the button returns it to the normal state,
// and that the button did not activate the listener.
TEST_F(MenuButtonTest, TouchFeedbackDuringTapCancel) {
  TestMenuButtonListener menu_button_listener;
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  generator()->PressTouch();
  EXPECT_EQ(Button::STATE_HOVERED, button()->state());

  generator()->MoveTouch(gfx::Point(10, 30));
  generator()->ReleaseTouch();
  EXPECT_EQ(Button::STATE_NORMAL, button()->state());
  EXPECT_EQ(nullptr, menu_button_listener.last_source());
}

#endif  // !defined(OS_MACOSX) || defined(USE_AURA)

TEST_F(MenuButtonTest, InkDropHoverWhenShowingMenu) {
  PressStateMenuButtonListener menu_button_listener(false);
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  menu_button_listener.set_menu_button(button());

  generator()->MoveMouseTo(GetOutOfButtonLocation());
  EXPECT_FALSE(ink_drop()->is_hovered());

  generator()->MoveMouseTo(button()->bounds().CenterPoint());
  EXPECT_TRUE(ink_drop()->is_hovered());

  generator()->PressLeftButton();
  EXPECT_FALSE(ink_drop()->is_hovered());
}

TEST_F(MenuButtonTest, InkDropIsHoveredAfterDismissingMenuWhenMouseOverButton) {
  PressStateMenuButtonListener menu_button_listener(false);
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  menu_button_listener.set_menu_button(button());

  generator()->MoveMouseTo(button()->bounds().CenterPoint());
  generator()->PressLeftButton();
  EXPECT_FALSE(ink_drop()->is_hovered());
  menu_button_listener.ReleasePressedLock();

  EXPECT_TRUE(ink_drop()->is_hovered());
}

TEST_F(MenuButtonTest,
       InkDropIsntHoveredAfterDismissingMenuWhenMouseOutsideButton) {
  PressStateMenuButtonListener menu_button_listener(false);
  CreateMenuButtonWithMenuButtonListener(&menu_button_listener);
  menu_button_listener.set_menu_button(button());

  generator()->MoveMouseTo(button()->bounds().CenterPoint());
  generator()->PressLeftButton();
  generator()->MoveMouseTo(GetOutOfButtonLocation());
  menu_button_listener.ReleasePressedLock();

  EXPECT_FALSE(ink_drop()->is_hovered());
}

}  // namespace views
