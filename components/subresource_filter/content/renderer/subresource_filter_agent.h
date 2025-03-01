// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SUBRESOURCE_FILTER_CONTENT_RENDERER_SUBRESOURCE_FILTER_AGENT_H_
#define COMPONENTS_SUBRESOURCE_FILTER_CONTENT_RENDERER_SUBRESOURCE_FILTER_AGENT_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "url/gurl.h"

namespace blink {
class WebDocumentSubresourceFilter;
}  // namespace blink

namespace subresource_filter {

struct DocumentLoadStatistics;
class UnverifiedRulesetDealer;
class WebDocumentSubresourceFilterImpl;

// The renderer-side agent of ContentSubresourceFilterDriverFactory. There is
// one instance per RenderFrame, responsible for setting up the subresource
// filter for the ongoing provisional document load in the frame when instructed
// to do so by the driver.
class SubresourceFilterAgent
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<SubresourceFilterAgent>,
      public base::SupportsWeakPtr<SubresourceFilterAgent> {
 public:
  // The |ruleset_dealer| must not be null and must outlive this instance. The
  // |render_frame| may be null in unittests.
  explicit SubresourceFilterAgent(content::RenderFrame* render_frame,
                                  UnverifiedRulesetDealer* ruleset_dealer);
  ~SubresourceFilterAgent() override;

 protected:
  // Below methods are protected virtual so they can be mocked out in tests.

  // Returns the URL of the currently committed document.
  virtual GURL GetDocumentURL();

  // Injects the provided subresource |filter| into the DocumentLoader
  // orchestrating the most recently committed load.
  virtual void SetSubresourceFilterForCommittedLoad(
      std::unique_ptr<blink::WebDocumentSubresourceFilter> filter);

  // Informs the browser that the first subresource load has been disallowed for
  // the most recently committed load. Not called if all resources are allowed.
  virtual void SignalFirstSubresourceDisallowedForCommittedLoad();

  // Sends statistics about the DocumentSubresourceFilter's work to the browser.
  virtual void SendDocumentLoadStatistics(
      const DocumentLoadStatistics& statistics);

 private:
  // Assumes that the parent will be in a local frame relative to this one, upon
  // construction.
  static ActivationState GetParentActivationState(
      content::RenderFrame* render_frame);

  void OnActivateForNextCommittedLoad(const ActivationState& activation_state);
  void RecordHistogramsOnLoadCommitted(const ActivationState& activation_state);
  void RecordHistogramsOnLoadFinished();
  void ResetActivatonStateForNextCommit();

  // content::RenderFrameObserver:
  void OnDestruct() override;
  void DidCommitProvisionalLoad(bool is_new_navigation,
                                bool is_same_document_navigation) override;
  void DidFailProvisionalLoad(const blink::WebURLError& error) override;
  void DidFinishLoad() override;
  bool OnMessageReceived(const IPC::Message& message) override;
  void WillCreateWorkerFetchContext(blink::WebWorkerFetchContext*) override;

  // Subframe navigations matching these URLs/schemes will not trigger
  // ReadyToCommitNavigation in the browser process, so they must be treated
  // specially to maintain activation.
  bool ShouldUseParentActivation(const GURL& url) const;

  // Owned by the ChromeContentRendererClient and outlives us.
  UnverifiedRulesetDealer* ruleset_dealer_;

  ActivationState activation_state_for_next_commit_;

  base::WeakPtr<WebDocumentSubresourceFilterImpl>
      filter_for_last_committed_load_;

  DISALLOW_COPY_AND_ASSIGN(SubresourceFilterAgent);
};

}  // namespace subresource_filter

#endif  // COMPONENTS_SUBRESOURCE_FILTER_CONTENT_RENDERER_SUBRESOURCE_FILTER_AGENT_H_
