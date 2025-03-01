// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_
#define CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "content/browser/site_instance_impl.h"
#include "content/public/common/page_state.h"
#include "content/public/common/referrer.h"
#include "content/public/common/resource_request_body.h"

namespace content {

// Represents a session history item for a particular frame.  It is matched with
// corresponding FrameTreeNodes using unique name (or by the root position).
//
// This class is refcounted and can be shared across multiple NavigationEntries.
// For now, it is owned by a single NavigationEntry and only tracks the main
// frame.
//
// If SiteIsolationPolicy::UseSubframeNavigationEntries is true, there will be a
// tree of FrameNavigationEntries in each NavigationEntry, one per frame.
// TODO(creis): Share these FrameNavigationEntries across NavigationEntries if
// the frame hasn't changed.
class CONTENT_EXPORT FrameNavigationEntry
    : public base::RefCounted<FrameNavigationEntry> {
 public:
  FrameNavigationEntry();
  FrameNavigationEntry(const std::string& frame_unique_name,
                       int64_t item_sequence_number,
                       int64_t document_sequence_number,
                       scoped_refptr<SiteInstanceImpl> site_instance,
                       scoped_refptr<SiteInstanceImpl> source_site_instance,
                       const GURL& url,
                       const Referrer& referrer,
                       const std::string& method,
                       int64_t post_id);

  // Creates a copy of this FrameNavigationEntry that can be modified
  // independently from the original.
  FrameNavigationEntry* Clone() const;

  // Updates all the members of this entry.
  void UpdateEntry(const std::string& frame_unique_name,
                   int64_t item_sequence_number,
                   int64_t document_sequence_number,
                   SiteInstanceImpl* site_instance,
                   scoped_refptr<SiteInstanceImpl> source_site_instance,
                   const GURL& url,
                   const Referrer& referrer,
                   const std::vector<GURL>& redirect_chain,
                   const PageState& page_state,
                   const std::string& method,
                   int64_t post_id);

  // The unique name of the frame this entry is for.  This is a stable name for
  // the frame based on its position in the tree and relation to other named
  // frames, which does not change after cross-process navigations or restores.
  // Only the main frame can have an empty name.
  //
  // This is unique relative to other frames in the same page, but not among
  // other pages (i.e., not globally unique).
  const std::string& frame_unique_name() const { return frame_unique_name_; }
  void set_frame_unique_name(const std::string& frame_unique_name) {
    frame_unique_name_ = frame_unique_name;
  }

  // Keeps track of where this entry belongs in the frame's session history.
  // The item sequence number identifies each stop in the back/forward history
  // and is globally unique.  The document sequence number increments for each
  // new document and is also globally unique.  In-page navigations get a new
  // item sequence number but the same document sequence number.  These numbers
  // should not change once assigned.
  void set_item_sequence_number(int64_t item_sequence_number);
  int64_t item_sequence_number() const { return item_sequence_number_; }
  void set_document_sequence_number(int64_t document_sequence_number);
  int64_t document_sequence_number() const { return document_sequence_number_; }

  // The SiteInstance responsible for rendering this frame.  All frames sharing
  // a SiteInstance must live in the same process.  This is a refcounted pointer
  // that keeps the SiteInstance (not necessarily the process) alive as long as
  // this object remains in the session history.
  void set_site_instance(scoped_refptr<SiteInstanceImpl> site_instance) {
    site_instance_ = std::move(site_instance);
  }
  SiteInstanceImpl* site_instance() const { return site_instance_.get(); }

  // The |source_site_instance| is used to identify the SiteInstance of the
  // frame that initiated the navigation. It is present only for
  // renderer-initiated navigations and is cleared once the navigation has
  // committed.
  void set_source_site_instance(
      scoped_refptr<SiteInstanceImpl> source_site_instance) {
    source_site_instance_ = std::move(source_site_instance);
  }
  SiteInstanceImpl* source_site_instance() const {
    return source_site_instance_.get();
  }

  // The actual URL loaded in the frame.  This is in contrast to the virtual
  // URL, which is shown to the user.
  void set_url(const GURL& url) { url_ = url; }
  const GURL& url() const { return url_; }

  // The referring URL.  Can be empty.
  void set_referrer(const Referrer& referrer) { referrer_ = referrer; }
  const Referrer& referrer() const { return referrer_; }

  // The redirect chain traversed during this frame navigation, from the initial
  // redirecting URL to the final non-redirecting current URL.
  void set_redirect_chain(const std::vector<GURL>& redirect_chain) {
    redirect_chain_ = redirect_chain;
  }
  const std::vector<GURL>& redirect_chain() const { return redirect_chain_; }

  void SetPageState(const PageState& page_state);
  const PageState& page_state() const { return page_state_; }

  // The HTTP method used to navigate.
  const std::string& method() const { return method_; }
  void set_method(const std::string& method) { method_ = method; }

  // The id of the post corresponding to this navigation or -1 if the
  // navigation was not a POST.
  int64_t post_id() const { return post_id_; }
  void set_post_id(int64_t post_id) { post_id_ = post_id; }

  // The data sent during a POST navigation. Returns nullptr if the navigation
  // is not a POST.
  scoped_refptr<ResourceRequestBody> GetPostData() const;

 private:
  friend class base::RefCounted<FrameNavigationEntry>;
  virtual ~FrameNavigationEntry();

  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
  // Add all new fields to |UpdateEntry|.
  // TODO(creis): These fields have implications for session restore.  This is
  // currently managed by NavigationEntry, but the logic will move here.
  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING

  // See the accessors above for descriptions.
  std::string frame_unique_name_;
  int64_t item_sequence_number_;
  int64_t document_sequence_number_;
  scoped_refptr<SiteInstanceImpl> site_instance_;
  // This member is cleared at commit time and is not persisted.
  scoped_refptr<SiteInstanceImpl> source_site_instance_;
  GURL url_;
  Referrer referrer_;
  // This is used when transferring a pending entry from one process to another.
  // We also send the main frame's redirect chain through session sync for
  // offline analysis.
  // It is preserved after commit but should not be persisted.
  std::vector<GURL> redirect_chain_;
  // TODO(creis): Change this to FrameState.
  PageState page_state_;
  std::string method_;
  int64_t post_id_;

  DISALLOW_COPY_AND_ASSIGN(FrameNavigationEntry);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_
