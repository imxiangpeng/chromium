// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/dom_distiller/content/renderer/distiller_js_render_frame_observer.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "components/dom_distiller/content/common/distiller_page_notifier_service.mojom.h"
#include "components/dom_distiller/content/renderer/distiller_page_notifier_service_impl.h"
#include "content/public/renderer/render_frame.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "v8/include/v8.h"

namespace dom_distiller {

DistillerJsRenderFrameObserver::DistillerJsRenderFrameObserver(
    content::RenderFrame* render_frame,
    const int distiller_isolated_world_id)
    : RenderFrameObserver(render_frame),
      distiller_isolated_world_id_(distiller_isolated_world_id),
      is_distiller_page_(false),
      weak_factory_(this) {}

DistillerJsRenderFrameObserver::~DistillerJsRenderFrameObserver() {}

void DistillerJsRenderFrameObserver::OnInterfaceRequestForFrame(
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle* interface_pipe) {
  registry_.TryBindInterface(interface_name, interface_pipe);
}

void DistillerJsRenderFrameObserver::DidStartProvisionalLoad(
    blink::WebDocumentLoader* document_loader) {
  RegisterMojoInterface();
}

void DistillerJsRenderFrameObserver::DidFinishLoad() {
  // If no message about the distilled page was received at this point, there
  // will not be one; remove the mojom::DistillerPageNotifierService from the
  // registry.
  registry_.RemoveInterface<mojom::DistillerPageNotifierService>();
}

void DistillerJsRenderFrameObserver::DidCreateScriptContext(
    v8::Local<v8::Context> context,
    int world_id) {
  if (world_id != distiller_isolated_world_id_ || !is_distiller_page_) {
    return;
  }

  native_javascript_handle_.reset(
      new DistillerNativeJavaScript(render_frame()));
  native_javascript_handle_->AddJavaScriptObjectToFrame(context);
}

void DistillerJsRenderFrameObserver::RegisterMojoInterface() {
  registry_.AddInterface(base::Bind(
      &DistillerJsRenderFrameObserver::CreateDistillerPageNotifierService,
      weak_factory_.GetWeakPtr()));
}

void DistillerJsRenderFrameObserver::CreateDistillerPageNotifierService(
    mojom::DistillerPageNotifierServiceRequest request) {
  mojo::MakeStrongBinding(
      base::MakeUnique<DistillerPageNotifierServiceImpl>(this),
      std::move(request));
}

void DistillerJsRenderFrameObserver::SetIsDistillerPage() {
  is_distiller_page_ = true;
}

void DistillerJsRenderFrameObserver::OnDestruct() {
  delete this;
}

}  // namespace dom_distiller
