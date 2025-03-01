// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

const fs = require('fs');
const path = require('path');

const cheerio = require('cheerio');
const mkdirp = require('mkdirp');
const recast = require('recast');
const types = recast.types;
const b = recast.types.builders;

const utils = require('../utils');

const DRY_RUN = process.env.DRY_RUN || false;
const FRONT_END_PATH = path.resolve(__dirname, '..', '..', 'front_end');
const LINE_BREAK = '$$SECRET_IDENTIFIER_FOR_LINE_BREAK$$();';

function main() {
  const files = process.argv.slice(2);
  const inputPaths = files.map(p => path.isAbsolute(p) ? p : path.resolve(process.cwd(), p));
  const identifierMap = generateTestHelperMap();
  for (const inputPath of inputPaths) {
    migrateTest(inputPath, identifierMap);
  }
  console.log(`Finished migrating ${inputPaths.length} tests`);
}

main();

function migrateTest(inputPath, identifierMap) {
  console.log('Starting to migrate: ', inputPath);
  const htmlTestFile = fs.readFileSync(inputPath, 'utf-8');
  const $ = cheerio.load(htmlTestFile);
  const javascriptFixtures = [];
  const inputCode = $('script:not([src])')
                        .toArray()
                        .map(n => n.children[0].data)
                        .map(code => processScriptCode(code, javascriptFixtures))
                        .filter(x => !!x)
                        .join('\n');
  const bodyText = $('body').text().trim();
  const helperScripts = [];
  const resourceScripts = [];
  $('script[src]').toArray().map((n) => n.attribs.src).forEach(src => {
    if (src.indexOf('resources/') !== -1) {
      resourceScripts.push(src);
      return;
    }
    const components = src.split('/');
    var filename = components[components.length - 1].split('.')[0];
    helperScripts.push(filename);
  });

  const outPath = getOutPath(inputPath);
  const srcResourcePaths = resourceScripts.map(s => path.resolve(path.dirname(inputPath), s));
  const destResourcePaths = resourceScripts.map(s => path.resolve(path.dirname(outPath), s));
  const relativeResourcePaths = destResourcePaths.map(p => p.slice(p.indexOf('/http/tests') + '/http/tests'.length));

  let outputCode;
  try {
    const testHelpers = mapTestHelpers(helperScripts);
    let domFixture = $('body')
                         .html()
                         .trim()
                         // Tries to remove it if it has it's own line
                         .replace(bodyText + '\n', '')
                         // Tries to remove it if it's inline
                         .replace(bodyText, '');
    if (/<p>\s*<\/p>/.test(domFixture)) {
      domFixture = undefined;
    }
    const onloadFunctionName = $('body')[0].attribs.onload.slice(0, -2);
    outputCode = transformTestScript(
        inputCode, bodyText, identifierMap, testHelpers, javascriptFixtures, getPanel(inputPath), domFixture,
        onloadFunctionName, relativeResourcePaths);
  } catch (err) {
    console.log('Unable to migrate: ', inputPath);
    console.log('ERROR: ', err);
    return;
  }

  console.log(outputCode);
  if (!DRY_RUN) {
    mkdirp.sync(path.dirname(outPath));

    fs.writeFileSync(outPath, outputCode);
    const expectationsPath = inputPath.replace('.html', '-expected.txt');
    copyExpectations(expectationsPath, outPath);
    copyResourceScripts(srcResourcePaths, destResourcePaths);

    fs.unlinkSync(inputPath);
    fs.unlinkSync(expectationsPath);
    console.log('Migrated to: ', outPath);
  }
}

function copyResourceScripts(srcResourcePaths, destResourcePaths) {
  destResourcePaths.forEach((p, i) => {
    mkdirp.sync(path.dirname(p));
    if (!utils.isFile(p)) {
      fs.writeFileSync(p, fs.readFileSync(srcResourcePaths[i]));
    } else {
      const originalResource = fs.readFileSync(srcResourcePaths[i]);
      const newResource = fs.readFileSync(p);
      if (originalResource !== newResource) {
        console.log('Discrepancy with resource script', p, 'for file: ', inputPath);
      }
    }
  });
}

function transformTestScript(
    inputCode, bodyText, identifierMap, explicitTestHelpers, javascriptFixtures, panel, domFixture, onloadFunctionName,
    relativeResourcePaths) {
  const ast = recast.parse(inputCode);

  /**
   * Wrap everything that's not the magical 'test' function
   * with evaluateInPagePromise
   */
  const nonTestNodes = [];
  for (const [index, node] of ast.program.body.entries()) {
    if (node.type === 'FunctionDeclaration' && node.id.name === 'test') {
      continue;
    }
    if (node.type === 'VariableDeclaration' && node.declarations[0].id.name === 'test') {
      continue;
    }
    nonTestNodes.push(node);
  }

  unwrapTestFunctionExpressionIfNecessary(ast);
  unwrapTestFunctionDeclarationIfNecessary(ast);


  /**
   * Need to track all of the namespaces used because test helper refactoring
   * requires additional fine-grained dependencies for some tests
   */
  const namespacesUsed = new Set();

  /**
   * Migrate all the call sites from InspectorTest to .*TestRunner
   */
  recast.visit(ast, {
    visitIdentifier: function(path) {
      if (path.parentPath && path.parentPath.value && path.parentPath.value.object &&
          path.parentPath.value.object.name === 'InspectorTest' && path.value.name !== 'InspectorTest') {
        const newParentIdentifier = identifierMap.get(path.value.name);
        if (!newParentIdentifier) {
          throw new Error('Could not find identifier for: ' + path.value.name);
        }
        path.parentPath.value.object.name = newParentIdentifier;
        namespacesUsed.add(newParentIdentifier.split('.')[0]);
      }
      return false;
    }
  });

  const allTestHelpers = new Set();

  for (const helper of explicitTestHelpers) {
    allTestHelpers.add(helper);
  }

  for (const namespace of namespacesUsed) {
    if (namespace === 'TestRunner')
      continue;
    const moduleName = namespace.replace(/([A-Z])/g, '_$1').replace(/^_/, '').toLowerCase();
    allTestHelpers.add(moduleName);
  }

  /**
   * Create test header based on extracted data
   */
  const headerLines = [];
  headerLines.push(createExpressionNode(`TestRunner.addResult('${bodyText}\\n');`));
  headerLines.push(createNewLineNode());
  for (const helper of allTestHelpers) {
    headerLines.push(createAwaitExpressionNode(`await TestRunner.loadModule('${helper}');`));
  }
  headerLines.push(createAwaitExpressionNode(`await TestRunner.loadPanel('${panel}');`));
  headerLines.push(createAwaitExpressionNode(`await TestRunner.showPanel('${panel}');`));

  if (domFixture) {
    headerLines.push(createAwaitExpressionNode(`await TestRunner.loadHTML(\`
${domFixture.split('\n').map(line => '    ' + line).join('\n')}
  \`)`));
  }

  if (relativeResourcePaths.length) {
    relativeResourcePaths.forEach(p => {
      headerLines.push(createAwaitExpressionNode(`await TestRunner.loadScript('${p}');`));
    });
  }

  for (const fixture of javascriptFixtures) {
    headerLines.push(fixture);
  }

  let nonTestCode = nonTestNodes.reduce((acc, node) => {
    let code = recast.print(node).code.split('\n').map(line => '    ' + line).join('\n');
    if (node.id && node.id.name === onloadFunctionName) {
      code = code.replace('        runTest();\n', '');
      code = `    (${code.trimLeft()})();`;
    };
    return acc + '\n' + code;
  }, '');

  nonTestCode = nonTestCode.startsWith('\n') && nonTestCode.slice(2);
  if (nonTestCode) {
    headerLines.push((createAwaitExpressionNode(`await TestRunner.evaluateInPagePromise(\`
  ${nonTestCode}
  \`)`)));
  }

  headerLines.push(createNewLineNode());

  ast.program.body = headerLines.concat(ast.program.body);

  /**
   * Wrap entire body in an async IIFE
   */
  const iife = b.functionExpression(null, [], b.blockStatement(ast.program.body));
  iife.async = true;
  ast.program.body = [b.expressionStatement(b.callExpression(iife, []))];

  return print(ast);
}

function copyExpectations(expectationsPath, outTestPath) {
  const outExpectationsPath = path.resolve(path.dirname(outTestPath), path.basename(expectationsPath));
  fs.writeFileSync(outExpectationsPath, fs.readFileSync(expectationsPath));
}

/**
 * If the <script></script> block doesn't contain a test function
 * assume that it needs to be serialized
 */
function processScriptCode(code, additionalHelperBlocks) {
  const ast = recast.parse(code);
  const testFunctionExpression =
      ast.program.body.find(n => n.type === 'VariableDeclaration' && n.declarations[0].id.name === 'test');
  const testFunctionDeclaration = ast.program.body.find(n => n.type === 'FunctionDeclaration' && n.id.name === 'test');
  if (testFunctionExpression || testFunctionDeclaration) {
    return code;
  }
  const formattedCode = code.trimRight().split('\n').map(line => '    ' + line).join('\n');
  additionalHelperBlocks.push(createAwaitExpressionNode(`await TestRunner.evaluateInPagePromise(\`${formattedCode}
  \`)`));
  return;
}

/**
 * Unwrap test if it's a function expression
 * var test = function () {...}
 */
function unwrapTestFunctionExpressionIfNecessary(ast) {
  const index =
      ast.program.body.findIndex(n => n.type === 'VariableDeclaration' && n.declarations[0].id.name === 'test');
  if (index > -1) {
    const testFunctionNode = ast.program.body[index];
    ast.program.body = testFunctionNode.declarations[0].init.body.body;
  }
}


/**
 * Unwrap test if it's a function declaration
 * function test () {...}
 */
function unwrapTestFunctionDeclarationIfNecessary(ast) {
  const index = ast.program.body.findIndex(n => n.type === 'FunctionDeclaration' && n.id.name === 'test');
  if (index > -1) {
    const testFunctionNode = ast.program.body[index];
    ast.program.body.splice(index, 1);
    ast.program.body = testFunctionNode.body.body;
  }
}

function print(ast) {
  const copyrightNotice = `// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

`;

  /**
   * Not using clang-format because certain tests look bad when formatted by it.
   * Recast pretty print is smarter about preserving existing spacing.
   */
  let code = recast.prettyPrint(ast, {tabWidth: 2, wrapColumn: 120, quote: 'single'}).code;
  code = code.replace(/(\/\/\#\s*sourceURL=[\w-]+)\.html/, '$1.js');
  code = code.replace(/\s*\$\$SECRET_IDENTIFIER_FOR_LINE_BREAK\$\$\(\);/g, '\n');
  const copyrightedCode = copyrightNotice + code + '\n';
  return copyrightedCode;
}


function getOutPath(inputPath) {
  const nonHttpLayoutTestPrefix = 'LayoutTests/inspector';
  const httpLayoutTestPrefix = 'LayoutTests/http/tests/inspector';
  const postfix = inputPath.indexOf(nonHttpLayoutTestPrefix) === -1 ?
      inputPath.slice(inputPath.indexOf(httpLayoutTestPrefix) + httpLayoutTestPrefix.length + 1)
          .replace('.html', '.js') :
      inputPath.slice(inputPath.indexOf(nonHttpLayoutTestPrefix) + nonHttpLayoutTestPrefix.length + 1)
          .replace('.html', '.js');
  const out = path.resolve(__dirname, '..', '..', '..', '..', 'LayoutTests', 'http', 'tests', 'devtools', postfix);
  return out;
}

function getPanel(inputPath) {
  const panelByFolder = {
    'animation': 'elements',
    'audits': 'audits',
    'console': 'console',
    'elements': 'elements',
    'editor': 'sources',
    'layers': 'layers',
    'network': 'network',
    'profiler': 'heap_profiler',
    'resource-tree': 'resources',
    'search': 'sources',
    'security': 'security',
    'service-workers': 'resources',
    'sources': 'sources',
    'timeline': 'timeline',
    'tracing': 'timeline',
  };

  const components = inputPath.slice(inputPath.indexOf('LayoutTests/')).split('/');
  const folder = inputPath.indexOf('LayoutTests/inspector') === -1 ? components[4] : components[2];
  const panel = panelByFolder[folder];
  if (!panel) {
    throw new Error('Could not figure out which panel to map folder: ' + folder);
  }
  return panel;
}

function mapTestHelpers(testHelpers) {
  const SKIP = 'SKIP';
  const testHelperMap = {
    'inspector-test': SKIP,
    'console-test': 'console_test_runner',
    'elements-test': 'elements_test_runner',
    'sources-test': 'sources_test_runner',
  };
  const mappedHelpers = [];
  for (const helper of testHelpers) {
    const mappedHelper = testHelperMap[helper];
    if (!mappedHelper) {
      throw Error('Could not map helper ' + helper);
    }
    if (mappedHelper !== SKIP) {
      mappedHelpers.push(mappedHelper);
    }
  }
  return mappedHelpers;
}

function generateTestHelperMap() {
  const map = new Map();
  for (const folder of fs.readdirSync(FRONT_END_PATH)) {
    if (folder.indexOf('test_runner') === -1) {
      continue;
    }
    const testRunnerModulePath = path.resolve(FRONT_END_PATH, folder);
    if (!utils.isDir(testRunnerModulePath)) {
      continue;
    }
    for (const filename of fs.readdirSync(testRunnerModulePath)) {
      if (filename === 'module.json') {
        continue;
      }
      scrapeTestHelperIdentifiers(path.resolve(testRunnerModulePath, filename));
    }
  }

  // Manual overrides
  map.set('consoleModel', 'ConsoleModel');
  map.set('networkLog', 'NetworkLog');

  return map;

  function scrapeTestHelperIdentifiers(filePath) {
    var content = fs.readFileSync(filePath).toString();
    var lines = content.split('\n');
    for (var line of lines) {
      var line = line.trim();
      if (line.indexOf('TestRunner.') === -1)
        continue;
      var match = line.match(/^\s*(\b\w*TestRunner.[a-z_A-Z0-9]+)\s*(\=[^,}]|[;])/) ||
          line.match(/^(TestRunner.[a-z_A-Z0-9]+)\s*\=$/);
      if (!match)
        continue;
      var name = match[1];
      var components = name.split('.');
      if (components.length !== 2)
        continue;
      map.set(components[1], components[0]);
    }
  }
}

/**
 * Hack to quickly create an AST node
 */
function createExpressionNode(code) {
  return recast.parse(code).program.body[0];
}

/**
 * Hack to quickly create an AST node
 */
function createAwaitExpressionNode(code) {
  return recast.parse(`(async function(){${code}})`).program.body[0].expression.body.body[0];
}

function createNewLineNode() {
  return createExpressionNode(LINE_BREAK);
}