// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/native_widget_aura.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "services/ui/public/interfaces/window_manager.mojom.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "services/ui/public/interfaces/window_tree_constants.mojom.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/capture_client.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/client/window_types.h"
#include "ui/aura/env.h"
#include "ui/aura/mus/property_utils.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/class_property.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/native_theme/native_theme_aura.h"
#include "ui/views/drag_utils.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/drop_helper.h"
#include "ui/views/widget/focus_manager_event_handler.h"
#include "ui/views/widget/native_widget_delegate.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/tooltip_manager_aura.h"
#include "ui/views/widget/widget_aura_utils.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/window_reorderer.h"
#include "ui/wm/core/shadow_types.h"
#include "ui/wm/core/transient_window_manager.h"
#include "ui/wm/core/window_animations.h"
#include "ui/wm/core/window_util.h"
#include "ui/wm/public/activation_client.h"
#include "ui/wm/public/window_move_client.h"

#if defined(OS_WIN)
#include "base/win/scoped_gdi_object.h"
#include "base/win/win_util.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_win.h"
#endif

#if defined(USE_X11) && !defined(OS_CHROMEOS)
#include "ui/views/linux_ui/linux_ui.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_x11.h"
#endif

#if !defined(OS_CHROMEOS)
#include "ui/views/widget/desktop_aura/desktop_native_widget_aura.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host.h"
#endif

DECLARE_UI_CLASS_PROPERTY_TYPE(views::internal::NativeWidgetPrivate*)

namespace views {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(internal::NativeWidgetPrivate*,
                           kNativeWidgetPrivateKey,
                           nullptr);

void SetRestoreBounds(aura::Window* window, const gfx::Rect& bounds) {
  window->SetProperty(aura::client::kRestoreBoundsKey, new gfx::Rect(bounds));
}

void SetIcon(aura::Window* window,
             const aura::WindowProperty<gfx::ImageSkia*>* key,
             const gfx::ImageSkia& value) {
  if (value.isNull())
    window->ClearProperty(key);
  else
    window->SetProperty(key, new gfx::ImageSkia(value));
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, public:

NativeWidgetAura::NativeWidgetAura(internal::NativeWidgetDelegate* delegate,
                                   bool is_parallel_widget_in_window_manager)
    : delegate_(delegate),
      is_parallel_widget_in_window_manager_(
          is_parallel_widget_in_window_manager),
      window_(new aura::Window(this)),
      ownership_(Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET),
      destroying_(false),
      cursor_(gfx::kNullCursor),
      close_widget_factory_(this) {
  aura::client::SetFocusChangeObserver(window_, this);
  wm::SetActivationChangeObserver(window_, this);
}

// static
void NativeWidgetAura::RegisterNativeWidgetForWindow(
      internal::NativeWidgetPrivate* native_widget,
      aura::Window* window) {
  window->SetProperty(kNativeWidgetPrivateKey, native_widget);
}

// static
void NativeWidgetAura::AssignIconToAuraWindow(aura::Window* window,
                                              const gfx::ImageSkia& window_icon,
                                              const gfx::ImageSkia& app_icon) {
  if (window) {
    SetIcon(window, aura::client::kWindowIconKey, window_icon);
    SetIcon(window, aura::client::kAppIconKey, app_icon);
  }
}

// static
void NativeWidgetAura::SetShadowElevationFromInitParams(
    aura::Window* window,
    const Widget::InitParams& params) {
  if (params.shadow_type == Widget::InitParams::SHADOW_TYPE_NONE) {
    SetShadowElevation(window, wm::ShadowElevation::NONE);
  } else if (params.shadow_type == Widget::InitParams::SHADOW_TYPE_DROP &&
             params.shadow_elevation) {
    SetShadowElevation(window, *params.shadow_elevation);
  }
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, internal::NativeWidgetPrivate implementation:

void NativeWidgetAura::InitNativeWidget(const Widget::InitParams& params) {
  // Aura needs to know which desktop (Ash or regular) will manage this widget.
  // See Widget::InitParams::context for details.
  DCHECK(params.parent || params.context);

  ownership_ = params.ownership;

  RegisterNativeWidgetForWindow(this, window_);
  // MusClient has assertions that ui::mojom::WindowType matches
  // views::Widget::InitParams::Type.
  aura::SetWindowType(window_, static_cast<ui::mojom::WindowType>(params.type));
  window_->SetProperty(aura::client::kShowStateKey, params.show_state);
  if (params.type == Widget::InitParams::TYPE_BUBBLE)
    wm::SetHideOnDeactivate(window_, true);
  window_->SetTransparent(
      params.opacity == Widget::InitParams::TRANSLUCENT_WINDOW);
  window_->Init(params.layer_type);
  // Set name after layer init so it propagates to layer.
  window_->SetName(params.name);
  SetShadowElevationFromInitParams(window_, params);
  if (params.type == Widget::InitParams::TYPE_CONTROL)
    window_->Show();

  delegate_->OnNativeWidgetCreated(false);

  gfx::Rect window_bounds = params.bounds;
  gfx::NativeView parent = params.parent;
  gfx::NativeView context = params.context;
  if (!params.child) {
    // Set up the transient child before the window is added. This way the
    // LayoutManager knows the window has a transient parent.
    if (parent && parent->type() != aura::client::WINDOW_TYPE_UNKNOWN) {
      wm::AddTransientChild(parent, window_);
      if (!context)
        context = parent;
      parent = NULL;

      // Generally transient bubbles are showing state associated to the parent
      // window. Make sure the transient bubble is only visible if the parent is
      // visible, otherwise the bubble may not make sense by itself.
      if (params.type == Widget::InitParams::TYPE_BUBBLE) {
        wm::TransientWindowManager::Get(window_)
            ->set_parent_controls_visibility(true);
      }
    }
    // SetAlwaysOnTop before SetParent so that always-on-top container is used.
    SetAlwaysOnTop(params.keep_on_top);
    // Make sure we have a real |window_bounds|.
    if (parent && window_bounds == gfx::Rect()) {
      // If a parent is specified but no bounds are given,
      // use the origin of the parent's display so that the widget
      // will be added to the same display as the parent.
      gfx::Rect bounds = display::Screen::GetScreen()
                             ->GetDisplayNearestWindow(parent)
                             .bounds();
      window_bounds.set_origin(bounds.origin());
    }
  }

  // Set properties before adding to the parent so that its layout manager sees
  // the correct values.
  OnSizeConstraintsChanged();

  if (parent) {
    parent->AddChild(window_);
  } else {
    aura::client::ParentWindowWithContext(
        window_, context->GetRootWindow(), window_bounds);
  }

  // Start observing property changes.
  window_->AddObserver(this);

  // Wait to set the bounds until we have a parent. That way we can know our
  // true state/bounds (the LayoutManager may enforce a particular
  // state/bounds).
  if (IsMaximized())
    SetRestoreBounds(window_, window_bounds);
  else
    SetBounds(window_bounds);
  window_->SetEventTargetingPolicy(
      params.accept_events
          ? ui::mojom::EventTargetingPolicy::TARGET_AND_DESCENDANTS
          : ui::mojom::EventTargetingPolicy::NONE);
  DCHECK(GetWidget()->GetRootView());
  if (params.type != Widget::InitParams::TYPE_TOOLTIP)
    tooltip_manager_.reset(new views::TooltipManagerAura(GetWidget()));

  drop_helper_.reset(new DropHelper(GetWidget()->GetRootView()));
  if (params.type != Widget::InitParams::TYPE_TOOLTIP &&
      params.type != Widget::InitParams::TYPE_POPUP) {
    aura::client::SetDragDropDelegate(window_, this);
  }

  if (params.type == Widget::InitParams::TYPE_WINDOW) {
    focus_manager_event_handler_ =
        base::MakeUnique<FocusManagerEventHandler>(GetWidget(), window_);
  }

  wm::SetActivationDelegate(window_, this);

  window_reorderer_.reset(new WindowReorderer(window_,
      GetWidget()->GetRootView()));
}

void NativeWidgetAura::OnWidgetInitDone() {}

NonClientFrameView* NativeWidgetAura::CreateNonClientFrameView() {
  return NULL;
}

bool NativeWidgetAura::ShouldUseNativeFrame() const {
  // There is only one frame type for aura.
  return false;
}

bool NativeWidgetAura::ShouldWindowContentsBeTransparent() const {
  return false;
}

void NativeWidgetAura::FrameTypeChanged() {
  // This is called when the Theme has changed; forward the event to the root
  // widget.
  GetWidget()->ThemeChanged();
  GetWidget()->GetRootView()->SchedulePaint();
}

Widget* NativeWidgetAura::GetWidget() {
  return delegate_->AsWidget();
}

const Widget* NativeWidgetAura::GetWidget() const {
  return delegate_->AsWidget();
}

gfx::NativeView NativeWidgetAura::GetNativeView() const {
  return window_;
}

gfx::NativeWindow NativeWidgetAura::GetNativeWindow() const {
  return window_;
}

Widget* NativeWidgetAura::GetTopLevelWidget() {
  NativeWidgetPrivate* native_widget = GetTopLevelNativeWidget(GetNativeView());
  return native_widget ? native_widget->GetWidget() : NULL;
}

const ui::Compositor* NativeWidgetAura::GetCompositor() const {
  return window_ ? window_->layer()->GetCompositor() : NULL;
}

const ui::Layer* NativeWidgetAura::GetLayer() const {
  return window_ ? window_->layer() : NULL;
}

void NativeWidgetAura::ReorderNativeViews() {
  window_reorderer_->ReorderChildWindows();
}

void NativeWidgetAura::ViewRemoved(View* view) {
  DCHECK(drop_helper_.get() != NULL);
  drop_helper_->ResetTargetViewIfEquals(view);
}

void NativeWidgetAura::SetNativeWindowProperty(const char* name, void* value) {
  if (window_)
    window_->SetNativeWindowProperty(name, value);
}

void* NativeWidgetAura::GetNativeWindowProperty(const char* name) const {
  return window_ ? window_->GetNativeWindowProperty(name) : NULL;
}

TooltipManager* NativeWidgetAura::GetTooltipManager() const {
  return tooltip_manager_.get();
}

void NativeWidgetAura::SetCapture() {
  if (window_)
    window_->SetCapture();
}

void NativeWidgetAura::ReleaseCapture() {
  if (window_)
    window_->ReleaseCapture();
}

bool NativeWidgetAura::HasCapture() const {
  return window_ && window_->HasCapture();
}

ui::InputMethod* NativeWidgetAura::GetInputMethod() {
  if (!window_)
    return nullptr;
  aura::Window* root_window = window_->GetRootWindow();
  return root_window ? root_window->GetHost()->GetInputMethod() : nullptr;
}

void NativeWidgetAura::CenterWindow(const gfx::Size& size) {
  if (!window_ || is_parallel_widget_in_window_manager_)
    return;

  window_->SetProperty(aura::client::kPreferredSize, new gfx::Size(size));

  gfx::Rect parent_bounds(window_->parent()->GetBoundsInRootWindow());
  // When centering window, we take the intersection of the host and
  // the parent. We assume the root window represents the visible
  // rect of a single screen.
  gfx::Rect work_area = display::Screen::GetScreen()
                            ->GetDisplayNearestWindow(window_)
                            .work_area();

  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(window_->GetRootWindow());
  if (screen_position_client) {
    gfx::Point origin = work_area.origin();
    screen_position_client->ConvertPointFromScreen(window_->GetRootWindow(),
                                                   &origin);
    work_area.set_origin(origin);
  }

  parent_bounds.Intersect(work_area);

  // If |window_|'s transient parent's bounds are big enough to fit it, then we
  // center it with respect to the transient parent.
  if (wm::GetTransientParent(window_)) {
    gfx::Rect transient_parent_rect =
        wm::GetTransientParent(window_)->GetBoundsInRootWindow();
    transient_parent_rect.Intersect(work_area);
    if (transient_parent_rect.height() >= size.height() &&
        transient_parent_rect.width() >= size.width())
      parent_bounds = transient_parent_rect;
  }

  gfx::Rect window_bounds(
      parent_bounds.x() + (parent_bounds.width() - size.width()) / 2,
      parent_bounds.y() + (parent_bounds.height() - size.height()) / 2,
      size.width(),
      size.height());
  // Don't size the window bigger than the parent, otherwise the user may not be
  // able to close or move it.
  window_bounds.AdjustToFit(parent_bounds);

  // Convert the bounds back relative to the parent.
  gfx::Point origin = window_bounds.origin();
  aura::Window::ConvertPointToTarget(window_->GetRootWindow(),
      window_->parent(), &origin);
  window_bounds.set_origin(origin);
  window_->SetBounds(window_bounds);
}

void NativeWidgetAura::GetWindowPlacement(
    gfx::Rect* bounds,
    ui::WindowShowState* show_state) const {
  // The interface specifies returning restored bounds, not current bounds.
  *bounds = GetRestoredBounds();
  *show_state = window_ ? window_->GetProperty(aura::client::kShowStateKey) :
      ui::SHOW_STATE_DEFAULT;
}

bool NativeWidgetAura::SetWindowTitle(const base::string16& title) {
  if (!window_ || is_parallel_widget_in_window_manager_)
    return false;
  if (window_->GetTitle() == title)
    return false;
  window_->SetTitle(title);
  return true;
}

void NativeWidgetAura::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                      const gfx::ImageSkia& app_icon) {
  if (!is_parallel_widget_in_window_manager_)
    AssignIconToAuraWindow(window_, window_icon, app_icon);
}

void NativeWidgetAura::InitModalType(ui::ModalType modal_type) {
  if (modal_type != ui::MODAL_TYPE_NONE)
    window_->SetProperty(aura::client::kModalKey, modal_type);
}

gfx::Rect NativeWidgetAura::GetWindowBoundsInScreen() const {
  return window_ ? window_->GetBoundsInScreen() : gfx::Rect();
}

gfx::Rect NativeWidgetAura::GetClientAreaBoundsInScreen() const {
  // View-to-screen coordinate system transformations depend on this returning
  // the full window bounds, for example View::ConvertPointToScreen().
  return window_ ? window_->GetBoundsInScreen() : gfx::Rect();
}

gfx::Rect NativeWidgetAura::GetRestoredBounds() const {
  if (!window_)
    return gfx::Rect();

  // Restored bounds should only be relevant if the window is minimized,
  // maximized, or fullscreen. However, in some places the code expects
  // GetRestoredBounds() to return the current window bounds if the window is
  // not in either state.
  if (IsMinimized() || IsMaximized() || IsFullscreen()) {
    // Restore bounds are in screen coordinates, no need to convert.
    gfx::Rect* restore_bounds =
        window_->GetProperty(aura::client::kRestoreBoundsKey);
    if (restore_bounds)
      return *restore_bounds;
  }
  return window_->GetBoundsInScreen();
}

std::string NativeWidgetAura::GetWorkspace() const {
  return std::string();
}

void NativeWidgetAura::SetBounds(const gfx::Rect& bounds) {
  if (!window_)
    return;

  aura::Window* root = window_->GetRootWindow();
  if (root) {
    aura::client::ScreenPositionClient* screen_position_client =
        aura::client::GetScreenPositionClient(root);
    if (screen_position_client) {
      display::Display dst_display =
          display::Screen::GetScreen()->GetDisplayMatching(bounds);
      screen_position_client->SetBounds(window_, bounds, dst_display);
      return;
    }
  }
  window_->SetBounds(bounds);
}

void NativeWidgetAura::SetSize(const gfx::Size& size) {
  if (window_)
    window_->SetBounds(gfx::Rect(window_->bounds().origin(), size));
}

void NativeWidgetAura::StackAbove(gfx::NativeView native_view) {
  if (window_ && window_->parent() &&
      window_->parent() == native_view->parent())
    window_->parent()->StackChildAbove(window_, native_view);
}

void NativeWidgetAura::StackAtTop() {
  if (window_)
    window_->parent()->StackChildAtTop(window_);
}

void NativeWidgetAura::SetShape(std::unique_ptr<SkRegion> region) {
  if (window_)
    window_->layer()->SetAlphaShape(std::move(region));
}

void NativeWidgetAura::Close() {
  // |window_| may already be deleted by parent window. This can happen
  // when this widget is child widget or has transient parent
  // and ownership is WIDGET_OWNS_NATIVE_WIDGET.
  DCHECK(window_ ||
         ownership_ == Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET);
  if (window_) {
    window_->SuppressPaint();
    Hide();
    window_->SetProperty(aura::client::kModalKey, ui::MODAL_TYPE_NONE);
  }

  if (!close_widget_factory_.HasWeakPtrs()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&NativeWidgetAura::CloseNow,
                              close_widget_factory_.GetWeakPtr()));
  }
}

void NativeWidgetAura::CloseNow() {
  delete window_;
}

void NativeWidgetAura::Show() {
  ShowWithWindowState(ui::SHOW_STATE_NORMAL);
}

void NativeWidgetAura::Hide() {
  if (window_)
    window_->Hide();
}

void NativeWidgetAura::ShowMaximizedWithBounds(
    const gfx::Rect& restored_bounds) {
  SetRestoreBounds(window_, restored_bounds);
  ShowWithWindowState(ui::SHOW_STATE_MAXIMIZED);
}

void NativeWidgetAura::ShowWithWindowState(ui::WindowShowState state) {
  if (!window_)
    return;

  if (state == ui::SHOW_STATE_MAXIMIZED || state == ui::SHOW_STATE_FULLSCREEN)
    window_->SetProperty(aura::client::kShowStateKey, state);
  window_->Show();
  if (delegate_->CanActivate()) {
    if (state != ui::SHOW_STATE_INACTIVE)
      Activate();
    // SetInitialFocus() should be always be called, even for
    // SHOW_STATE_INACTIVE. If the window has to stay inactive, the method will
    // do the right thing.
    // Activate() might fail if the window is non-activatable. In this case, we
    // should pass SHOW_STATE_INACTIVE to SetInitialFocus() to stop the initial
    // focused view from getting focused. See crbug.com/515594 for example.
    SetInitialFocus(IsActive() ? state : ui::SHOW_STATE_INACTIVE);
  }

  // On desktop aura, a window is activated first even when it is shown as
  // minimized. Do the same for consistency.
  if (state == ui::SHOW_STATE_MINIMIZED)
    Minimize();
}

bool NativeWidgetAura::IsVisible() const {
  return window_ && window_->IsVisible();
}

void NativeWidgetAura::Activate() {
  if (!window_)
    return;

  // We don't necessarily have a root window yet. This can happen with
  // constrained windows.
  if (window_->GetRootWindow()) {
    wm::GetActivationClient(window_->GetRootWindow())->ActivateWindow(window_);
  }
  if (window_->GetProperty(aura::client::kDrawAttentionKey))
    window_->SetProperty(aura::client::kDrawAttentionKey, false);
}

void NativeWidgetAura::Deactivate() {
  if (!window_)
    return;
  wm::GetActivationClient(window_->GetRootWindow())->DeactivateWindow(window_);
}

bool NativeWidgetAura::IsActive() const {
  return window_ && wm::IsActiveWindow(window_);
}

void NativeWidgetAura::SetAlwaysOnTop(bool on_top) {
  if (window_ && !is_parallel_widget_in_window_manager_)
    window_->SetProperty(aura::client::kAlwaysOnTopKey, on_top);
}

bool NativeWidgetAura::IsAlwaysOnTop() const {
  return window_ && window_->GetProperty(aura::client::kAlwaysOnTopKey);
}

void NativeWidgetAura::SetVisibleOnAllWorkspaces(bool always_visible) {
  // Not implemented on chromeos or for child widgets.
}

bool NativeWidgetAura::IsVisibleOnAllWorkspaces() const {
  return false;
}

void NativeWidgetAura::Maximize() {
  if (window_)
    window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
}

void NativeWidgetAura::Minimize() {
  if (window_)
    window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
}

bool NativeWidgetAura::IsMaximized() const {
  return window_ && window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MAXIMIZED;
}

bool NativeWidgetAura::IsMinimized() const {
  return window_ && window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_MINIMIZED;
}

void NativeWidgetAura::Restore() {
  if (window_)
    window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
}

void NativeWidgetAura::SetFullscreen(bool fullscreen) {
  if (!window_ || IsFullscreen() == fullscreen)
    return;  // Nothing to do.

  wm::SetWindowFullscreen(window_, fullscreen);
}

bool NativeWidgetAura::IsFullscreen() const {
  return window_ && window_->GetProperty(aura::client::kShowStateKey) ==
      ui::SHOW_STATE_FULLSCREEN;
}

void NativeWidgetAura::SetOpacity(float opacity) {
  if (window_)
    window_->layer()->SetOpacity(opacity);
}

void NativeWidgetAura::FlashFrame(bool flash) {
  if (window_)
    window_->SetProperty(aura::client::kDrawAttentionKey, flash);
}

void NativeWidgetAura::RunShellDrag(View* view,
                                    const ui::OSExchangeData& data,
                                    const gfx::Point& location,
                                    int operation,
                                    ui::DragDropTypes::DragEventSource source) {
  if (window_)
    views::RunShellDrag(window_, data, location, operation, source);
}

void NativeWidgetAura::SchedulePaintInRect(const gfx::Rect& rect) {
  if (window_)
    window_->SchedulePaintInRect(rect);
}

void NativeWidgetAura::SetCursor(gfx::NativeCursor cursor) {
  cursor_ = cursor;
  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(window_->GetRootWindow());
  if (cursor_client)
    cursor_client->SetCursor(cursor);
}

bool NativeWidgetAura::IsMouseEventsEnabled() const {
  if (!window_)
    return false;
  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(window_->GetRootWindow());
  return cursor_client ? cursor_client->IsMouseEventsEnabled() : true;
}

void NativeWidgetAura::ClearNativeFocus() {
  aura::client::FocusClient* client = aura::client::GetFocusClient(window_);
  if (window_ && client && window_->Contains(client->GetFocusedWindow()))
    client->ResetFocusWithinActiveWindow(window_);
}

gfx::Rect NativeWidgetAura::GetWorkAreaBoundsInScreen() const {
  if (!window_)
    return gfx::Rect();
  return display::Screen::GetScreen()
      ->GetDisplayNearestWindow(window_)
      .work_area();
}

Widget::MoveLoopResult NativeWidgetAura::RunMoveLoop(
    const gfx::Vector2d& drag_offset,
    Widget::MoveLoopSource source,
    Widget::MoveLoopEscapeBehavior escape_behavior) {
  // |escape_behavior| is only needed on windows when running the native message
  // loop.
  if (!window_ || !window_->GetRootWindow())
    return Widget::MOVE_LOOP_CANCELED;
  wm::WindowMoveClient* move_client =
      wm::GetWindowMoveClient(window_->GetRootWindow());
  if (!move_client)
    return Widget::MOVE_LOOP_CANCELED;

  SetCapture();
  wm::WindowMoveSource window_move_source =
      source == Widget::MOVE_LOOP_SOURCE_MOUSE ? wm::WINDOW_MOVE_SOURCE_MOUSE
                                               : wm::WINDOW_MOVE_SOURCE_TOUCH;
  if (move_client->RunMoveLoop(window_, drag_offset, window_move_source) ==
      wm::MOVE_SUCCESSFUL) {
    return Widget::MOVE_LOOP_SUCCESSFUL;
  }
  return Widget::MOVE_LOOP_CANCELED;
}

void NativeWidgetAura::EndMoveLoop() {
  if (!window_ || !window_->GetRootWindow())
    return;
  wm::WindowMoveClient* move_client =
      wm::GetWindowMoveClient(window_->GetRootWindow());
  if (move_client)
    move_client->EndMoveLoop();
}

void NativeWidgetAura::SetVisibilityChangedAnimationsEnabled(bool value) {
  if (window_)
    window_->SetProperty(aura::client::kAnimationsDisabledKey, !value);
}

void NativeWidgetAura::SetVisibilityAnimationDuration(
    const base::TimeDelta& duration) {
  wm::SetWindowVisibilityAnimationDuration(window_, duration);
}

void NativeWidgetAura::SetVisibilityAnimationTransition(
    Widget::VisibilityTransition transition) {
  wm::WindowVisibilityAnimationTransition wm_transition = wm::ANIMATE_NONE;
  switch (transition) {
    case Widget::ANIMATE_SHOW:
      wm_transition = wm::ANIMATE_SHOW;
      break;
    case Widget::ANIMATE_HIDE:
      wm_transition = wm::ANIMATE_HIDE;
      break;
    case Widget::ANIMATE_BOTH:
      wm_transition = wm::ANIMATE_BOTH;
      break;
    case Widget::ANIMATE_NONE:
      wm_transition = wm::ANIMATE_NONE;
      break;
  }
  wm::SetWindowVisibilityAnimationTransition(window_, wm_transition);
}

bool NativeWidgetAura::IsTranslucentWindowOpacitySupported() const {
  return true;
}

void NativeWidgetAura::OnSizeConstraintsChanged() {
  if (is_parallel_widget_in_window_manager_)
    return;

  int32_t behavior = ui::mojom::kResizeBehaviorNone;
  if (GetWidget()->widget_delegate())
    behavior = GetWidget()->widget_delegate()->GetResizeBehavior();
  window_->SetProperty(aura::client::kResizeBehaviorKey, behavior);
}

void NativeWidgetAura::RepostNativeEvent(gfx::NativeEvent native_event) {
  OnEvent(native_event);
}

std::string NativeWidgetAura::GetName() const {
  return window_ ? window_->GetName() : std::string();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::WindowDelegate implementation:

gfx::Size NativeWidgetAura::GetMinimumSize() const {
  return delegate_->GetMinimumSize();
}

gfx::Size NativeWidgetAura::GetMaximumSize() const {
  // A window should not have a maximum size and also be maximizable.
  DCHECK(delegate_->GetMaximumSize().IsEmpty() ||
         !(window_->GetProperty(aura::client::kResizeBehaviorKey) &
           ui::mojom::kResizeBehaviorCanMaximize));
  return delegate_->GetMaximumSize();
}

void NativeWidgetAura::OnBoundsChanged(const gfx::Rect& old_bounds,
                                       const gfx::Rect& new_bounds) {
  // Assume that if the old bounds was completely empty a move happened. This
  // handles the case of a maximize animation acquiring the layer (acquiring a
  // layer results in clearing the bounds).
  if (old_bounds.origin() != new_bounds.origin() ||
      (old_bounds == gfx::Rect(0, 0, 0, 0) && !new_bounds.IsEmpty())) {
    delegate_->OnNativeWidgetMove();
  }
  if (old_bounds.size() != new_bounds.size())
    delegate_->OnNativeWidgetSizeChanged(new_bounds.size());
}

gfx::NativeCursor NativeWidgetAura::GetCursor(const gfx::Point& point) {
  return cursor_;
}

int NativeWidgetAura::GetNonClientComponent(const gfx::Point& point) const {
  return delegate_->GetNonClientComponent(point);
}

bool NativeWidgetAura::ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) {
  return delegate_->ShouldDescendIntoChildForEventHandling(
      window_->layer(), child, child->layer(), location);
}

bool NativeWidgetAura::CanFocus() {
  return ShouldActivate();
}

void NativeWidgetAura::OnCaptureLost() {
  delegate_->OnMouseCaptureLost();
}

void NativeWidgetAura::OnPaint(const ui::PaintContext& context) {
  delegate_->OnNativeWidgetPaint(context);
}

void NativeWidgetAura::OnDeviceScaleFactorChanged(float device_scale_factor) {
  GetWidget()->DeviceScaleFactorChanged(device_scale_factor);
}

void NativeWidgetAura::OnWindowDestroying(aura::Window* window) {
  window_->RemoveObserver(this);
  delegate_->OnNativeWidgetDestroying();

  // If the aura::Window is destroyed, we can no longer show tooltips.
  tooltip_manager_.reset();

  focus_manager_event_handler_.reset();
}

void NativeWidgetAura::OnWindowDestroyed(aura::Window* window) {
  window_ = NULL;
  delegate_->OnNativeWidgetDestroyed();
  if (ownership_ == Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
    delete this;
}

void NativeWidgetAura::OnWindowTargetVisibilityChanged(bool visible) {
  delegate_->OnNativeWidgetVisibilityChanged(visible);
}

bool NativeWidgetAura::HasHitTestMask() const {
  return delegate_->HasHitTestMask();
}

void NativeWidgetAura::GetHitTestMask(gfx::Path* mask) const {
  DCHECK(mask);
  delegate_->GetHitTestMask(mask);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::WindowObserver implementation:

void NativeWidgetAura::OnWindowPropertyChanged(aura::Window* window,
                                               const void* key,
                                               intptr_t old) {
  if (key == aura::client::kShowStateKey)
    delegate_->OnNativeWidgetWindowShowStateChanged();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, ui::EventHandler implementation:

void NativeWidgetAura::OnKeyEvent(ui::KeyEvent* event) {
  DCHECK(window_);
  // Renderer may send a key event back to us if the key event wasn't handled,
  // and the window may be invisible by that time.
  if (!window_->IsVisible())
    return;

  delegate_->OnKeyEvent(event);
}

void NativeWidgetAura::OnMouseEvent(ui::MouseEvent* event) {
  DCHECK(window_);
  DCHECK(window_->IsVisible());
  if (event->type() == ui::ET_MOUSEWHEEL) {
    delegate_->OnMouseEvent(event);
    return;
  }

  if (tooltip_manager_.get())
    tooltip_manager_->UpdateTooltip();
  TooltipManagerAura::UpdateTooltipManagerForCapture(GetWidget());
  delegate_->OnMouseEvent(event);
}

void NativeWidgetAura::OnScrollEvent(ui::ScrollEvent* event) {
  delegate_->OnScrollEvent(event);
}

void NativeWidgetAura::OnGestureEvent(ui::GestureEvent* event) {
  DCHECK(window_);
  DCHECK(window_->IsVisible() || event->IsEndingEvent());
  delegate_->OnGestureEvent(event);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, wm::ActivationDelegate implementation:

bool NativeWidgetAura::ShouldActivate() const {
  return delegate_->CanActivate();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, wm::ActivationChangeObserver implementation:

void NativeWidgetAura::OnWindowActivated(
    wm::ActivationChangeObserver::ActivationReason,
    aura::Window* gained_active,
    aura::Window* lost_active) {
  DCHECK(window_ == gained_active || window_ == lost_active);
  if (GetWidget()->GetFocusManager()) {
    if (window_ == gained_active)
      GetWidget()->GetFocusManager()->RestoreFocusedView();
    else if (window_ == lost_active)
      GetWidget()->GetFocusManager()->StoreFocusedView(true);
  }
  delegate_->OnNativeWidgetActivationChanged(window_ == gained_active);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::client::FocusChangeObserver:

void NativeWidgetAura::OnWindowFocused(aura::Window* gained_focus,
                                       aura::Window* lost_focus) {
  if (window_ == gained_focus)
    delegate_->OnNativeFocus();
  else if (window_ == lost_focus)
    delegate_->OnNativeBlur();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, aura::WindowDragDropDelegate implementation:

void NativeWidgetAura::OnDragEntered(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  last_drop_operation_ = drop_helper_->OnDragOver(event.data(),
      event.location(), event.source_operations());
}

int NativeWidgetAura::OnDragUpdated(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  last_drop_operation_ = drop_helper_->OnDragOver(event.data(),
      event.location(), event.source_operations());
  return last_drop_operation_;
}

void NativeWidgetAura::OnDragExited() {
  DCHECK(drop_helper_.get() != NULL);
  drop_helper_->OnDragExit();
}

int NativeWidgetAura::OnPerformDrop(const ui::DropTargetEvent& event) {
  DCHECK(drop_helper_.get() != NULL);
  return drop_helper_->OnDrop(event.data(), event.location(),
      last_drop_operation_);
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, protected:

NativeWidgetAura::~NativeWidgetAura() {
  destroying_ = true;
  if (ownership_ == Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET)
    delete delegate_;
  else
    CloseNow();
}

////////////////////////////////////////////////////////////////////////////////
// NativeWidgetAura, private:

void NativeWidgetAura::SetInitialFocus(ui::WindowShowState show_state) {
  // The window does not get keyboard messages unless we focus it.
  if (!GetWidget()->SetInitialFocus(show_state))
    window_->Focus();
}

////////////////////////////////////////////////////////////////////////////////
// Widget, public:

namespace {
#if defined(OS_WIN) || (defined(USE_X11) && !defined(OS_CHROMEOS))
void CloseWindow(aura::Window* window) {
  if (window) {
    Widget* widget = Widget::GetWidgetForNativeView(window);
    if (widget && widget->is_secondary_widget())
      // To avoid the delay in shutdown caused by using Close which may wait
      // for animations, use CloseNow. Because this is only used on secondary
      // widgets it seems relatively safe to skip the extra processing of
      // Close.
      widget->CloseNow();
  }
}
#endif

#if defined(OS_WIN)
BOOL CALLBACK WindowCallbackProc(HWND hwnd, LPARAM lParam) {
  aura::Window* root_window =
      DesktopWindowTreeHostWin::GetContentWindowForHWND(hwnd);
  CloseWindow(root_window);
  return TRUE;
}
#endif
}  // namespace

// static
void Widget::CloseAllSecondaryWidgets() {
#if defined(OS_WIN)
  EnumThreadWindows(GetCurrentThreadId(), WindowCallbackProc, 0);
#endif

#if defined(USE_X11) && !defined(OS_CHROMEOS)
  DesktopWindowTreeHostX11::CleanUpWindowList(CloseWindow);
#endif
}

bool Widget::ConvertRect(const Widget* source,
                         const Widget* target,
                         gfx::Rect* rect) {
  return false;
}

const ui::NativeTheme* Widget::GetNativeTheme() const {
#if defined(USE_X11) && !defined(OS_CHROMEOS)
  const LinuxUI* linux_ui = LinuxUI::instance();
  if (linux_ui) {
    ui::NativeTheme* native_theme =
        linux_ui->GetNativeTheme(native_widget_->GetNativeWindow());
    if (native_theme)
      return native_theme;
  }
#endif

  return ui::NativeTheme::GetInstanceForNativeUi();
}

namespace internal {

////////////////////////////////////////////////////////////////////////////////
// internal::NativeWidgetPrivate, public:

// static
NativeWidgetPrivate* NativeWidgetPrivate::CreateNativeWidget(
    internal::NativeWidgetDelegate* delegate) {
  return new NativeWidgetAura(delegate);
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetNativeWidgetForNativeView(
    gfx::NativeView native_view) {
  return native_view->GetProperty(kNativeWidgetPrivateKey);
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetNativeWidgetForNativeWindow(
    gfx::NativeWindow native_window) {
  return native_window->GetProperty(kNativeWidgetPrivateKey);
}

// static
NativeWidgetPrivate* NativeWidgetPrivate::GetTopLevelNativeWidget(
    gfx::NativeView native_view) {
  aura::Window* window = native_view;
  NativeWidgetPrivate* top_level_native_widget = NULL;
  while (window) {
    NativeWidgetPrivate* native_widget = GetNativeWidgetForNativeView(window);
    if (native_widget)
      top_level_native_widget = native_widget;
    window = window->parent();
  }
  return top_level_native_widget;
}

// static
void NativeWidgetPrivate::GetAllChildWidgets(gfx::NativeView native_view,
                                             Widget::Widgets* children) {
  {
    // Code expects widget for |native_view| to be added to |children|.
    NativeWidgetPrivate* native_widget = static_cast<NativeWidgetPrivate*>(
        GetNativeWidgetForNativeView(native_view));
    if (native_widget && native_widget->GetWidget())
      children->insert(native_widget->GetWidget());
  }

  const aura::Window::Windows& child_windows = native_view->children();
  for (aura::Window::Windows::const_iterator i = child_windows.begin();
       i != child_windows.end(); ++i) {
    GetAllChildWidgets((*i), children);
  }
}

// static
void NativeWidgetPrivate::GetAllOwnedWidgets(gfx::NativeView native_view,
                                             Widget::Widgets* owned) {
  // Add all owned widgets.
  for (aura::Window* transient_child : wm::GetTransientChildren(native_view)) {
    NativeWidgetPrivate* native_widget = static_cast<NativeWidgetPrivate*>(
        GetNativeWidgetForNativeView(transient_child));
    if (native_widget && native_widget->GetWidget())
      owned->insert(native_widget->GetWidget());
    GetAllOwnedWidgets(transient_child, owned);
  }

  // Add all child windows.
  for (aura::Window* child : native_view->children())
    GetAllChildWidgets(child, owned);
}

// static
void NativeWidgetPrivate::ReparentNativeView(gfx::NativeView native_view,
                                             gfx::NativeView new_parent) {
  DCHECK(native_view != new_parent);

  gfx::NativeView previous_parent = native_view->parent();
  if (previous_parent == new_parent)
    return;

  Widget::Widgets widgets;
  GetAllChildWidgets(native_view, &widgets);

  // First notify all the widgets that they are being disassociated
  // from their previous parent.
  for (Widget::Widgets::iterator it = widgets.begin();
      it != widgets.end(); ++it) {
    (*it)->NotifyNativeViewHierarchyWillChange();
  }

  if (new_parent) {
    new_parent->AddChild(native_view);
  } else {
    // The following looks weird, but it's the equivalent of what aura has
    // always done. (The previous behaviour of aura::Window::SetParent() used
    // NULL as a special value that meant ask the WindowParentingClient where
    // things should go.)
    //
    // This probably isn't strictly correct, but its an invariant that a Window
    // in use will be attached to a RootWindow, so we can't just call
    // RemoveChild here. The only possible thing that could assign a RootWindow
    // in this case is the stacking client of the current RootWindow. This
    // matches our previous behaviour; the global stacking client would almost
    // always reattach the window to the same RootWindow.
    aura::Window* root_window = native_view->GetRootWindow();
    aura::client::ParentWindowWithContext(
        native_view, root_window, root_window->GetBoundsInScreen());
  }

  // And now, notify them that they have a brand new parent.
  for (Widget::Widgets::iterator it = widgets.begin();
      it != widgets.end(); ++it) {
    (*it)->NotifyNativeViewHierarchyChanged();
  }
}

// static
bool NativeWidgetPrivate::IsMouseButtonDown() {
  return aura::Env::GetInstance()->IsMouseButtonDown();
}

// static
gfx::FontList NativeWidgetPrivate::GetWindowTitleFontList() {
#if defined(OS_WIN)
  NONCLIENTMETRICS_XP ncm;
  base::win::GetNonClientMetrics(&ncm);
  l10n_util::AdjustUIFont(&(ncm.lfCaptionFont));
  base::win::ScopedHFONT caption_font(CreateFontIndirect(&(ncm.lfCaptionFont)));
  return gfx::FontList(gfx::Font(caption_font.get()));
#else
  return gfx::FontList();
#endif
}

// static
gfx::NativeView NativeWidgetPrivate::GetGlobalCapture(
    gfx::NativeView native_view) {
  aura::client::CaptureClient* capture_client =
      aura::client::GetCaptureClient(native_view->GetRootWindow());
  if (!capture_client)
    return nullptr;
  return capture_client->GetGlobalCaptureWindow();
}

}  // namespace internal
}  // namespace views
