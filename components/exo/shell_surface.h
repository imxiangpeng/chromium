// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_EXO_SHELL_SURFACE_H_
#define COMPONENTS_EXO_SHELL_SURFACE_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "ash/wm/window_state_observer.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "components/exo/surface_observer.h"
#include "components/exo/surface_tree_host.h"
#include "components/exo/wm_helper.h"
#include "ui/base/hit_test.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {
class WindowResizer;
namespace mojom {
enum class WindowPinType;
}
}

namespace base {
namespace trace_event {
class TracedValue;
}
}

namespace exo {
class Surface;

// This class provides functions for treating a surfaces like toplevel,
// fullscreen or popup widgets, move, resize or maximize them, associate
// metadata like title and class, etc.
class ShellSurface : public SurfaceTreeHost,
                     public SurfaceObserver,
                     public views::WidgetDelegate,
                     public views::View,
                     public ash::wm::WindowStateObserver,
                     public WMHelper::ActivationObserver,
                     public WMHelper::DisplayConfigurationObserver {
 public:
  enum class BoundsMode { SHELL, CLIENT, FIXED };

  // The |origin| is in screen coordinates. When bounds are controlled by the
  // shell or fixed, it determines the initial position of the shell surface.
  // In that case, the position specified as part of the geometry is relative
  // to the shell surface.
  //
  // When bounds are controlled by the client, it represents the origin of a
  // coordinate system to which the position of the shell surface, specified
  // as part of the geometry, is relative. The client must acknowledge changes
  // to the origin, and offset the geometry accordingly.
  ShellSurface(Surface* surface,
               ShellSurface* parent,
               BoundsMode bounds_mode,
               const gfx::Point& origin,
               bool activatable,
               bool can_minimize,
               int container);
  explicit ShellSurface(Surface* surface);
  ~ShellSurface() override;

  // Set the callback to run when the user wants the shell surface to be closed.
  // The receiver can chose to not close the window on this signal.
  void set_close_callback(const base::Closure& close_callback) {
    close_callback_ = close_callback;
  }

  // Set the callback to run when the surface is destroyed.
  void set_surface_destroyed_callback(
      const base::Closure& surface_destroyed_callback) {
    surface_destroyed_callback_ = surface_destroyed_callback;
  }

  // Set the callback to run when the surface state changed.
  using StateChangedCallback =
      base::Callback<void(ash::wm::WindowStateType old_state_type,
                          ash::wm::WindowStateType new_state_type)>;
  void set_state_changed_callback(
      const StateChangedCallback& state_changed_callback) {
    state_changed_callback_ = state_changed_callback;
  }

  // Set the callback to run when the client is asked to configure the surface.
  // The size is a hint, in the sense that the client is free to ignore it if
  // it doesn't resize, pick a smaller size (to satisfy aspect ratio or resize
  // in steps of NxM pixels).
  using ConfigureCallback =
      base::Callback<uint32_t(const gfx::Size& size,
                              ash::wm::WindowStateType state_type,
                              bool resizing,
                              bool activated,
                              const gfx::Vector2d& origin_offset)>;
  void set_configure_callback(const ConfigureCallback& configure_callback) {
    configure_callback_ = configure_callback;
  }

  // When the client is asked to configure the surface, it should acknowledge
  // the configure request sometime before the commit. |serial| is the serial
  // from the configure callback.
  void AcknowledgeConfigure(uint32_t serial);

  // Set the "parent" of this surface. This window should be stacked above a
  // parent.
  void SetParent(ShellSurface* parent);

  // Activates the shell surface.
  void Activate();

  // Maximizes the shell surface.
  void Maximize();

  // Minimize the shell surface.
  void Minimize();

  // Restore the shell surface.
  void Restore();

  // Set fullscreen state for shell surface.
  void SetFullscreen(bool fullscreen);

  // Pins the shell surface.
  void SetPinned(ash::mojom::WindowPinType type);

  // Sets whether or not the shell surface should autohide the system UI.
  void SetSystemUiVisibility(bool autohide);

  // Set whether the surface is always on top.
  void SetAlwaysOnTop(bool always_on_top);

  // Set title for the surface.
  void SetTitle(const base::string16& title);

  // Set icon for the surface.
  void SetIcon(const gfx::ImageSkia& icon);

  // Sets the system modality.
  void SetSystemModal(bool system_modal);

  // Sets the application ID for the window. The application ID identifies the
  // general class of applications to which the window belongs.
  static void SetApplicationId(aura::Window* window, const std::string& id);
  static const std::string* GetApplicationId(aura::Window* window);

  // Set the application ID for the surface.
  void SetApplicationId(const std::string& application_id);

  // Start an interactive move of surface.
  void Move();

  // Start an interactive resize of surface. |component| is one of the windows
  // HT constants (see ui/base/hit_test.h) and describes in what direction the
  // surface should be resized.
  void Resize(int component);

  // Signal a request to close the window. It is up to the implementation to
  // actually decide to do so though.
  void Close();

  // Set geometry for surface. The geometry represents the "visible bounds"
  // for the surface from the user's perspective.
  void SetGeometry(const gfx::Rect& geometry);

  // Enable/disable rectangular shadow that uses the widget bounds as a content
  // bounds.
  void SetRectangularShadowEnabled(bool enabled);

  // [Deprecated] Set the content bounds for the shadow. Shell surface geometry
  // will be
  // used if bounds are empty.
  void SetRectangularShadow_DEPRECATED(const gfx::Rect& content_bounds);

  // Set the content bounds for the shadow in the surface's coordinates.
  // Setting empty bounds will disable the shadow.
  void SetRectangularSurfaceShadow(const gfx::Rect& content_bounds);

  // Set the pacity of the background for the window that has a shadow.
  void SetRectangularShadowBackgroundOpacity(float opacity);

  // Enable/disable window frame.
  void SetFrame(bool enabled);

  // Set scale factor for surface. The scale factor will be applied to surface
  // and all descendants.
  void SetScale(double scale);

  // Set top inset for surface.
  void SetTopInset(int height);

  // Set origin in screen coordinate space.
  void SetOrigin(const gfx::Point& origin);

  // Set activatable state for surface.
  void SetActivatable(bool activatable);

  // Set container for surface.
  void SetContainer(int container);

  // Sets the main surface for the window.
  static void SetMainSurface(aura::Window* window, Surface* surface);

  // Returns the main Surface instance or nullptr if it is not set.
  // |window| must not be nullptr.
  static Surface* GetMainSurface(const aura::Window* window);

  // Returns a trace value representing the state of the surface.
  std::unique_ptr<base::trace_event::TracedValue> AsTracedValue() const;

  // Overridden from SurfaceDelegate:
  void OnSurfaceCommit() override;

  // Overridden from SurfaceObserver:
  void OnSurfaceDestroying(Surface* surface) override;

  // Overridden from views::WidgetDelegate:
  bool CanResize() const override;
  bool CanMaximize() const override;
  bool CanMinimize() const override;
  base::string16 GetWindowTitle() const override;
  gfx::ImageSkia GetWindowIcon() override;
  void SaveWindowPlacement(const gfx::Rect& bounds,
                           ui::WindowShowState show_state) override;
  bool GetSavedWindowPlacement(const views::Widget* widget,
                               gfx::Rect* bounds,
                               ui::WindowShowState* show_state) const override;
  void WindowClosing() override;
  views::Widget* GetWidget() override;
  const views::Widget* GetWidget() const override;
  views::View* GetContentsView() override;
  views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) override;
  bool WidgetHasHitTestMask() const override;
  void GetWidgetHitTestMask(gfx::Path* mask) const override;

  // Overridden from views::View:
  gfx::Size CalculatePreferredSize() const override;
  gfx::Size GetMinimumSize() const override;

  // Overridden from ash::wm::WindowStateObserver:
  void OnPreWindowStateTypeChange(ash::wm::WindowState* window_state,
                                  ash::wm::WindowStateType old_type) override;
  void OnPostWindowStateTypeChange(ash::wm::WindowState* window_state,
                                   ash::wm::WindowStateType old_type) override;

  // Overridden from aura::WindowObserver:
  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override;
  void OnWindowAddedToRootWindow(aura::Window* window) override;
  void OnWindowRemovingFromRootWindow(aura::Window* window,
                                      aura::Window* new_root) override;
  void OnWindowDestroying(aura::Window* window) override;

  // Overridden from WMHelper::ActivationObserver:
  void OnWindowActivated(
      aura::Window* gained_active,
      aura::Window* lost_active) override;

  // Overridden from WMHelper::DisplayConfigurationObserver:
  void OnDisplayConfigurationChanged() override;

  // Overridden from ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;

  // Overridden from ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  aura::Window* shadow_overlay() { return shadow_overlay_.get(); }
  aura::Window* shadow_underlay() { return shadow_underlay_.get(); }

  Surface* surface_for_testing() { return root_surface(); }

 private:
  class ScopedConfigure;
  class ScopedAnimationsDisabled;

  // Surface state associated with each configure request.
  struct Config {
    uint32_t serial;
    gfx::Vector2d origin_offset;
    int resize_component;
  };

  // Creates the |widget_| for |surface_|. |show_state| is the initial state
  // of the widget (e.g. maximized).
  void CreateShellSurfaceWidget(ui::WindowShowState show_state);

  // Asks the client to configure its surface.
  void Configure();

  // Returns the window that has capture during dragging.
  aura::Window* GetDragWindow();

  // Attempt to start a drag operation. The type of drag operation to start is
  // determined by |component|.
  void AttemptToStartDrag(int component);

  // End current drag operation.
  void EndDrag(bool revert);

  // Returns true if surface is currently being resized.
  bool IsResizing() const;

  // Returns the "visible bounds" for the surface from the user's perspective.
  gfx::Rect GetVisibleBounds() const;

  // Returns the origin for the surface taking visible bounds and current
  // resize direction into account.
  gfx::Point GetSurfaceOrigin() const;

  // Updates the bounds of widget to match the current surface bounds.
  void UpdateWidgetBounds();

  // Updates the bounds of surface to match the current widget bounds.
  void UpdateSurfaceBounds();

  // Creates, deletes and update the shadow bounds based on
  // |pending_shadow_content_bounds_|.
  void UpdateShadow();

  // Applies |system_modal_| to |widget_|.
  void UpdateSystemModal();

  // In the coordinate system of the parent root window.
  gfx::Point GetMouseLocation() const;

  views::Widget* widget_ = nullptr;
  aura::Window* parent_;
  const BoundsMode bounds_mode_;
  int64_t primary_display_id_;
  gfx::Point origin_;
  bool activatable_ = true;
  const bool can_minimize_;
  // Container Window Id (see ash/public/cpp/shell_window_ids.h)
  int container_;
  bool frame_enabled_ = false;
  bool shadow_enabled_ = false;
  bool pending_show_widget_ = false;
  base::string16 title_;
  std::string application_id_;
  gfx::Rect geometry_;
  gfx::Rect pending_geometry_;
  double scale_ = 1.0;
  double pending_scale_ = 1.0;
  base::Closure close_callback_;
  base::Closure surface_destroyed_callback_;
  StateChangedCallback state_changed_callback_;
  ConfigureCallback configure_callback_;
  ScopedConfigure* scoped_configure_ = nullptr;
  bool ignore_window_bounds_changes_ = false;
  gfx::Vector2d origin_offset_;
  gfx::Vector2d pending_origin_offset_;
  gfx::Vector2d pending_origin_offset_accumulator_;
  int resize_component_ = HTCAPTION;  // HT constant (see ui/base/hit_test.h)
  int pending_resize_component_ = HTCAPTION;
  std::unique_ptr<aura::Window> shadow_overlay_;
  std::unique_ptr<aura::Window> shadow_underlay_;
  gfx::Rect shadow_content_bounds_;
  float shadow_background_opacity_ = 1.0;
  std::deque<Config> pending_configs_;
  std::unique_ptr<ash::WindowResizer> resizer_;
  std::unique_ptr<ScopedAnimationsDisabled> scoped_animations_disabled_;
  int top_inset_height_ = 0;
  int pending_top_inset_height_ = 0;
  bool shadow_underlay_in_surface_ = true;
  bool pending_shadow_underlay_in_surface_ = true;
  bool system_modal_ = false;
  gfx::ImageSkia icon_;

  DISALLOW_COPY_AND_ASSIGN(ShellSurface);
};

}  // namespace exo

#endif  // COMPONENTS_EXO_SHELL_SURFACE_H_
