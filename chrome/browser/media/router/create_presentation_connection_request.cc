// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/create_presentation_connection_request.h"

#include "chrome/common/media_router/media_route.h"
#include "chrome/common/media_router/media_source_helper.h"
#include "chrome/common/media_router/route_request_result.h"
#include "url/origin.h"

using content::PresentationInfo;
using content::PresentationError;

namespace media_router {

CreatePresentationConnectionRequest::CreatePresentationConnectionRequest(
    const content::PresentationRequest& presentation_request,
    PresentationConnectionCallback success_cb,
    PresentationConnectionErrorCallback error_cb)
    : presentation_request_(presentation_request),
      success_cb_(std::move(success_cb)),
      error_cb_(std::move(error_cb)),
      cb_invoked_(false) {
  DCHECK(!success_cb_.is_null());
  DCHECK(!error_cb_.is_null());
}

CreatePresentationConnectionRequest::~CreatePresentationConnectionRequest() {
  if (!cb_invoked_) {
    std::move(error_cb_).Run(content::PresentationError(
        content::PRESENTATION_ERROR_UNKNOWN, "Unknown error."));
  }
}

void CreatePresentationConnectionRequest::InvokeSuccessCallback(
    const std::string& presentation_id,
    const GURL& presentation_url,
    const MediaRoute& route) {
  DCHECK(!cb_invoked_);
  if (!cb_invoked_) {
    std::move(success_cb_)
        .Run(content::PresentationInfo(presentation_url, presentation_id),
             route);
    cb_invoked_ = true;
  }
}

void CreatePresentationConnectionRequest::InvokeErrorCallback(
    const content::PresentationError& error) {
  DCHECK(!cb_invoked_);
  if (!cb_invoked_) {
    std::move(error_cb_).Run(error);
    cb_invoked_ = true;
  }
}

// static
void CreatePresentationConnectionRequest::HandleRouteResponse(
    std::unique_ptr<CreatePresentationConnectionRequest> presentation_request,
    const RouteRequestResult& result) {
  if (!result.route()) {
    presentation_request->InvokeErrorCallback(content::PresentationError(
        content::PRESENTATION_ERROR_UNKNOWN, result.error()));
  } else {
    presentation_request->InvokeSuccessCallback(
        result.presentation_id(), result.presentation_url(), *result.route());
  }
}

}  // namespace media_router
