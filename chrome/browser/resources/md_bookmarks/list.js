// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Polymer({
  is: 'bookmarks-list',

  behaviors: [
    bookmarks.StoreClient,
  ],

  properties: {
    /**
     * A list of item ids wrapped in an Object. This is necessary because
     * iron-list is unable to distinguish focusing index 6 from focusing id '6'
     * so the item we supply to iron-list needs to be non-index-like.
     * @private {Array<{id: string}>}
     */
    displayedList_: {
      type: Array,
      value: function() {
        // Use an empty list during initialization so that the databinding to
        // hide #list takes effect.
        return [];
      },
    },

    /** @private {Array<string>} */
    displayedIds_: {
      type: Array,
      observer: 'onDisplayedIdsChanged_',
    },

    /** @private */
    searchTerm_: {
      type: String,
      observer: 'onDisplayedListSourceChange_',
    },

    /** @private */
    selectedFolder_: {
      type: String,
      observer: 'onDisplayedListSourceChange_',
    },
  },

  listeners: {
    'click': 'deselectItems_',
    'open-item-menu': 'onOpenItemMenu_',
  },

  attached: function() {
    var list = /** @type {IronListElement} */ (this.$.list);
    list.scrollTarget = this;

    this.watch('displayedIds_', function(state) {
      return bookmarks.util.getDisplayedList(state);
    });
    this.watch('searchTerm_', function(state) {
      return state.search.term;
    });
    this.watch('selectedFolder_', function(state) {
      return state.selectedFolder;
    });
    this.updateFromStore();

    this.$.list.addEventListener(
        'keydown', this.onItemKeydown_.bind(this), true);

    /** @private {function(!Event)} */
    this.boundOnHighlightItems_ = this.onHighlightItems_.bind(this);
    document.addEventListener('highlight-items', this.boundOnHighlightItems_);
  },

  detached: function() {
    document.removeEventListener(
        'highlight-items', this.boundOnHighlightItems_);
  },

  /** @return {HTMLElement} */
  getDropTarget: function() {
    return this.$.message;
  },

  /**
   * Updates `displayedList_` using splices to be equivalent to `newValue`. This
   * allows the iron-list to delete sublists of items which preserves scroll and
   * focus on incremental update.
   * @param {Array<string>} newValue
   * @param {Array<string>} oldValue
   */
  onDisplayedIdsChanged_: function(newValue, oldValue) {
    if (!oldValue) {
      this.displayedList_ = this.displayedIds_.map(function(id) {
        return {id: id};
      });
    } else {
      var splices = Polymer.ArraySplice.calculateSplices(
          /** @type {!Array<string>} */ (newValue),
          /** @type {!Array<string>} */ (oldValue));
      splices.forEach((splice) => {
        // TODO(calamity): Could use notifySplices to improve performance here.
        var additions =
            newValue.slice(splice.index, splice.index + splice.addedCount)
                .map(function(id) {
                  return {id: id};
                });
        this.splice.apply(this, [
          'displayedList_', splice.index, splice.removed.length
        ].concat(additions));
      });
    }
  },

  /** @private */
  onDisplayedListSourceChange_: function() {
    this.scrollTop = 0;
  },

  /**
   * Scroll the list so that |itemId| is visible, if it is not already.
   * @param {string} itemId
   * @private
   */
  scrollToId_: function(itemId) {
    var index = this.displayedIds_.indexOf(itemId);
    var list = this.$.list;
    if (index >= 0 && index < list.firstVisibleIndex ||
        index > list.lastVisibleIndex) {
      list.scrollToIndex(index);
    }
  },

  /** @private */
  emptyListMessage_: function() {
    var emptyListMessage = this.searchTerm_ ? 'noSearchResults' : 'emptyList';
    return loadTimeData.getString(emptyListMessage);
  },

  /** @private */
  isEmptyList_: function() {
    return this.displayedList_.length == 0;
  },

  /** @private */
  deselectItems_: function() {
    this.dispatch(bookmarks.actions.deselectItems());
  },

  /**
   * @param{HTMLElement} el
   * @private
   */
  getIndexForItemElement_: function(el) {
    return this.$.list.modelForElement(el).index;
  },

  /**
   * @param {Event} e
   * @private
   */
  onOpenItemMenu_: function(e) {
    // If the item is not visible, scroll to it before rendering the menu.
    this.scrollToId_(/** @type {BookmarksItemElement} */ (e.path[0]).itemId);
  },

  /**
   * Highlight a list of items by selecting them, scrolling them into view and
   * focusing the first item.
   * @param {Event} e
   * @private
   */
  onHighlightItems_: function(e) {
    // Ensure that we only select items which are actually being displayed.
    // This should only matter if an unrelated update to the bookmark model
    // happens with the perfect timing to end up in a tracked batch update.
    var toHighlight = /** @type {!Array<string>} */
        (e.detail.filter((item) => this.displayedIds_.indexOf(item) != -1));

    assert(toHighlight.length > 0);
    var leadId = toHighlight[0];
    this.dispatch(
        bookmarks.actions.selectAll(toHighlight, this.getState(), leadId));

    // Allow iron-list time to render additions to the list.
    this.async(function() {
      this.scrollToId_(leadId);
      var leadIndex = this.displayedIds_.indexOf(leadId);
      assert(leadIndex != -1);
      this.$.list.focusItem(leadIndex);
    });
  },

  /**
   * @param {KeyboardEvent} e
   * @private
   */
  onItemKeydown_: function(e) {
    var handled = true;
    var list = this.$.list;
    var focusMoved = false;
    var focusedIndex =
        this.getIndexForItemElement_(/** @type {HTMLElement} */ (e.target));
    var oldFocusedIndex = focusedIndex;
    var cursorModifier = cr.isMac ? e.metaKey : e.ctrlKey;
    if (e.key == 'ArrowUp') {
      focusedIndex--;
      focusMoved = true;
    } else if (e.key == 'ArrowDown') {
      focusedIndex++;
      focusMoved = true;
      e.preventDefault();
    } else if (e.key == 'Home') {
      focusedIndex = 0;
      focusMoved = true;
    } else if (e.key == 'End') {
      focusedIndex = list.items.length - 1;
      focusMoved = true;
    } else if (e.key == ' ' && cursorModifier) {
      this.dispatch(bookmarks.actions.selectItem(
          this.displayedIds_[focusedIndex], this.getState(), {
            clear: false,
            range: false,
            toggle: true,
          }));
    } else {
      handled = false;
    }

    if (focusMoved) {
      focusedIndex = Math.min(list.items.length - 1, Math.max(0, focusedIndex));
      list.focusItem(focusedIndex);

      if (cursorModifier && !e.shiftKey) {
        this.dispatch(
            bookmarks.actions.updateAnchor(this.displayedIds_[focusedIndex]));
      } else {
        // If shift-selecting with no anchor, use the old focus index.
        if (e.shiftKey && this.getState().selection.anchor == null) {
          this.dispatch(bookmarks.actions.updateAnchor(
              this.displayedIds_[oldFocusedIndex]));
        }

        // If the focus moved from something other than a Ctrl + move event,
        // update the selection.
        var config = {
          clear: !cursorModifier,
          range: e.shiftKey,
          toggle: false,
        };

        this.dispatch(bookmarks.actions.selectItem(
            this.displayedIds_[focusedIndex], this.getState(), config));
      }
    }

    // Prevent the iron-list from changing focus on enter.
    if (e.path[0] instanceof HTMLButtonElement && e.key == 'Enter')
      handled = true;

    if (!handled) {
      handled = bookmarks.CommandManager.getInstance().handleKeyEvent(
          e, this.getState().selection.items);
    }

    if (handled)
      e.stopPropagation();
  },
});
