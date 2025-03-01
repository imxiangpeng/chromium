// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


/** @typedef {?{elements: !Array<BookmarkNode>, sameProfile: boolean}} */
var NormalizedDragData;

cr.define('bookmarks', function() {
  /** @const {number} */
  var DRAG_THRESHOLD = 15;

  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkItem(element) {
    return element.tagName == 'BOOKMARKS-ITEM';
  }

  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkFolderNode(element) {
    return element.tagName == 'BOOKMARKS-FOLDER-NODE';
  }

  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkList(element) {
    return element.tagName == 'BOOKMARKS-LIST';
  }

  /**
   * @param {Array<!Element>|undefined} path
   * @return {BookmarkElement}
   */
  function getBookmarkElement(path) {
    if (!path)
      return null;

    for (var i = 0; i < path.length; i++) {
      if (isBookmarkItem(path[i]) || isBookmarkFolderNode(path[i]) ||
          isBookmarkList(path[i])) {
        return path[i];
      }
    }
    return null;
  }

  /**
   * @param {Array<!Element>|undefined} path
   * @return {BookmarkElement}
   */
  function getDragElement(path) {
    var dragElement = getBookmarkElement(path);
    for (var i = 0; i < path.length; i++) {
      if (path[i].tagName == 'BUTTON')
        return null;
    }
    return dragElement && dragElement.getAttribute('draggable') ? dragElement :
                                                                  null;
  }

  /**
   * @param {BookmarkElement} bookmarkElement
   * @return {BookmarkNode}
   */
  function getBookmarkNode(bookmarkElement) {
    return bookmarks.Store.getInstance().data.nodes[bookmarkElement.itemId];
  }

  /**
   * Contains and provides utility methods for drag data sent by the
   * bookmarkManagerPrivate API.
   * @constructor
   */
  function DragInfo() {
    /** @type {NormalizedDragData} */
    this.dragData = null;
  }

  DragInfo.prototype = {
    /** @param {DragData} newDragData */
    setNativeDragData: function(newDragData) {
      this.dragData = {
        sameProfile: newDragData.sameProfile,
        elements:
            newDragData.elements.map((x) => bookmarks.util.normalizeNode(x))
      };
    },

    clearDragData: function() {
      this.dragData = null;
    },

    /** @return {boolean} */
    isDragValid: function() {
      return !!this.dragData;
    },

    /** @return {boolean} */
    isSameProfile: function() {
      return !!this.dragData && this.dragData.sameProfile;
    },

    /** @return {boolean} */
    isDraggingFolders: function() {
      return !!this.dragData && this.dragData.elements.some(function(node) {
        return !node.url;
      });
    },

    /** @return {boolean} */
    isDraggingBookmark: function(bookmarkId) {
      return !!this.dragData && this.isSameProfile() &&
          this.dragData.elements.some(function(node) {
            return node.id == bookmarkId;
          });
    },

    /** @return {boolean} */
    isDraggingChildBookmark: function(folderId) {
      return !!this.dragData && this.isSameProfile() &&
          this.dragData.elements.some(function(node) {
            return node.parentId == folderId;
          });
    },

    /** @return {boolean} */
    isDraggingFolderToDescendant: function(itemId, nodes) {
      if (!this.isSameProfile())
        return false;

      var parentId = nodes[itemId].parentId;
      var parents = {};
      while (parentId) {
        parents[parentId] = true;
        parentId = nodes[parentId].parentId;
      }

      return !!this.dragData && this.dragData.elements.some(function(node) {
        return parents[node.id];
      });
    },
  };


  /**
   * Manages auto expanding of sidebar folders on hover while dragging.
   * @constructor
   */
  function AutoExpander() {
    /** @const {number} */
    this.EXPAND_FOLDER_DELAY = 400;

    /** @private {number} */
    this.lastTimestamp_ = 0;

    /** @private {BookmarkElement|null} */
    this.lastElement_ = null;

    /** @private {number} */
    this.testTimestamp_ = 0;
  }

  AutoExpander.prototype = {
    /**
     * @param {Event} e
     * @param {?BookmarkElement} overElement
     */
    update: function(e, overElement) {
      var eventTimestamp = this.testTimestamp_ || e.timeStamp;
      var itemId = overElement ? overElement.itemId : null;
      var store = bookmarks.Store.getInstance();

      // If hovering over the same folder as last update, open the folder after
      // the delay has passed.
      if (overElement && overElement == this.lastElement_) {
        if (eventTimestamp - this.lastTimestamp_ < this.EXPAND_FOLDER_DELAY)
          return;

        var action = bookmarks.actions.changeFolderOpen(itemId, true);
        store.dispatch(action);
      } else if (
          overElement && isBookmarkFolderNode(overElement) &&
          bookmarks.util.hasChildFolders(itemId, store.data.nodes) &&
          store.data.closedFolders.has(itemId)) {
        // Since this is a closed folder node that has children, set the auto
        // expander to this element.
        this.lastTimestamp_ = eventTimestamp;
        this.lastElement_ = overElement;
        return;
      }

      // If the folder has been expanded or we have moved to a different
      // element, reset the auto expander.
      this.lastTimestamp_ = 0;
      this.lastElement_ = null;
    },
  };

  /**
   * Encapsulates the behavior of the drag and drop indicator which puts a line
   * between items or highlights folders which are valid drop targets.
   * @constructor
   */
  function DropIndicator() {
    /**
     * @private {number|null} Timer id used to help minimize flicker.
     */
    this.removeDropIndicatorTimeoutId_ = null;

    /**
     * The element that had a style applied it to indicate the drop location.
     * This is used to easily remove the style when necessary.
     * @private {BookmarkElement|null}
     */
    this.lastIndicatorElement_ = null;

    /**
     * The style that was applied to indicate the drop location.
     * @private {?string|null}
     */
    this.lastIndicatorClassName_ = null;

    /**
     * Used to instantly remove the indicator style in tests.
     * @private {bookmarks.TimerProxy}
     */
    this.timerProxy = new bookmarks.TimerProxy();
  }

  DropIndicator.prototype = {
    /**
     * Applies the drop indicator style on the target element and stores that
     * information to easily remove the style in the future.
     * @param {HTMLElement} indicatorElement
     * @param {DropPosition} position
     */
    addDropIndicatorStyle: function(indicatorElement, position) {
      var indicatorStyleName = position == DropPosition.ABOVE ?
          'drag-above' :
          position == DropPosition.BELOW ? 'drag-below' : 'drag-on';

      this.lastIndicatorElement_ = indicatorElement;
      this.lastIndicatorClassName_ = indicatorStyleName;

      indicatorElement.classList.add(indicatorStyleName);
    },

    /**
     * Clears the drop indicator style from the last drop target.
     */
    removeDropIndicatorStyle: function() {
      if (!this.lastIndicatorElement_ || !this.lastIndicatorClassName_)
        return;

      this.lastIndicatorElement_.classList.remove(this.lastIndicatorClassName_);
      this.lastIndicatorElement_ = null;
      this.lastIndicatorClassName_ = null;
    },

    /**
     * Displays the drop indicator on the current drop target to give the
     * user feedback on where the drop will occur.
     * @param {DropDestination} dropDest
     */
    update: function(dropDest) {
      this.timerProxy.clearTimeout(this.removeDropIndicatorTimeoutId_);

      var indicatorElement = dropDest.element.getDropTarget();
      var position = dropDest.position;

      this.removeDropIndicatorStyle();
      this.addDropIndicatorStyle(indicatorElement, position);
    },

    /**
     * Stop displaying the drop indicator.
     */
    finish: function() {
      // The use of a timeout is in order to reduce flickering as we move
      // between valid drop targets.
      this.timerProxy.clearTimeout(this.removeDropIndicatorTimeoutId_);
      this.removeDropIndicatorTimeoutId_ = this.timerProxy.setTimeout(() => {
        this.removeDropIndicatorStyle();
      }, 100);
    },
  };

  /**
   * Manages drag and drop events for the bookmarks-app.
   *
   * This class manages an internal drag and drop based on mouse events and then
   * delegates to the native drag and drop in chrome.bookmarkManagerPrivate when
   * the mouse leaves the web content area. This allows us to render a drag and
   * drop chip UI for internal drags, while correctly handling and avoiding
   * conflict with native drags.
   *
   * The event flows look like
   *
   * mousedown -> mousemove -> mouseup
   *               |
   *               v
   *      dragstart/dragleave (if the drag leaves the browser window)
   *               |
   *               v
   * external drag -> bookmarkManagerPrivate.onDragEnter -> dragover -> drop
   *
   * @constructor
   */
  function DNDManager() {
    /** @private {bookmarks.DragInfo} */
    this.dragInfo_ = null;

    /** @private {?DropDestination} */
    this.dropDestination_ = null;

    /** @private {bookmarks.DropIndicator} */
    this.dropIndicator_ = null;

    /** @private {Object<string, function(!Event)>} */
    this.documentListeners_ = null;

    /**
     * Used to instantly clearDragData in tests.
     * @private {bookmarks.TimerProxy}
     */
    this.timerProxy_ = new bookmarks.TimerProxy();

    /**
     * The bookmark drag and drop indicator chip.
     * @private {BookmarksDndChipElement}
     */
    this.chip_ = null;

    /**
     * The element that initiated an internal drag. Not used once native drag
     * starts.
     * @private {BookmarkElement}
     */
    this.internalDragElement_ = null;

    /**
     * Where the internal drag started.
     * @private {?{x: number, y: number}}
     */
    this.mouseDownPos_ = null;
  }

  DNDManager.prototype = {
    init: function() {
      this.dragInfo_ = new DragInfo();
      this.dropIndicator_ = new DropIndicator();
      this.autoExpander_ = new AutoExpander();

      this.documentListeners_ = {
        'mousedown': this.onMousedown_.bind(this),
        'mousemove': this.onMouseMove_.bind(this),
        'mouseup': this.onMouseUp_.bind(this),
        'mouseleave': this.onMouseLeave_.bind(this),

        'dragstart': this.onDragStart_.bind(this),
        'dragenter': this.onDragEnter_.bind(this),
        'dragover': this.onDragOver_.bind(this),
        'dragleave': this.onDragLeave_.bind(this),
        'drop': this.onDrop_.bind(this),
        'dragend': this.clearDragData_.bind(this),
        // TODO(calamity): Add touch support.
      };
      for (var event in this.documentListeners_)
        document.addEventListener(event, this.documentListeners_[event]);

      chrome.bookmarkManagerPrivate.onDragEnter.addListener(
          this.handleChromeDragEnter_.bind(this));
      chrome.bookmarkManagerPrivate.onDragLeave.addListener(
          this.clearDragData_.bind(this));
      chrome.bookmarkManagerPrivate.onDrop.addListener(
          this.clearDragData_.bind(this));
    },

    destroy: function() {
      if (this.chip_ && this.chip_.parentElement)
        document.body.removeChild(this.chip_);

      for (var event in this.documentListeners_)
        document.removeEventListener(event, this.documentListeners_[event]);
    },

    ////////////////////////////////////////////////////////////////////////////
    // MouseEvent handlers:

    /**
     * @private
     * @param {Event} e
     */
    onMousedown_: function(e) {
      var dragElement = getDragElement(e.path);
      if (e.button != 0 || !dragElement)
        return;

      this.internalDragElement_ = dragElement;
      this.mouseDownPos_ = {
        x: e.clientX,
        y: e.clientY,
      };
    },

    /**
     * @private
     * @param {Event} e
     */
    onMouseMove_: function(e) {
      // mousemove events still fire when dragged onto the the bookmarks bar.
      // Once we are outside of the web contents, allow the native drag to
      // start.
      if (!this.internalDragElement_ || e.clientX < 0 ||
          e.clientX > window.innerWidth || e.clientY < 0 ||
          e.clientY > window.innerHeight) {
        return;
      }

      this.dropDestination_ = null;

      // Prevents a native drag from starting.
      e.preventDefault();

      // On the first mousemove after a mousedown, calculate the items to drag.
      // This can't be done in mousedown because the user may be shift-clicking
      // an item.
      if (!this.dragInfo_.isDragValid()) {
        // If the mouse hasn't been moved far enough, defer to next mousemove.
        if (Math.abs(this.mouseDownPos_.x - e.clientX) < DRAG_THRESHOLD &&
            Math.abs(this.mouseDownPos_.y - e.clientY) < DRAG_THRESHOLD) {
          return;
        }

        var dragData = this.calculateDragData_();
        if (!dragData) {
          this.clearDragData_();
          return;
        }

        this.dragInfo_.dragData = dragData;
      }

      var state = bookmarks.Store.getInstance().data;
      var items = this.dragInfo_.dragData.elements;
      this.dndChip.showForItems(
          e.clientX, e.clientY, items,
          this.internalDragElement_ ?
              state.nodes[this.internalDragElement_.itemId] :
              items[0]);

      this.onDragOverCommon_(e);
    },

    /**
     * This event fires when the mouse leaves the browser window (not the web
     * content area).
     * @private
     */
    onMouseLeave_: function() {
      if (!this.internalDragElement_)
        return;

      this.startNativeDrag_();
    },

    /**
     * @private
     */
    onMouseUp_: function() {
      if (!this.internalDragElement_)
        return;

      if (this.dropDestination_) {
        // Complete the drag by moving all dragged items to the drop
        // destination.
        var dropInfo = this.calculateDropInfo_(this.dropDestination_);
        var shouldHighlight = this.shouldHighlight_(this.dropDestination_);

        var movePromises = this.dragInfo_.dragData.elements.map((item) => {
          return new Promise((resolve) => {
            chrome.bookmarks.move(item.id, {
              parentId: dropInfo.parentId,
              index: dropInfo.index == -1 ? undefined : dropInfo.index
            }, resolve);
          });
        });

        if (shouldHighlight) {
          bookmarks.ApiListener.trackUpdatedItems();
          Promise.all(movePromises)
              .then(() => bookmarks.ApiListener.highlightUpdatedItems());
        }
      }

      this.clearDragData_();
    },

    ////////////////////////////////////////////////////////////////////////////
    // DragEvent handlers:

    /**
     * This should only fire when a mousemove goes from the content area to the
     * browser chrome.
     * @private
     * @param {Event} e
     */
    onDragStart_: function(e) {
      // |e| will be for the originally dragged bookmark item which dragstart
      // was disabled for due to mousemove's preventDefault.
      var dragElement = getDragElement(e.path);
      if (!dragElement)
        return;

      // Prevent normal drags of all bookmark items.
      e.preventDefault();

      if (!this.startNativeDrag_())
        return;

      // If we are dragging a single link, we can do the *Link* effect.
      // Otherwise, we only allow copy and move.
      if (e.dataTransfer) {
        var draggedNodes = this.dragInfo_.dragData.elements;
        e.dataTransfer.effectAllowed =
            draggedNodes.length == 1 && draggedNodes[0].url ? 'copyLink' :
                                                              'copyMove';
      }
    },

    /** @private */
    onDragLeave_: function() {
      this.dropIndicator_.finish();
    },

    /**
     * @private
     * @param {!Event} e
     */
    onDrop_: function(e) {
      if (this.dropDestination_) {
        e.preventDefault();

        var dropInfo = this.calculateDropInfo_(this.dropDestination_);
        var index = dropInfo.index != -1 ? dropInfo.index : undefined;
        var shouldHighlight = this.shouldHighlight_(this.dropDestination_);

        if (shouldHighlight)
          bookmarks.ApiListener.trackUpdatedItems();

        chrome.bookmarkManagerPrivate.drop(
            dropInfo.parentId, index,
            shouldHighlight ? bookmarks.ApiListener.highlightUpdatedItems :
                              undefined);
      }

      this.clearDragData_();
    },

    /**
     * @private
     * @param {Event} e
     */
    onDragEnter_: function(e) {
      e.preventDefault();
    },

    /**
     * @private
     * @param {Event} e
     */
    onDragOver_: function(e) {
      this.dropDestination_ = null;

      // This is necessary to actually trigger the 'none' effect, even though
      // the event will have this set to 'none' already.
      if (e.dataTransfer)
        e.dataTransfer.dropEffect = 'none';

      // Allow normal DND on text inputs.
      if (e.path[0].tagName == 'INPUT')
        return;

      // The default operation is to allow dropping links etc to do
      // navigation. We never want to do that for the bookmark manager.
      e.preventDefault();

      if (!this.dragInfo_.isDragValid())
        return;

      if (this.onDragOverCommon_(e) && e.dataTransfer) {
        e.dataTransfer.dropEffect =
            this.dragInfo_.isSameProfile() ? 'move' : 'copy';
      }
    },

    /**
     * @private
     * @param {DragData} dragData
     */
    handleChromeDragEnter_: function(dragData) {
      this.dragInfo_.setNativeDragData(dragData);
    },

    ////////////////////////////////////////////////////////////////////////////
    // Common drag methods:

    /** @private */
    clearDragData_: function() {
      this.dndChip.hide();
      this.internalDragElement_ = null;
      this.mouseDownPos_ = null;

      // Defer the clearing of the data so that the bookmark manager API's drop
      // event doesn't clear the drop data before the web drop event has a
      // chance to execute (on Mac).
      this.timerProxy_.setTimeout(() => {
        this.dragInfo_.clearDragData();
        this.dropDestination_ = null;
        this.dropIndicator_.finish();
      }, 0);
    },

    /**
     * Starts a native drag by sending a message to the browser.
     * @private
     * @return {boolean}
     */
    startNativeDrag_: function() {
      var state = bookmarks.Store.getInstance().data;
      this.dndChip.hide();

      if (!this.dragInfo_.isDragValid())
        return false;

      var draggedNodes =
          this.dragInfo_.dragData.elements.map((item) => item.id);

      // TODO(calamity): account for touch.
      chrome.bookmarkManagerPrivate.startDrag(draggedNodes, false);

      return true;
    },

    /**
     * @private
     * @param {Event} e
     * @return {boolean}
     */
    onDragOverCommon_: function(e) {
      var state = bookmarks.Store.getInstance().data;
      var items = this.dragInfo_.dragData.elements;

      var overElement = getBookmarkElement(e.path);
      this.autoExpander_.update(e, overElement);
      if (!overElement) {
        this.dropIndicator_.finish();
        return false;
      }

      // Now we know that we can drop. Determine if we will drop above, on or
      // below based on mouse position etc.
      this.dropDestination_ =
          this.calculateDropDestination_(e.clientY, overElement);
      if (!this.dropDestination_) {
        this.dropIndicator_.finish();
        return false;
      }

      this.dropIndicator_.update(this.dropDestination_);

      return true;
    },

    ////////////////////////////////////////////////////////////////////////////
    // Other methods:

    /**
     * @param {DropDestination} dropDestination
     * @return {{parentId: string, index: number}}
     */
    calculateDropInfo_: function(dropDestination) {
      if (isBookmarkList(dropDestination.element)) {
        return {
          index: 0,
          parentId: bookmarks.Store.getInstance().data.selectedFolder,
        };
      }

      var node = getBookmarkNode(dropDestination.element);
      var position = dropDestination.position;
      var index = -1;
      var parentId = node.id;

      if (position != DropPosition.ON) {
        var state = bookmarks.Store.getInstance().data;

        // Drops between items in the normal list and the sidebar use the drop
        // destination node's parent.
        parentId = assert(node.parentId);
        index = state.nodes[parentId].children.indexOf(node.id);

        if (position == DropPosition.BELOW)
          index++;
      }

      return {
        index: index,
        parentId: parentId,
      };
    },

    /**
     * Calculates which items should be dragged based on the initial drag item
     * and the current selection. Dragged items will end up selected.
     * @private
     */
    calculateDragData_: function() {
      var dragId = this.internalDragElement_.itemId;
      var store = bookmarks.Store.getInstance();
      var state = store.data;

      // Determine the selected bookmarks.
      var draggedNodes = Array.from(state.selection.items);

      // Change selection to the dragged node if the node is not part of the
      // existing selection.
      if (isBookmarkFolderNode(this.internalDragElement_) ||
          draggedNodes.indexOf(dragId) == -1) {
        store.dispatch(bookmarks.actions.deselectItems());
        if (!isBookmarkFolderNode(this.internalDragElement_)) {
          store.dispatch(bookmarks.actions.selectItem(dragId, state, {
            clear: false,
            range: false,
            toggle: false,
          }));
        }
        draggedNodes = [dragId];
      }

      // If any node can't be dragged, end the drag.
      var anyUnmodifiable = draggedNodes.some(
          (itemId) => !bookmarks.util.canEditNode(state, itemId));

      if (anyUnmodifiable)
        return null;

      return {
        elements: draggedNodes.map((id) => state.nodes[id]),
        sameProfile: true,
      };
    },

    /**
     * This function determines where the drop will occur.
     * @private
     * @param {number} elementClientY
     * @param {!BookmarkElement} overElement
     * @return {?DropDestination} If no valid drop position is found, null,
     *   otherwise:
     *       element - The target element that will receive the drop.
     *       position - A |DropPosition| relative to the |element|.
     */
    calculateDropDestination_: function(elementClientY, overElement) {
      var validDropPositions = this.calculateValidDropPositions_(overElement);
      if (validDropPositions == DropPosition.NONE)
        return null;

      var above = validDropPositions & DropPosition.ABOVE;
      var below = validDropPositions & DropPosition.BELOW;
      var on = validDropPositions & DropPosition.ON;
      var rect = overElement.getDropTarget().getBoundingClientRect();
      var yRatio = (elementClientY - rect.top) / rect.height;

      if (above && (yRatio <= .25 || yRatio <= .5 && (!below || !on)))
        return {element: overElement, position: DropPosition.ABOVE};

      if (below && (yRatio > .75 || yRatio > .5 && (!above || !on)))
        return {element: overElement, position: DropPosition.BELOW};

      if (on)
        return {element: overElement, position: DropPosition.ON};

      return null;
    },

    /**
     * Determines the valid drop positions for the given target element.
     * @private
     * @param {!BookmarkElement} overElement The element that we are currently
     *     dragging over.
     * @return {DropPosition} An bit field enumeration of valid drop locations.
     */
    calculateValidDropPositions_: function(overElement) {
      var dragInfo = this.dragInfo_;
      var state = bookmarks.Store.getInstance().data;
      var itemId = overElement.itemId;

      // Drags aren't allowed onto the search result list.
      if ((isBookmarkList(overElement) || isBookmarkItem(overElement)) &&
          bookmarks.util.isShowingSearch(state)) {
        return DropPosition.NONE;
      }

      if (isBookmarkList(overElement))
        itemId = state.selectedFolder;

      if (!bookmarks.util.canReorderChildren(state, itemId))
        return DropPosition.NONE;

      // Drags of a bookmark onto itself or of a folder into its children aren't
      // allowed.
      if (dragInfo.isDraggingBookmark(itemId) ||
          dragInfo.isDraggingFolderToDescendant(itemId, state.nodes)) {
        return DropPosition.NONE;
      }

      var validDropPositions = this.calculateDropAboveBelow_(overElement);
      if (this.canDropOn_(overElement))
        validDropPositions |= DropPosition.ON;

      return validDropPositions;
    },

    /**
     * @private
     * @param {BookmarkElement} overElement
     * @return {DropPosition}
     */
    calculateDropAboveBelow_: function(overElement) {
      var dragInfo = this.dragInfo_;
      var state = bookmarks.Store.getInstance().data;

      if (isBookmarkList(overElement))
        return DropPosition.NONE;

      // We cannot drop between Bookmarks bar and Other bookmarks.
      if (getBookmarkNode(overElement).parentId == ROOT_NODE_ID)
        return DropPosition.NONE;

      var isOverFolderNode = isBookmarkFolderNode(overElement);

      // We can only drop between items in the tree if we have any folders.
      if (isOverFolderNode && !dragInfo.isDraggingFolders())
        return DropPosition.NONE;

      var validDropPositions = DropPosition.NONE;

      // Cannot drop above if the item above is already in the drag source.
      var previousElem = overElement.previousElementSibling;
      if (!previousElem || !dragInfo.isDraggingBookmark(previousElem.itemId))
        validDropPositions |= DropPosition.ABOVE;

      // Don't allow dropping below an expanded sidebar folder item since it is
      // confusing to the user anyway.
      if (isOverFolderNode && !state.closedFolders.has(overElement.itemId) &&
          bookmarks.util.hasChildFolders(overElement.itemId, state.nodes)) {
        return validDropPositions;
      }

      var nextElement = overElement.nextElementSibling;

      // Cannot drop below if the item below is already in the drag source.
      if (!nextElement || !dragInfo.isDraggingBookmark(nextElement.itemId))
        validDropPositions |= DropPosition.BELOW;

      return validDropPositions;
    },

    /**
     * Determine whether we can drop the dragged items on the drop target.
     * @private
     * @param {!BookmarkElement} overElement The element that we are currently
     *     dragging over.
     * @return {boolean} Whether we can drop the dragged items on the drop
     *     target.
     */
    canDropOn_: function(overElement) {
      // Allow dragging onto empty bookmark lists.
      if (isBookmarkList(overElement)) {
        var state = bookmarks.Store.getInstance().data;
        return state.selectedFolder &&
            state.nodes[state.selectedFolder].children.length == 0;
      }

      // We can only drop on a folder.
      if (getBookmarkNode(overElement).url)
        return false;

      return !this.dragInfo_.isDraggingChildBookmark(overElement.itemId);
    },

    /**
     * @param {DropDestination} dropDestination
     * @private
     */
    shouldHighlight_: function(dropDestination) {
      return isBookmarkItem(dropDestination.element) ||
          isBookmarkList(dropDestination.element);
    },

    /** @param {bookmarks.TimerProxy} timerProxy */
    setTimerProxyForTesting: function(timerProxy) {
      this.timerProxy_ = timerProxy;
      this.dropIndicator_.timerProxy = timerProxy;
    },

    /** @return {BookmarksDndChipElement} */
    get dndChip() {
      if (!this.chip_) {
        this.chip_ =
            /** @type {BookmarksDndChipElement} */ (
                document.createElement('bookmarks-dnd-chip'));
        document.body.appendChild(this.chip_);
      }

      return this.chip_;
    },
  };

  return {
    DNDManager: DNDManager,
    DragInfo: DragInfo,
    DropIndicator: DropIndicator,
  };
});
