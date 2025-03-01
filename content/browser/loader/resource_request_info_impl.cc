// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/loader/resource_request_info_impl.h"

#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/loader/global_routing_id.h"
#include "content/browser/loader/resource_message_filter.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/net/url_request_service_worker_data.h"
#include "content/common/net/url_request_user_data.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/process_type.h"
#include "net/url_request/url_request.h"

namespace content {

namespace {

int FrameTreeNodeIdFromHostIds(int render_process_host_id,
                               int render_frame_host_id) {
  RenderFrameHost* render_frame_host =
      RenderFrameHost::FromID(render_process_host_id, render_frame_host_id);
  return render_frame_host ? render_frame_host->GetFrameTreeNodeId() : -1;
}

}  // namespace

// ----------------------------------------------------------------------------
// ResourceRequestInfo

// static
const ResourceRequestInfo* ResourceRequestInfo::ForRequest(
    const net::URLRequest* request) {
  return ResourceRequestInfoImpl::ForRequest(request);
}

// static
void ResourceRequestInfo::AllocateForTesting(net::URLRequest* request,
                                             ResourceType resource_type,
                                             ResourceContext* context,
                                             int render_process_id,
                                             int render_view_id,
                                             int render_frame_id,
                                             bool is_main_frame,
                                             bool parent_is_main_frame,
                                             bool allow_download,
                                             bool is_async,
                                             PreviewsState previews_state) {
  // Make sure both |is_main_frame| and |parent_is_main_frame| aren't set at the
  // same time.
  DCHECK(!(is_main_frame && parent_is_main_frame));

  // Make sure RESOURCE_TYPE_MAIN_FRAME is declared as being fetched as part of
  // the main frame.
  DCHECK(resource_type != RESOURCE_TYPE_MAIN_FRAME || is_main_frame);

  ResourceRequestInfoImpl* info = new ResourceRequestInfoImpl(
      ResourceRequesterInfo::CreateForRendererTesting(
          render_process_id),                 // resource_requester_info
      render_view_id,                         // route_id
      -1,                                     // frame_tree_node_id
      0,                                      // origin_pid
      0,                                      // request_id
      render_frame_id,                        // render_frame_id
      is_main_frame,                          // is_main_frame
      parent_is_main_frame,                   // parent_is_main_frame
      resource_type,                          // resource_type
      ui::PAGE_TRANSITION_LINK,               // transition_type
      false,                                  // should_replace_current_entry
      false,                                  // is_download
      false,                                  // is_stream
      allow_download,                         // allow_download
      false,                                  // has_user_gesture
      false,                                  // enable load timing
      request->has_upload(),                  // enable upload progress
      false,                                  // do_not_prompt_for_login
      blink::kWebReferrerPolicyDefault,       // referrer_policy
      blink::kWebPageVisibilityStateVisible,  // visibility_state
      context,                                // context
      false,                                  // report_raw_headers
      is_async,                               // is_async
      previews_state,                         // previews_state
      nullptr,                                // body
      false);                                 // initiated_in_secure_context
  info->AssociateWithRequest(request);
}

// static
bool ResourceRequestInfo::GetRenderFrameForRequest(
    const net::URLRequest* request,
    int* render_process_id,
    int* render_frame_id) {
  URLRequestUserData* user_data = static_cast<URLRequestUserData*>(
      request->GetUserData(URLRequestUserData::kUserDataKey));
  if (!user_data)
    return false;
  *render_process_id = user_data->render_process_id();
  *render_frame_id = user_data->render_frame_id();
  return true;
}

// static
bool ResourceRequestInfo::OriginatedFromServiceWorker(
    const net::URLRequest* request) {
  return !!request->GetUserData(
      content::URLRequestServiceWorkerData::kUserDataKey);
}

// ----------------------------------------------------------------------------
// ResourceRequestInfoImpl

// static
ResourceRequestInfoImpl* ResourceRequestInfoImpl::ForRequest(
    net::URLRequest* request) {
  return static_cast<ResourceRequestInfoImpl*>(request->GetUserData(NULL));
}

// static
const ResourceRequestInfoImpl* ResourceRequestInfoImpl::ForRequest(
    const net::URLRequest* request) {
  return ForRequest(const_cast<net::URLRequest*>(request));
}

ResourceRequestInfoImpl::ResourceRequestInfoImpl(
    scoped_refptr<ResourceRequesterInfo> requester_info,
    int route_id,
    int frame_tree_node_id,
    int origin_pid,
    int request_id,
    int render_frame_id,
    bool is_main_frame,
    bool parent_is_main_frame,
    ResourceType resource_type,
    ui::PageTransition transition_type,
    bool should_replace_current_entry,
    bool is_download,
    bool is_stream,
    bool allow_download,
    bool has_user_gesture,
    bool enable_load_timing,
    bool enable_upload_progress,
    bool do_not_prompt_for_login,
    blink::WebReferrerPolicy referrer_policy,
    blink::WebPageVisibilityState visibility_state,
    ResourceContext* context,
    bool report_raw_headers,
    bool is_async,
    PreviewsState previews_state,
    const scoped_refptr<ResourceRequestBody> body,
    bool initiated_in_secure_context)
    : detachable_handler_(NULL),
      requester_info_(std::move(requester_info)),
      route_id_(route_id),
      frame_tree_node_id_(frame_tree_node_id),
      origin_pid_(origin_pid),
      request_id_(request_id),
      render_frame_id_(render_frame_id),
      is_main_frame_(is_main_frame),
      parent_is_main_frame_(parent_is_main_frame),
      should_replace_current_entry_(should_replace_current_entry),
      is_download_(is_download),
      is_stream_(is_stream),
      allow_download_(allow_download),
      has_user_gesture_(has_user_gesture),
      enable_load_timing_(enable_load_timing),
      enable_upload_progress_(enable_upload_progress),
      do_not_prompt_for_login_(do_not_prompt_for_login),
      was_ignored_by_handler_(false),
      counted_as_in_flight_request_(false),
      resource_type_(resource_type),
      transition_type_(transition_type),
      memory_cost_(0),
      referrer_policy_(referrer_policy),
      visibility_state_(visibility_state),
      context_(context),
      report_raw_headers_(report_raw_headers),
      is_async_(is_async),
      previews_state_(previews_state),
      body_(body),
      initiated_in_secure_context_(initiated_in_secure_context) {}

ResourceRequestInfoImpl::~ResourceRequestInfoImpl() {
}

ResourceRequestInfo::WebContentsGetter
ResourceRequestInfoImpl::GetWebContentsGetterForRequest() const {
  // PlzNavigate: navigation requests are created with a valid FrameTreeNode ID
  // and invalid RenderProcessHost and RenderFrameHost IDs. The FrameTreeNode
  // ID should be used to access the WebContents.
  if (frame_tree_node_id_ != -1) {
    DCHECK(IsBrowserSideNavigationEnabled());
    return base::Bind(WebContents::FromFrameTreeNodeId, frame_tree_node_id_);
  }

  // In other cases, use the RenderProcessHost ID + RenderFrameHost ID to get
  // the WebContents.
  int render_process_host_id = -1;
  int render_frame_host_id = -1;
  if (!GetAssociatedRenderFrame(&render_process_host_id,
                                &render_frame_host_id)) {
    NOTREACHED();
  }

  return base::Bind(&WebContentsImpl::FromRenderFrameHostID,
                    render_process_host_id, render_frame_host_id);
}

ResourceRequestInfo::FrameTreeNodeIdGetter
ResourceRequestInfoImpl::GetFrameTreeNodeIdGetterForRequest() const {
  if (frame_tree_node_id_ != -1) {
    DCHECK(IsBrowserSideNavigationEnabled());
    return base::Bind([](int id) { return id; }, frame_tree_node_id_);
  }

  int render_process_host_id = -1;
  int render_frame_host_id = -1;
  if (!GetAssociatedRenderFrame(&render_process_host_id,
                                &render_frame_host_id)) {
    NOTREACHED();
  }

  return base::Bind(&FrameTreeNodeIdFromHostIds, render_process_host_id,
                    render_frame_host_id);
}

ResourceContext* ResourceRequestInfoImpl::GetContext() const {
  return context_;
}

int ResourceRequestInfoImpl::GetChildID() const {
  return requester_info_->child_id();
}

int ResourceRequestInfoImpl::GetRouteID() const {
  return route_id_;
}

GlobalRequestID ResourceRequestInfoImpl::GetGlobalRequestID() const {
  return GlobalRequestID(GetChildID(), request_id_);
}

int ResourceRequestInfoImpl::GetOriginPID() const {
  return origin_pid_;
}

int ResourceRequestInfoImpl::GetRenderFrameID() const {
  return render_frame_id_;
}

int ResourceRequestInfoImpl::GetFrameTreeNodeId() const {
  return frame_tree_node_id_;
}

bool ResourceRequestInfoImpl::IsMainFrame() const {
  return is_main_frame_;
}

bool ResourceRequestInfoImpl::ParentIsMainFrame() const {
  return parent_is_main_frame_;
}

ResourceType ResourceRequestInfoImpl::GetResourceType() const {
  return resource_type_;
}

int ResourceRequestInfoImpl::GetProcessType() const {
  return requester_info_->IsBrowserSideNavigation() ? PROCESS_TYPE_BROWSER
                                                    : PROCESS_TYPE_RENDERER;
}

blink::WebReferrerPolicy ResourceRequestInfoImpl::GetReferrerPolicy() const {
  return referrer_policy_;
}

blink::WebPageVisibilityState
ResourceRequestInfoImpl::GetVisibilityState() const {
  return visibility_state_;
}

ui::PageTransition ResourceRequestInfoImpl::GetPageTransition() const {
  return transition_type_;
}

bool ResourceRequestInfoImpl::HasUserGesture() const {
  return has_user_gesture_;
}

bool ResourceRequestInfoImpl::WasIgnoredByHandler() const {
  return was_ignored_by_handler_;
}

bool ResourceRequestInfoImpl::GetAssociatedRenderFrame(
    int* render_process_id,
    int* render_frame_id) const {
  *render_process_id = GetChildID();
  *render_frame_id = render_frame_id_;
  return true;
}

bool ResourceRequestInfoImpl::IsAsync() const {
  return is_async_;
}

bool ResourceRequestInfoImpl::IsDownload() const {
  return is_download_;
}

PreviewsState ResourceRequestInfoImpl::GetPreviewsState() const {
  return previews_state_;
}

bool ResourceRequestInfoImpl::ShouldReportRawHeaders() const {
  return report_raw_headers_;
}

NavigationUIData* ResourceRequestInfoImpl::GetNavigationUIData() const {
  return navigation_ui_data_.get();
}

void ResourceRequestInfoImpl::AssociateWithRequest(net::URLRequest* request) {
  request->SetUserData(nullptr, base::WrapUnique(this));
  int render_process_id;
  int render_frame_id;
  if (GetAssociatedRenderFrame(&render_process_id, &render_frame_id)) {
    request->SetUserData(URLRequestUserData::kUserDataKey,
                         base::MakeUnique<URLRequestUserData>(render_process_id,
                                                              render_frame_id));
  }
}

int ResourceRequestInfoImpl::GetRequestID() const {
  return request_id_;
}

GlobalRoutingID ResourceRequestInfoImpl::GetGlobalRoutingID() const {
  return GlobalRoutingID(GetChildID(), route_id_);
}

void ResourceRequestInfoImpl::UpdateForTransfer(
    int route_id,
    int render_frame_id,
    int origin_pid,
    int request_id,
    ResourceRequesterInfo* requester_info,
    mojom::URLLoaderRequest url_loader_request,
    mojom::URLLoaderClientPtr url_loader_client) {
  route_id_ = route_id;
  render_frame_id_ = render_frame_id;
  origin_pid_ = origin_pid;
  request_id_ = request_id;
  requester_info_ = requester_info;

  // on_transfer_ is non-null only when MojoAsyncResourceHandler is used.
  if (on_transfer_) {
    on_transfer_.Run(std::move(url_loader_request),
                     std::move(url_loader_client));
  }
}

void ResourceRequestInfoImpl::ResetBody() {
  body_ = nullptr;
}

void ResourceRequestInfoImpl::SetBlobHandles(BlobHandles blob_handles) {
  blob_handles_ = std::move(blob_handles);
}

}  // namespace content
