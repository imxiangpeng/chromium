// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult('Tests that command line api works.\n');

  await TestRunner.loadModule('console_test_runner');
  await TestRunner.loadModule('elements_test_runner');
  await TestRunner.showPanel('console');
  await TestRunner.loadHTML(`
    <p id='foo'>
      Tests that command line api works.
    </p><p id='bar'></p>
  `);

  var expressions = [
    'String($0)',
    '$3',
    'String(keys([3,4]))',
    'String(values([3,4]))',
    `String($('#foo'))`,
    `String($('#foo', document.body))`,
    `String($('#foo', 'non-node'))`,
    `String($('#foo', $('#bar')))`,
    `String($$('p'))`,
    `String($$('p', document.body))`,
    `String($('foo'))`,
    `console.assert(keys(window).indexOf('__commandLineAPI') === -1)`
  ];

  ElementsTestRunner.selectNodeWithId('foo', step1);

  function step1(node) {
    var expression = expressions.shift();
    if (!expression) {
      step2();
      return;
    }
    Common.console.log('');
    ConsoleTestRunner.evaluateInConsole(expression, step1);
  }

  async function step2() {
    await TestRunner.evaluateInPagePromise(`
      (function assertNoBoundCommandLineAPI() {
        ['__commandLineAPI', '__scopeChainForEval'].forEach(function(name) {
          console.assert(!(name in window), 'FAIL: Should be no ' + name);
        });
      })();
    `);
    step3();
  }

  function step3() {
    ConsoleTestRunner.dumpConsoleMessages();
    TestRunner.completeTest();
  }
})();
