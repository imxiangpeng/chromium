// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Common utilities for extension ui tests. */
cr.define('extension_test_util', function() {
  /**
   * A mock to test that clicking on an element calls a specific method.
   * @constructor
   */
  function ClickMock() {}

  ClickMock.prototype = {
    /**
     * Tests clicking on an element and expecting a call.
     * @param {HTMLElement} element The element to click on.
     * @param {string} callName The function expected to be called.
     * @param {Array<*>=} opt_expectedArgs The arguments the function is
     *     expected to be called with.
     */
    testClickingCalls: function(element, callName, opt_expectedArgs) {
      var mock = new MockController();
      var mockMethod = mock.createFunctionMock(this, callName);
      MockMethod.prototype.addExpectation.apply(
          mockMethod, opt_expectedArgs);
      MockInteractions.tap(element);
      mock.verifyMocks();
    },
  };

  /**
   * A mock to test receiving expected events and verify that they were called
   * with the proper detail values.
   */
  function ListenerMock() {
    this.listeners_ = {};
  }

  ListenerMock.prototype = {
    /** @private {Object<{satisfied: boolean, args: !Object}>} */
    listeners_: undefined,

    /**
     * @param {string} eventName
     * @param {Event} e
     */
    onEvent_: function(eventName, e) {
      assert(this.listeners_.hasOwnProperty(eventName));
      if (this.listeners_[eventName].satisfied) {
        // Event was already called and checked. We could always make this
        // more intelligent by allowing for subsequent calls, removing the
        // listener, etc, but there's no need right now.
        return;
      }
      var expected = this.listeners_[eventName].args || {};
      expectDeepEquals(e.detail, expected);
      this.listeners_[eventName].satisfied = true;
    },

    /**
     * Adds an expected event.
     * @param {!EventTarget} target
     * @param {string} eventName
     * @param {Object=} opt_eventArgs If omitted, will check that the details
     *     are empty (i.e., {}).
     */
    addListener: function(target, eventName, opt_eventArgs) {
      assert(!this.listeners_.hasOwnProperty(eventName));
      this.listeners_[eventName] =
          {args: opt_eventArgs || {}, satisfied: false};
      target.addEventListener(eventName, this.onEvent_.bind(this, eventName));
    },

    /** Verifies the expectations set. */
    verify: function() {
      var missingEvents = [];
      for (var key in this.listeners_) {
        if (!this.listeners_[key].satisfied)
          missingEvents.push(key);
      }
      expectEquals(0, missingEvents.length, JSON.stringify(missingEvents));
    },
  }

  /**
   * A mock delegate for the item, capable of testing functionality.
   * @constructor
   * @extends {ClickMock}
   * @implements {extensions.ItemDelegate}
   */
  function MockItemDelegate() {}

  MockItemDelegate.prototype = {
    __proto__: ClickMock.prototype,

    /** @override */
    deleteItem: function(id) {},

    /** @override */
    setItemEnabled: function(id, enabled) {},

    /** @override */
    showItemDetails: function(id) {},

    /** @override */
    setItemAllowedIncognito: function(id, enabled) {},

    /** @override */
    setItemAllowedOnFileUrls: function(id, enabled) {},

    /** @override */
    setItemAllowedOnAllSites: function(id, enabled) {},

    /** @override */
    setItemCollectsErrors: function(id, enabled) {},

    /** @override */
    inspectItemView: function(id, view) {},

    /** @override */
    repairItem: function(id) {},

    /** @override */
    showItemOptionsPage: function(id) {},
  };

  /**
   * Returns whether or not the element specified is visible.
   * @param {!HTMLElement} parentEl
   * @param {string} selector
   * @param {boolean=} checkLightDom
   * @return {boolean}
   */
  function isVisible(parentEl, selector, checkLightDom) {
    var element = (checkLightDom ? parentEl.querySelector : parentEl.$$).call(
                      parentEl, selector);
    var rect = element ? element.getBoundingClientRect() : null;
    return !!rect && rect.width * rect.height > 0;
  }

  /**
   * Tests that the element's visibility matches |expectedVisible| and,
   * optionally, has specific content if it is visible.
   * @param {!HTMLElement} parentEl The parent element to query for the element.
   * @param {string} selector The selector to find the element.
   * @param {boolean} expectedVisible Whether the element should be
   *     visible.
   * @param {string=} opt_expectedText The expected textContent value.
   */
  function testVisible(parentEl, selector, expectedVisible, opt_expectedText) {
    var visible = isVisible(parentEl, selector);
    expectEquals(expectedVisible, visible, selector);
    if (expectedVisible && visible && opt_expectedText) {
      var element = parentEl.$$(selector);
      expectEquals(opt_expectedText, element.textContent.trim(), selector);
    }
  }

  /**
   * Creates an ExtensionInfo object.
   * @param {Object=} opt_properties A set of properties that will be used on
   *     the resulting ExtensionInfo (otherwise defaults will be used).
   * @return {chrome.developerPrivate.ExtensionInfo}
   */
  function createExtensionInfo(opt_properties) {
    var id = opt_properties && opt_properties.hasOwnProperty('id') ?
        opt_properties[id] : 'a'.repeat(32);
    var baseUrl = 'chrome-extension://' + id + '/';
    return Object.assign(
        {
          commands: [],
          dependentExtensions: [],
          description: 'This is an extension',
          disableReasons: {
            suspiciousInstall: false,
            corruptInstall: false,
            updateRequired: false,
          },
          homePage: {specified: false, url: ''},
          iconUrl: 'chrome://extension-icon/' + id + '/24/0',
          id: id,
          incognitoAccess: {isEnabled: true, isActive: false},
          location: 'FROM_STORE',
          manifestErrors: [],
          name: 'Wonderful Extension',
          runtimeErrors: [],
          permissions: [],
          state: 'ENABLED',
          type: 'EXTENSION',
          userMayModify: true,
          version: '2.0',
          views: [{url: baseUrl + 'foo.html'}, {url: baseUrl + 'bar.html'}],
        },
        opt_properties);
  }

  /**
   * Tests that any iron-icon child of an HTML element has a corresponding
   * non-empty svg element.
   * @param {HTMLElement} e The element to check the iron icons in.
   */
  function testIronIcons(e) {
    e.querySelectorAll('* /deep/ iron-icon').forEach(function(icon) {
      var svg = icon.$$('svg');
      expectTrue(!!svg && svg.innerHTML != '',
                 'icon "' + icon.icon + '" is not present');
    });
  }

  return {
    ClickMock: ClickMock,
    ListenerMock: ListenerMock,
    MockItemDelegate: MockItemDelegate,
    isVisible: isVisible,
    testVisible: testVisible,
    createExtensionInfo: createExtensionInfo,
    testIronIcons: testIronIcons,
  };
});
