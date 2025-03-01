// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Login UI header bar implementation. The header bar is shown on
 * top of login UI (not to be confused with login.HeaderBar which is shown at
 * the bottom and contains login shelf elements).
 * It contains device version labels, and new-note action button if it's
 * enabled (which should only be the case on lock screen).
 */

cr.define('login', function() {

  /**
   * Calculates diagonal length of a rectangle with the provided sides.
   * @param {!number} x The rectangle width.
   * @param {!number} y The rectangle height.
   * @return {!number} The rectangle diagonal.
   */
  function diag(x, y) {
    return Math.sqrt(x * x + y * y);
  }

  /**
   * Returns Manhattan distance between two touch events.
   * @param {!TouchInfo} a
   * @param {!TouchInfo} b
   * @return !number The distance between two touch event (client) coordinates.
   */
  function manhattanDistance(a, b) {
    return Math.abs(a.x - b.x) + Math.abs(a.y - b.y);
  }

  /**
   * The minimal Manhattan distance between two touch events for the touch to
   * be considered as moved.
   * @const {!number}
   */
  var TOUCH_MOVE_THRESHOLD = 1;

  /**
   * The time in milliseconds after which the current touch should be considered
   * stopped.
   * @const {!number}
   */
  var TOUCH_STOP_TIMEOUT_MS = 300;

  /**
   * Minimal velocity in px per ms for a swipe movement to be considered
   * actionable.
   * @const {!number}
   */
  var MIN_SWIPE_VELOCITY = 0.4;

  /**
   * Contains data from a touch event relevant for swipe detection.
   * @param {!Touch} touch The wrapped touch event.
   */
  function TouchInfo(touch, timeStamp) {
    /**
     * The touch event horizontal client coordinate.
     * @type {!number}
     */
    this.x = touch.clientX;

    /**
     * The touch event vertical client coordinate.
     * @type {!number}
     */
    this.y = touch.clientY;

    /**
     * The time the touch event was recorded (and the TouchInfo was created).
     * @type {!number}
     */
    this.time = Date.now();
  }

  /**
   * Used to detect potential swipe gestures originating from the provided HTML
   * element. It tracks touch events on the element and remembers the last
   * touch move longer than TOUCH_MOVE_THRESHOLD. On touch gesture end, it
   * invokes the callback with the velocity vector of the last movement that was
   * long enough - the vector can then be used to determine if the last movement
   * should be considered a swipe or not.
   *
   * @param  {!HTMLElement} element The element whose touch events should be
   *     tracked.
   * @param  {!function(!{x: !number, y: !number})} swipeCallback Called when
   *     a swipe has been detected - the argument will be an object representing
   *     2-D swipe movement velocity vector.
   */
  function SwipeDetector(element, swipeCallback) {
    element.addEventListener('touchstart', this.onTouchMove_.bind(this));
    element.addEventListener('touchend', this.onTouchEnd_.bind(this));
    element.addEventListener('touchcancel', this.clearSwipe_.bind(this));
    element.addEventListener('touchmove', this.onTouchMove_.bind(this));

    /** @private {!function(!{x: !number, y: !number})} */
    this.swipeCallback_ = swipeCallback;
  }

  SwipeDetector.prototype = {
    /**
     * The last detected movement of the tracked touch point between two points
     * (start and end) that are more than <code>TOUCH_MOVE_THRESHOLD</code>
     * apart. When the touch event sequence ends (on touchend event), this
     * movement will define the vector that represents the final swipe gesture.
     * @private {?{start: !TouchInfo, end: !TouchInfo}}
     */
    current_: null,

    /**
     * The last detected touch point. It is a candidate for start of the next
     * touch movement (which is defined by this.current_). When the next touch
     * event is received, if the received event is far enough from this touch,
     * the vector defined by these two events will become the new this.current_
     * value.
     * @private {?TouchInfo}
     */
    nextStart_: null,

    /**
     * The tracked touch event ID. <code>undefined</code> if a touch is not
     * being tracked.
     * @private {number|undefined}
     */
    touchId_: undefined,

    /**
     * Reference to the timeout posted to invalidate current swipe touch data.
     * If a timeout was not posted, it will be set to <code>undefined</code>.
     * @private {number|undefined}
     */
    swipeTimeout_: undefined,

    /**
     * Whether swipe detector is enabled.
     * @private {boolean}
     */
    enabled_: false,

    /**
     * @param {boolean} enabled Whether the swipe detector should be enabled.
     */
    setEnabled: function(enabled) {
      if (this.enabled_ == enabled)
        return;
      if (!enabled)
        this.clearSwipe_();
      this.enabled_ = enabled;
    },

    /**
     * Whether the swipe detector started tracking a touch event, and at least
     * one (valid) touch event was detected.
     * @return {boolean}
     */
    started_: function() {
      return !!this.nextStart_;
    },

    /**
     * Processes a detected touch start/move event, updating current swipe
     * detector state - the last detected touch movement (i.e. the last two
     * touch events that are distant enough to be considered a movement) and the
     * last detected touch event.
     * @param {!TouchInfo} touch The relevant touch event information.
     */
    addTouch_: function(touch) {
      // If the new touch is distant enough from the last recorded touch event,
      // update the current movement (potential swipe).
      if (this.nextStart_ &&
          manhattanDistance(this.nextStart_, touch) >= TOUCH_MOVE_THRESHOLD) {
        this.current_ = {start: this.nextStart_, end: touch};
      }
      // Use the touch as the potential start of the next movement.
      this.nextStart_ = touch;
    },

    /**
     * Handles swipe timeout - if there is a current detected swipe, it gets
     * cleared if it did not start within the TOUCH_STOP_TIMEOUT_MS amount of
     * time from now. Similarly, if recorded potential start of the next is not
     * within the required time, it gets cleared as well.
     * @private
     */
    handleSwipeTimeout_: function() {
      this.swipeTimeout_ = undefined;

      if (!this.started_())
        return;

      /**
       * @param {!number} The time stamp of touch event.
       * @param {!number} The current time stamp.
       * @return Whether touchTime is within the valid time interval from now.
       */
      function isTouchTimeValid(touchTime, now) {
        var timeDelta = now - touchTime;
        return timeDelta >= 0 && timeDelta < TOUCH_STOP_TIMEOUT_MS;
      }

      // Detect whether detected events are within valid time interval, and
      // record the latest detected event timestamp.
      var now = Date.now();
      var timeSinceEvent = -1;
      if (this.current_ && isTouchTimeValid(this.current_.start.time, now)) {
        timeSinceEvent = now - this.current_.start.time;
      } else if (isTouchTimeValid(this.nextStart_.time, now)) {
        this.current_ = null;
        timeSinceEvent = now - this.nextStart_.time;
      }

      if (timeSinceEvent < 0 || timeSinceEvent > TOUCH_STOP_TIMEOUT_MS) {
        this.clearSwipe_();
        return;
      }

      // Schedule the next interval to be |TOUCH_STOP_TIMEOUT_MS| from the
      // latest detected touch event.
      this.swipeTimeout_ = setTimeout(
          this.handleSwipeTimeout_.bind(this),
          TOUCH_STOP_TIMEOUT_MS - timeSinceEvent);
    },

    /**
     * Cancels the currently tracked movement, and resets cached touch data.
     * @private
     */
    clearSwipe_: function() {
      if (this.swipeTimeout_ !== undefined)
        clearTimeout(this.swipeTimeout_);
      this.swipeTimeout_ = undefined;
      this.touchId_ = undefined;
      this.current_ = null;
      this.nextStart_ = null;
    },

    /**
     * Handler for touch move and start events.
     * @param {TouchEvent} e
     * @private
     */
    onTouchMove_: function(e) {
      if (!this.enabled_)
        return;

      // Cancel touch movement tracking if there is more than one touch point,
      // or if the current touch point is not associated with the tracked
      // element.
      if (e.touches.length != 1 || e.targetTouches.length != 1) {
        this.clearSwipe_();
        return;
      }

      var touch = e.touches.item(0);
      // Start/restart movement tracking if the touch event sequence is not
      // being tracked, or the touch pointer that should be tracked changed.
      if (!this.started_() || touch.identifier != this.touchId_) {
        this.clearSwipe_();

        // Invalidate the current detected swipe after a timeout - for example
        // to handle situation where the touch stops moving immediately after
        // a swipe, and the touch is ended without further movement after some
        // time.
        this.swipeTimeout_ = setTimeout(
            this.handleSwipeTimeout_.bind(this), TOUCH_STOP_TIMEOUT_MS);

        this.touchId_ = touch.identifier;
      }

      this.addTouch_(new TouchInfo(touch));
    },

    /**
     * Handler for touch end event.
     * @param {TouchEvent} e
     * @private
     */
    onTouchEnd_: function(e) {
      if (!this.enabled_)
        return;

      // Cancel the swipe if there are still other touch contact points when the
      // touch ends, or if the finished touch is not associated with the
      // currently tracked touch sequence.
      if (e.touches.length != 0 || e.changedTouches.length > 1 ||
          e.changedTouches.item(0).identifier != this.touchId_) {
        this.clearSwipe_();
        return;
      }

      // Process the final touch event.
      this.addTouch_(new TouchInfo(e.changedTouches.item(0)));

      // Remember the last potential swipe movement, and clear the tracked
      // touch data.
      var current = this.current_;
      this.clearSwipe_();

      if (!current)
        return;

      var velocity = {
        x: current.end.x - current.start.x,
        y: current.end.y - current.start.y
      };
      if (!velocity.x && !velocity.y)
        return;

      var time = current.end.time - current.start.time;
      if (!time)
        return;

      this.swipeCallback_({x: velocity.x / time, y: velocity.y / time});
    }
  };

  /**
   * Creates a header bar element shown at the top of the login screen.
   *
   * @constructor
   * @extends {HTMLDivElement}
   */
  var TopHeaderBar = cr.ui.define('div');

  TopHeaderBar.prototype = {
    __proto__: HTMLDivElement.prototype,

    /**
     * The current state of lock screen apps.
     * @private {!LockScreenAppsState}
     */
    lockScreenAppsState_: LOCK_SCREEN_APPS_STATE.NONE,

    /** @private {SwipeDetector} */
    swipeDetector_: null,

    set lockScreenAppsState(state) {
      if (this.lockScreenAppsState_ == state)
        return;

      this.lockScreenAppsState_ = state;
      this.updateUi_();
    },

    /** override */
    decorate: function() {
      $('new-note-action')
          .addEventListener('click', this.activateNoteAction_.bind(this));
      $('new-note-action')
          .addEventListener(
              'keydown', this.handleNewNoteActionKeyDown_.bind(this));
      this.swipeDetector_ =
          new SwipeDetector($('new-note-action'), this.handleSwipe_.bind(this));
    },

    /**
     * @private
     */
    updateUi_: function() {
      // Shorten transition duration when moving to available state, in
      // particular when moving from foreground state - when moving from
      // foreground state container gets cropped to top right corner
      // immediately, while action element is updated from full screen with
      // a transition. If the transition is too long, the action would be
      // displayed as full square for a fraction of a second, which can look
      // janky.
      if (this.lockScreenAppsState_ == LOCK_SCREEN_APPS_STATE.AVAILABLE) {
        $('new-note-action')
            .classList.toggle('new-note-action-short-transition', true);
        this.runOnNoteActionTransitionEnd_(function() {
          $('new-note-action')
              .classList.toggle('new-note-action-short-transition', false);
        });
      }

      this.swipeDetector_.setEnabled(
          this.lockScreenAppsState_ == LOCK_SCREEN_APPS_STATE.AVAILABLE);

      $('top-header-bar')
          .classList.toggle(
              'version-labels-unset',
              this.lockScreenAppsState_ == LOCK_SCREEN_APPS_STATE.FOREGROUND ||
                  this.lockScreenAppsState_ ==
                      LOCK_SCREEN_APPS_STATE.BACKGROUND);

      $('new-note-action-container').hidden =
          this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.AVAILABLE &&
          this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.FOREGROUND;

      $('new-note-action')
          .classList.toggle(
              'disabled',
              this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.AVAILABLE);

      $('new-note-action-icon').hidden =
          this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.AVAILABLE;

      // This might get set when the action is activated - reset it when the
      // lock screen action is updated.
      $('new-note-action-container')
          .classList.toggle('new-note-action-above-login-header', false);

      if (this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.FOREGROUND) {
        // Reset properties that might have been set as a result of activating
        // new note action.
        $('new-note-action-container')
            .classList.toggle('new-note-action-container-activated', false);
        $('new-note-action').style.removeProperty('border-bottom-left-radius');
        $('new-note-action').style.removeProperty('border-bottom-right-radius');
        $('new-note-action').style.removeProperty('height');
        $('new-note-action').style.removeProperty('width');
      }
    },

    /**
     * Handler for key down event.
     * @param {!KeyboardEvent} evt The key down event.
     * @private
     */
    handleNewNoteActionKeyDown_: function(evt) {
      if (evt.code != 'Enter')
        return;
      this.activateNoteAction_();
    },

    /**
     * Handles a detected touch movement event that might be considered a swipe.
     * @param {!{x: !number, y: number}} velocity The touch movement velocity
     *     vector.
     * @private
     */
    handleSwipe_: function(velocity) {
      // Ignore swipes in direction other than down left (in ltr world).
      var rtlAdjustmentForX = isRTL() ? -1 : 1;
      if ((rtlAdjustmentForX * velocity.x) > 0 || velocity.y < 0)
        return;

      // If not fast enough, ignore the swipe.
      if (diag(velocity.x, velocity.y) < MIN_SWIPE_VELOCITY)
        return;

      this.activateNoteAction_();
    },

    /**
     * @private
     */
    activateNoteAction_: function() {
      $('new-note-action').classList.toggle('disabled', true);
      $('new-note-action-icon').hidden = true;
      $('top-header-bar').classList.toggle('version-labels-unset', true);

      this.runOnNoteActionTransitionEnd_(
          (function() {
            if (this.lockScreenAppsState_ != LOCK_SCREEN_APPS_STATE.AVAILABLE)
              return;
            chrome.send(
                'setLockScreenAppsState',
                [LOCK_SCREEN_APPS_STATE.LAUNCH_REQUESTED]);
          }).bind(this));

      var container = $('new-note-action-container');
      container.classList.toggle('new-note-action-container-activated', true);
      container.classList.toggle('new-note-action-above-login-header', true);

      var newNoteAction = $('new-note-action');
      // Update new-note-action size to cover full screen - the element is a
      // circle quadrant, intent is for the whole quadrant to cover the screen
      // area, which means that the element size (radius) has to be set to the
      // container diagonal. Note that, even though the final new-note-action
      // element UI state is full-screen, the element is kept as circle quadrant
      // for purpose of transition animation (to get the effect of the element
      // radius increasing until it covers the whole screen).
      var targetSize =
          '' + diag(container.clientWidth, container.clientHeight) + 'px';
      newNoteAction.style.setProperty('width', targetSize);
      newNoteAction.style.setProperty('height', targetSize);
      newNoteAction.style.setProperty(
          isRTL() ? 'border-bottom-right-radius' : 'border-bottom-left-radius',
          targetSize);
    },

    /**
     * Waits for new note action element transition to end (the element expands
     * from top right corner to whole screen when the action is activated) and
     * then runs the provided closure.
     *
     * @param {!function()} callback Closure to run on transition end.
     */
    runOnNoteActionTransitionEnd_: function(callback) {
      $('new-note-action').addEventListener('transitionend', function listen() {
        $('new-note-action').removeEventListener('transitionend', listen);
        callback();
      });
      ensureTransitionEndEvent($('new-note-action'));
    }
  };

  return {TopHeaderBar: TopHeaderBar};
});
