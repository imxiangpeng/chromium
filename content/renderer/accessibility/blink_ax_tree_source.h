// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_ACCESSIBILITY_BLINK_AX_TREE_SOURCE_H_
#define CONTENT_RENDERER_ACCESSIBILITY_BLINK_AX_TREE_SOURCE_H_

#include <stdint.h>

#include "content/common/ax_content_node_data.h"
#include "third_party/WebKit/public/web/WebAXObject.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "ui/accessibility/ax_modes.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_source.h"

namespace content {

class BlinkAXTreeSource;
class RenderFrameImpl;

// Create this on the stack to freeze BlinkAXTreeSource and automatically
// un-freeze it when it goes out of scope.
class ScopedFreezeBlinkAXTreeSource {
 public:
  explicit ScopedFreezeBlinkAXTreeSource(BlinkAXTreeSource* tree_source);
  ~ScopedFreezeBlinkAXTreeSource();

 private:
  BlinkAXTreeSource* tree_source_;

  DISALLOW_COPY_AND_ASSIGN(ScopedFreezeBlinkAXTreeSource);
};

class BlinkAXTreeSource
    : public ui::AXTreeSource<blink::WebAXObject,
                              AXContentNodeData,
                              AXContentTreeData> {
 public:
  BlinkAXTreeSource(RenderFrameImpl* render_frame, ui::AXMode mode);
  ~BlinkAXTreeSource() override;

  // Freeze caches the document, accessibility root, and current focused
  // object for fast retrieval during a batch of operations. Use
  // ScopedFreezeBlinkAXTreeSource on the stack rather than calling
  // these directly.
  void Freeze();
  void Thaw();

  // It may be necessary to call SetRoot if you're using a WebScopedAXContext,
  // because BlinkAXTreeSource can't get the root of the tree from the
  // WebDocument if accessibility isn't enabled globally.
  void SetRoot(blink::WebAXObject root);

  // Walks up the ancestor chain to see if this is a descendant of the root.
  bool IsInTree(blink::WebAXObject node) const;

  ui::AXMode accessibility_mode() { return accessibility_mode_; }
  void SetAccessibilityMode(ui::AXMode new_mode);

  // Set the id of the node to fetch image data for. Normally the content
  // of images is not part of the accessibility tree, but one node at a
  // time can be designated as the image data node, which will send the
  // contents of the image with each accessibility update until another
  // node is designated.
  int image_data_node_id() { return image_data_node_id_; }
  void set_image_data_node_id(int id) { image_data_node_id_ = id; }

  void set_max_image_data_size(const gfx::Size& size) {
    max_image_data_size_ = size;
  }

  // Set the id of the node with accessibility focus. The node with
  // accessibility focus will force loading inline text box children,
  // which aren't always loaded by default on all platforms.
  int accessibility_focus_id() { return accessibility_focus_id_; }
  void set_accessibility_focus_id(int id) { accessibility_focus_id_ = id; }

  // AXTreeSource implementation.
  bool GetTreeData(AXContentTreeData* tree_data) const override;
  blink::WebAXObject GetRoot() const override;
  blink::WebAXObject GetFromId(int32_t id) const override;
  int32_t GetId(blink::WebAXObject node) const override;
  void GetChildren(
      blink::WebAXObject node,
      std::vector<blink::WebAXObject>* out_children) const override;
  blink::WebAXObject GetParent(blink::WebAXObject node) const override;
  void SerializeNode(blink::WebAXObject node,
                     AXContentNodeData* out_data) const override;
  bool IsValid(blink::WebAXObject node) const override;
  bool IsEqual(blink::WebAXObject node1,
               blink::WebAXObject node2) const override;
  blink::WebAXObject GetNull() const override;

  blink::WebDocument GetMainDocument() const;

 private:
  const blink::WebDocument& document() const {
    DCHECK(frozen_);
    return document_;
  }
  const blink::WebAXObject& root() const {
    DCHECK(frozen_);
    return root_;
  }
  const blink::WebAXObject& focus() const {
    DCHECK(frozen_);
    return focus_;
  }

  blink::WebAXObject ComputeRoot() const;

  RenderFrameImpl* render_frame_;

  ui::AXMode accessibility_mode_;

  // An explicit root to use, otherwise it's taken from the WebDocument.
  blink::WebAXObject explicit_root_;

  // The id of the object with accessibility focus.
  int accessibility_focus_id_ = -1;

  // The id of the object to fetch image data for.
  int image_data_node_id_ = -1;

  gfx::Size max_image_data_size_;

  // These are updated when calling |Freeze|.
  bool frozen_ = false;
  blink::WebDocument document_;
  blink::WebAXObject root_;
  blink::WebAXObject focus_;
};

}  // namespace content

#endif  // CONTENT_RENDERER_ACCESSIBILITY_BLINK_AX_TREE_SOURCE_H_
