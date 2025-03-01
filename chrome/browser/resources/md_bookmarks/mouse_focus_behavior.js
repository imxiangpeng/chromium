// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('bookmarks', function() {
  /** @const */
  var HIDE_FOCUS_RING_ATTRIBUTE = 'hide-focus-ring';

  /**
   * Behavior which adds the 'hide-focus-ring' attribute to a target element
   * when the user interacts with it using the mouse, allowing the focus outline
   * to be hidden without affecting keyboard users.
   * @polymerBehavior
   */
  var MouseFocusBehavior = {
    attached: function() {
      this.boundOnMousedown_ = this.onMousedown_.bind(this);
      this.boundOnKeydown = this.onKeydown_.bind(this);

      // These events are added to the document because capture doesn't work
      // properly when listeners are added to a Polymer element, because the
      // event is considered AT_TARGET for the element, and is evaluated after
      // inner captures.
      document.addEventListener('mousedown', this.boundOnMousedown_, true);
      document.addEventListener('keydown', this.boundOnKeydown, true);
    },

    detached: function() {
      document.removeEventListener('mousedown', this.boundOnMousedown_, true);
      document.removeEventListener('keydown', this.boundOnKeydown, true);
    },

    /** @private */
    onMousedown_: function() {
      this.setAttribute(HIDE_FOCUS_RING_ATTRIBUTE, '');
    },

    /**
     * @param {KeyboardEvent} e
     * @private
     */
    onKeydown_: function(e) {
      if (!['Shift', 'Alt', 'Control', 'Meta'].includes(e.key))
        this.removeAttribute(HIDE_FOCUS_RING_ATTRIBUTE);
    },
  };

  return {
    HIDE_FOCUS_RING_ATTRIBUTE: HIDE_FOCUS_RING_ATTRIBUTE,
    MouseFocusBehavior: MouseFocusBehavior,
  };
});
