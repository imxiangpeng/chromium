# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json

from webkitpy.common.checkout.git_mock import MockGit
from webkitpy.common.host_mock import MockHost
from webkitpy.common.net.buildbot import Build
from webkitpy.common.net.git_cl import TryJobStatus
from webkitpy.common.net.git_cl_mock import MockGitCL
from webkitpy.common.system.executive_mock import MockCall
from webkitpy.common.system.executive_mock import MockExecutive
from webkitpy.common.system.log_testing import LoggingTestCase
from webkitpy.w3c.chromium_commit_mock import MockChromiumCommit
from webkitpy.w3c.local_wpt import LocalWPT
from webkitpy.w3c.test_importer import TestImporter
from webkitpy.w3c.wpt_github_mock import MockWPTGitHub


class TestImporterTest(LoggingTestCase):

    def test_update_expectations_for_cl_no_results(self):
        host = MockHost()
        host.filesystem.write_text_file(
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        importer = TestImporter(host)
        importer.git_cl = MockGitCL(host, results={})
        success = importer.update_expectations_for_cl()
        self.assertFalse(success)
        self.assertLog([
            'INFO: Triggering try jobs for updating expectations.\n',
            'ERROR: No initial try job results, aborting.\n',
        ])
        self.assertEqual(importer.git_cl.calls[-1], ['git', 'cl', 'set-close'])

    def test_update_expectations_for_cl_all_jobs_pass(self):
        host = MockHost()
        host.filesystem.write_text_file(
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        importer = TestImporter(host)
        importer.git_cl = MockGitCL(host, results={
            Build('builder-a', 123): TryJobStatus('COMPLETED', 'SUCCESS'),
        })
        success = importer.update_expectations_for_cl()
        self.assertTrue(success)

    def test_update_expectations_for_cl_fail_but_no_changes(self):
        host = MockHost()
        host.filesystem.write_text_file(
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        importer = TestImporter(host)
        importer.git_cl = MockGitCL(host, results={
            Build('builder-a', 123): TryJobStatus('COMPLETED', 'FAILURE'),
        })
        importer.fetch_new_expectations_and_baselines = lambda: None
        success = importer.update_expectations_for_cl()
        self.assertTrue(success)
        self.assertLog([
            'INFO: Triggering try jobs for updating expectations.\n',
        ])

    def test_run_commit_queue_for_cl_pass(self):
        host = MockHost()
        host.filesystem.write_text_file(
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        importer = TestImporter(host)
        # Only the latest job for each builder is counted.
        importer.git_cl = MockGitCL(host, results={
            Build('builder-a', 120): TryJobStatus('COMPLETED', 'FAILURE'),
            Build('builder-a', 123): TryJobStatus('COMPLETED', 'SUCCESS'),
        })
        success = importer.run_commit_queue_for_cl()
        self.assertTrue(success)
        self.assertLog([
            'INFO: Triggering CQ try jobs.\n',
            'INFO: CQ appears to have passed; trying to commit.\n',
            'INFO: Update completed.\n',
        ])
        self.assertEqual(importer.git_cl.calls, [
            ['git', 'cl', 'try'],
            ['git', 'cl', 'upload', '-f', '--send-mail'],
            ['git', 'cl', 'set-commit'],
        ])

    def test_run_commit_queue_for_cl_fails(self):
        host = MockHost()
        host.filesystem.write_text_file(
            '/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        importer = TestImporter(host)
        importer.git_cl = MockGitCL(host, results={
            Build('builder-a', 120): TryJobStatus('COMPLETED', 'SUCCESS'),
            Build('builder-a', 123): TryJobStatus('COMPLETED', 'FAILURE'),
            Build('builder-b', 200): TryJobStatus('COMPLETED', 'SUCCESS'),
        })
        importer.fetch_new_expectations_and_baselines = lambda: None
        success = importer.run_commit_queue_for_cl()
        self.assertFalse(success)
        self.assertLog([
            'INFO: Triggering CQ try jobs.\n',
            'ERROR: CQ appears to have failed; aborting.\n',
        ])
        self.assertEqual(importer.git_cl.calls, [
            ['git', 'cl', 'try'],
            ['git', 'cl', 'set-close'],
        ])

    def test_apply_exportable_commits_locally(self):
        host = MockHost()
        importer = TestImporter(host, wpt_github=MockWPTGitHub(pull_requests=[]))
        fake_commit = MockChromiumCommit(
            host, subject='My fake commit',
            patch=(
                'Fake patch contents...\n'
                '--- a/third_party/WebKit/LayoutTests/external/wpt/css/css-ui-3/outline-004.html\n'
                '+++ b/third_party/WebKit/LayoutTests/external/wpt/css/css-ui-3/outline-004.html\n'
                '@@ -20,7 +20,7 @@\n'
                '...'))
        importer.exportable_but_not_exported_commits = lambda _: [fake_commit]
        applied = importer.apply_exportable_commits_locally(LocalWPT(host))
        self.assertEqual(applied, [fake_commit])
        self.assertEqual(host.executive.full_calls, [
            MockCall(
                ['git', 'apply', '-'],
                {
                    'input': (
                        'Fake patch contents...\n'
                        '--- a/css/css-ui-3/outline-004.html\n'
                        '+++ b/css/css-ui-3/outline-004.html\n'
                        '@@ -20,7 +20,7 @@\n'
                        '...'),
                    'cwd': '/tmp/wpt',
                    'env': None
                }),
            MockCall(
                ['git', 'add', '.'],
                kwargs={'input': None, 'cwd': '/tmp/wpt', 'env': None}),
            MockCall(
                ['git', 'commit', '--all', '-F', '-'],
                kwargs={'cwd': '/tmp/wpt', 'env': None})
        ])

    def test_apply_exportable_commits_locally_returns_none_on_failure(self):
        host = MockHost()
        wpt_github = MockWPTGitHub(pull_requests=[])
        importer = TestImporter(host, wpt_github=wpt_github)
        commit = MockChromiumCommit(host, subject='My fake commit')
        importer.exportable_but_not_exported_commits = lambda _: [commit]
        local_wpt = LocalWPT(host)
        local_wpt.apply_patch = lambda _: None  # Failure to apply patch.
        applied = importer.apply_exportable_commits_locally(local_wpt)
        self.assertIsNone(applied)

    def test_update_all_test_expectations_files(self):
        host = MockHost()
        host.filesystem.files['/mock-checkout/third_party/WebKit/LayoutTests/TestExpectations'] = (
            'Bug(test) some/test/a.html [ Failure ]\n'
            'Bug(test) some/test/b.html [ Failure ]\n'
            'Bug(test) some/test/c.html [ Failure ]\n')
        host.filesystem.files['/mock-checkout/third_party/WebKit/LayoutTests/VirtualTestSuites'] = '[]'
        host.filesystem.files['/mock-checkout/third_party/WebKit/LayoutTests/new/a.html'] = ''
        host.filesystem.files['/mock-checkout/third_party/WebKit/LayoutTests/new/b.html'] = ''
        importer = TestImporter(host)
        deleted_tests = ['some/test/b.html']
        renamed_test_pairs = {
            'some/test/a.html': 'new/a.html',
            'some/test/c.html': 'new/c.html',
        }
        importer.update_all_test_expectations_files(deleted_tests, renamed_test_pairs)
        self.assertMultiLineEqual(
            host.filesystem.read_text_file('/mock-checkout/third_party/WebKit/LayoutTests/TestExpectations'),
            ('Bug(test) new/a.html [ Failure ]\n'
             'Bug(test) new/c.html [ Failure ]\n'))

    def test_get_directory_owners(self):
        host = MockHost()
        host.filesystem.write_text_file('/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        host.filesystem.write_text_file('/mock-checkout/third_party/WebKit/LayoutTests/external/wpt/foo/OWNERS',
                                        'someone@chromium.org\n')
        git = MockGit(filesystem=host.filesystem, executive=host.executive, platform=host.platform)
        git.changed_files = lambda: ['third_party/WebKit/LayoutTests/external/wpt/foo/x.html']
        host.git = lambda: git
        importer = TestImporter(host)
        self.assertEqual(importer.get_directory_owners(), {('someone@chromium.org',): ['external/wpt/foo']})

    def test_get_directory_owners_no_changed_files(self):
        host = MockHost()
        host.filesystem.write_text_file('/mock-checkout/third_party/WebKit/LayoutTests/W3CImportExpectations', '')
        host.filesystem.write_text_file('/mock-checkout/third_party/WebKit/LayoutTests/external/wpt/foo/OWNERS',
                                        'someone@chromium.org\n')
        importer = TestImporter(host)
        self.assertEqual(importer.get_directory_owners(), {})

    # Tests for protected methods - pylint: disable=protected-access

    def test_commit_changes(self):
        host = MockHost()
        importer = TestImporter(host)
        importer._has_changes = lambda: True
        importer._commit_changes('dummy message')
        self.assertEqual(
            host.executive.calls,
            [['git', 'commit', '--all', '-F', '-']])

    def test_commit_message(self):
        importer = TestImporter(MockHost())
        self.assertEqual(
            importer._commit_message('aaaa', '1111'),
            'Import 1111\n\n'
            'Using wpt-import in Chromium aaaa.\n\n'
            'No-Export: true')

    def test_cl_description_with_empty_environ(self):
        host = MockHost()
        host.executive = MockExecutive(output='Last commit message\n\n')
        importer = TestImporter(host)
        description = importer._cl_description(directory_owners={})
        self.assertEqual(
            description,
            'Last commit message\n\n'
            'Note to sheriffs: This CL imports external tests and adds\n'
            'expectations for those tests; if this CL is large and causes\n'
            'a few new failures, please fix the failures by adding new\n'
            'lines to TestExpectations rather than reverting. See:\n'
            'https://chromium.googlesource.com'
            '/chromium/src/+/master/docs/testing/web_platform_tests.md\n\n'
            'No-Export: true')
        self.assertEqual(host.executive.calls, [['git', 'log', '-1', '--format=%B']])

    def test_cl_description_with_environ_variables(self):
        host = MockHost()
        host.executive = MockExecutive(output='Last commit message\n')
        importer = TestImporter(host)
        importer.host.environ['BUILDBOT_MASTERNAME'] = 'my.master'
        importer.host.environ['BUILDBOT_BUILDERNAME'] = 'b'
        importer.host.environ['BUILDBOT_BUILDNUMBER'] = '123'
        description = importer._cl_description(directory_owners={})
        self.assertIn(
            'Build: https://build.chromium.org/p/my.master/builders/b/builds/123\n\n',
            description)
        self.assertEqual(host.executive.calls, [['git', 'log', '-1', '--format=%B']])

    def test_cl_description_moves_noexport_tag(self):
        host = MockHost()
        host.executive = MockExecutive(output='Summary\n\nNo-Export: true\n\n')
        importer = TestImporter(host)
        description = importer._cl_description(directory_owners={})
        self.assertIn(
            'No-Export: true',
            description)

    def test_cl_description_with_directory_owners(self):
        host = MockHost()
        host.executive = MockExecutive(output='Last commit message\n\n')
        importer = TestImporter(host)
        description = importer._cl_description(directory_owners={
            ('someone@chromium.org',): ['external/wpt/foo', 'external/wpt/bar'],
            ('x@chromium.org', 'y@chromium.org'): ['external/wpt/baz'],
        })
        self.assertIn(
            'Directory owners for changes in this CL:\n'
            'someone@chromium.org:\n'
            '  external/wpt/foo\n'
            '  external/wpt/bar\n'
            'x@chromium.org, y@chromium.org:\n'
            '  external/wpt/baz\n\n',
            description)

    def test_cc_part(self):
        directory_owners = {
            ('someone@chromium.org',): ['external/wpt/foo', 'external/wpt/bar'],
            ('x@chromium.org', 'y@chromium.org'): ['external/wpt/baz'],
        }
        self.assertEqual(
            TestImporter._cc_part(directory_owners),
            ['--cc=someone@chromium.org', '--cc=x@chromium.org', '--cc=y@chromium.org'])

    def test_generate_manifest_successful_run(self):
        # This test doesn't test any aspect of the real manifest script, it just
        # asserts that TestImporter._generate_manifest would invoke the script.
        host = MockHost()
        importer = TestImporter(host)
        blink_path = '/mock-checkout/third_party/WebKit'
        host.filesystem.write_text_file(blink_path + '/LayoutTests/external/wpt/MANIFEST.json', '{}')
        importer._generate_manifest()
        self.assertEqual(
            host.executive.calls,
            [
                [
                    'python',
                    blink_path + '/Tools/Scripts/webkitpy/thirdparty/wpt/wpt/manifest',
                    '--work',
                    '--tests-root',
                    blink_path + '/LayoutTests/external/wpt',
                ],
                [
                    'git',
                    'add',
                    blink_path + '/LayoutTests/external/WPT_BASE_MANIFEST.json',
                ]
            ])

    def test_delete_orphaned_baselines_basic(self):
        host = MockHost()
        importer = TestImporter(host)
        dest_path = importer.dest_path
        host.filesystem.write_text_file(
            dest_path + '/MANIFEST.json',
            json.dumps({
                'items': {
                    'testharness': {
                        'a.html': [['/a.html', {}]],
                    },
                    'manual': {},
                    'reftest': {},
                },
            }))
        host.filesystem.write_text_file(dest_path + '/a.html', '')
        host.filesystem.write_text_file(dest_path + '/a-expected.txt', '')
        host.filesystem.write_text_file(dest_path + '/orphaned-expected.txt', '')
        importer._delete_orphaned_baselines()
        self.assertFalse(host.filesystem.exists(dest_path + '/orphaned-expected.txt'))
        self.assertTrue(host.filesystem.exists(dest_path + '/a-expected.txt'))

    def test_delete_orphaned_baselines_worker_js_tests(self):
        # This test checks that baselines for existing tests shouldn't be
        # deleted, even if the test name isn't the same as the file name.
        host = MockHost()
        importer = TestImporter(host)
        dest_path = importer.dest_path
        host.filesystem.write_text_file(
            dest_path + '/MANIFEST.json',
            json.dumps({
                'items': {
                    'testharness': {
                        'a.any.js': [
                            ['/a.any.html', {}],
                            ['/a.any.worker.html', {}],
                        ],
                        'b.worker.js': [['/b.worker.html', {}]],
                        'c.html': [
                            ['/c.html?q=1', {}],
                            ['/c.html?q=2', {}],
                        ],
                    },
                    'manual': {},
                    'reftest': {},
                },
            }))
        host.filesystem.write_text_file(dest_path + '/a.any.js', '')
        host.filesystem.write_text_file(dest_path + '/a.any-expected.txt', '')
        host.filesystem.write_text_file(dest_path + '/a.any.worker-expected.txt', '')
        host.filesystem.write_text_file(dest_path + '/b.worker.js', '')
        host.filesystem.write_text_file(dest_path + '/b.worker-expected.txt', '')
        host.filesystem.write_text_file(dest_path + '/c.html', '')
        host.filesystem.write_text_file(dest_path + '/c-expected.txt', '')
        importer._delete_orphaned_baselines()
        self.assertTrue(host.filesystem.exists(dest_path + '/a.any-expected.txt'))
        self.assertTrue(host.filesystem.exists(dest_path + '/a.any.worker-expected.txt'))
        self.assertTrue(host.filesystem.exists(dest_path + '/b.worker-expected.txt'))
        self.assertTrue(host.filesystem.exists(dest_path + '/c-expected.txt'))

    def test_clear_out_dest_path(self):
        host = MockHost()
        importer = TestImporter(host)
        dest_path = importer.dest_path
        host.filesystem.write_text_file(dest_path + '/foo-test.html', '')
        host.filesystem.write_text_file(dest_path + '/foo-test-expected.txt', '')
        host.filesystem.write_text_file(dest_path + '/OWNERS', '')
        host.filesystem.write_text_file(dest_path + '/bar/baz/OWNERS', '')
        # When the destination path is cleared, OWNERS files and baselines
        # are kept.
        importer._clear_out_dest_path()
        self.assertFalse(host.filesystem.exists(dest_path + '/foo-test.html'))
        self.assertTrue(host.filesystem.exists(dest_path + '/foo-test-expected.txt'))
        self.assertTrue(host.filesystem.exists(dest_path + '/OWNERS'))
        self.assertTrue(host.filesystem.exists(dest_path + '/bar/baz/OWNERS'))
