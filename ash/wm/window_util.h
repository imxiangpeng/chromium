// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WINDOW_UTIL_H_
#define ASH_WM_WINDOW_UTIL_H_

#include <stdint.h>

#include "ash/ash_export.h"
#include "base/compiler_specific.h"
#include "ui/base/ui_base_types.h"

namespace aura {
class Window;
}

namespace gfx {
class Point;
}

namespace ui {
class Event;
class EventHandler;
}

namespace ash {

class ImmersiveFullscreenController;

namespace wm {

// Utility functions for window activation.
ASH_EXPORT void ActivateWindow(aura::Window* window);
ASH_EXPORT void DeactivateWindow(aura::Window* window);
ASH_EXPORT bool IsActiveWindow(aura::Window* window);
ASH_EXPORT aura::Window* GetActiveWindow();
ASH_EXPORT bool CanActivateWindow(aura::Window* window);
ASH_EXPORT aura::Window* GetFocusedWindow();

// Retrieves the activatable window for |window|. If |window| is activatable,
// this will just return it, otherwise it will climb the parent/transient parent
// chain looking for a window that is activatable, per the ActivationController.
// If you're looking for a function to get the activatable "top level" window,
// this is probably what you're looking for.
ASH_EXPORT aura::Window* GetActivatableWindow(aura::Window* window);

// Returns the window with capture, null if no window currently has capture.
ASH_EXPORT aura::Window* GetCaptureWindow();

// Returns true if |window|'s location can be controlled by the user.
ASH_EXPORT bool IsWindowUserPositionable(aura::Window* window);

// Pins the window on top of other windows.
ASH_EXPORT void PinWindow(aura::Window* window, bool trusted);

// Indicates that the window should autohide the shelf when it is the active
// window.
ASH_EXPORT void SetAutoHideShelf(aura::Window* window, bool autohide);

// Moves |window| to the root window for the given |display_id|, if it is not
// already in the same root window. Returns true if |window| was moved.
ASH_EXPORT bool MoveWindowToDisplay(aura::Window* window, int64_t display_id);

// Moves |window| to the root window where the |event| occurred, if it is not
// already in the same root window. Returns true if |window| was moved.
ASH_EXPORT bool MoveWindowToEventRoot(aura::Window* window,
                                      const ui::Event& event);

// Snap the window's layer to physical pixel boundary.
ASH_EXPORT void SnapWindowToPixelBoundary(aura::Window* window);

// Mark the container window so that InstallSnapLayoutManagerToContainers
// installs the SnapToPixelLayoutManager.
ASH_EXPORT void SetSnapsChildrenToPhysicalPixelBoundary(
    aura::Window* container);

// Convenience for window->delegate()->GetNonClientComponent(location) that
// returns HTNOWHERE if window->delegate() is null.
ASH_EXPORT int GetNonClientComponent(aura::Window* window,
                                     const gfx::Point& location);

// When set, the child windows should get a slightly larger hit region to make
// resizing easier.
ASH_EXPORT void SetChildrenUseExtendedHitRegionForWindow(aura::Window* window);

// Requests the |window| to close and destroy itself. This is intended to
// forward to an associated widget.
ASH_EXPORT void CloseWidgetForWindow(aura::Window* window);

// Adds or removes a handler to receive events targeted at this window, before
// this window handles the events itself; the handler does not receive events
// from embedded windows. This only supports windows with internal widgets;
// see ash::GetInternalWidgetForWindow(). Ownership of the handler is not
// transferred.
//
// Also note that the target of these events is always an aura::Window.
ASH_EXPORT void AddLimitedPreTargetHandlerForWindow(ui::EventHandler* handler,
                                                    aura::Window* window);
ASH_EXPORT void RemoveLimitedPreTargetHandlerForWindow(
    ui::EventHandler* handler,
    aura::Window* window);

// Installs a resize handler on the window that makes it easier to resize
// the window. See ResizeHandleWindowTargeter for the specifics.
ASH_EXPORT void InstallResizeHandleWindowTargeterForWindow(
    aura::Window* window,
    ImmersiveFullscreenController* immersive_fullscreen_controller);

}  // namespace wm
}  // namespace ash

#endif  // ASH_WM_WINDOW_UTIL_H_
