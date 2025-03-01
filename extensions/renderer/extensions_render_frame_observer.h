// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_EXTENSIONS_RENDER_FRAME_OBSERVER_H_
#define EXTENSIONS_RENDERER_EXTENSIONS_RENDER_FRAME_OBSERVER_H_

#include <stdint.h>

#include "base/macros.h"
#include "content/public/renderer/render_frame_observer.h"
#include "extensions/common/mojo/app_window.mojom.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/service_manager/public/cpp/binder_registry.h"

namespace extensions {

// This class holds the extensions specific parts of RenderFrame, and has the
// same lifetime.
class ExtensionsRenderFrameObserver : public content::RenderFrameObserver,
                                      public mojom::AppWindow {
 public:
  explicit ExtensionsRenderFrameObserver(
      content::RenderFrame* render_frame);
  ~ExtensionsRenderFrameObserver() override;

 private:
  void BindAppWindowRequest(mojom::AppWindowRequest request);

  // Toggles visual muting of the render view area. This is on when a
  // constrained window is showing.
  void SetVisuallyDeemphasized(bool deemphasized) override;

  // RenderFrameObserver implementation.
  void OnInterfaceRequestForFrame(
      const std::string& interface_name,
      mojo::ScopedMessagePipeHandle* interface_pipe) override;
  void DetailedConsoleMessageAdded(const base::string16& message,
                                   const base::string16& source,
                                   const base::string16& stack_trace,
                                   uint32_t line_number,
                                   int32_t severity_level) override;
  void OnDestruct() override;

  // true if webview is overlayed with grey color.
  bool webview_visually_deemphasized_;

  mojo::BindingSet<mojom::AppWindow> bindings_;

  service_manager::BinderRegistry registry_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionsRenderFrameObserver);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_EXTENSIONS_RENDER_FRAME_OBSERVER_H_

