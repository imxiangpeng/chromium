// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import android.content.Context;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.MediumTest;
import android.support.test.filters.SmallTest;
import android.util.Log;
import android.util.Pair;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.ThreadUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.AdvancedMockContext;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.RetryOnFailure;
import org.chromium.chrome.browser.download.DownloadInfo.Builder;
import org.chromium.chrome.browser.download.DownloadManagerServiceTest.MockDownloadNotifier.MethodID;
import org.chromium.chrome.browser.test.ChromeBrowserTestRule;
import org.chromium.components.offline_items_collection.ContentId;
import org.chromium.components.offline_items_collection.OfflineItem.Progress;
import org.chromium.components.offline_items_collection.OfflineItemProgressUnit;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.net.ConnectionType;

import java.util.Collections;
import java.util.HashSet;
import java.util.Queue;
import java.util.UUID;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * Test for DownloadManagerService.
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class DownloadManagerServiceTest {
    @Rule
    public final ChromeBrowserTestRule mBrowserTestRule = new ChromeBrowserTestRule();

    private static final int UPDATE_DELAY_FOR_TEST = 1;
    private static final int DELAY_BETWEEN_CALLS = 10;
    private static final int LONG_UPDATE_DELAY_FOR_TEST = 500;

    /**
     * The MockDownloadNotifier. Currently there is no support for creating mock objects this is a
     * simple mock object that provides testing support for checking a sequence of calls.
     */
    static class MockDownloadNotifier extends SystemDownloadNotifier {
        private final Context mContext;
        private MockDownloadNotificationService mService;

        /**
         * The Ids of different methods in this mock object.
         */
        static enum MethodID {
            DOWNLOAD_SUCCESSFUL,
            DOWNLOAD_FAILED,
            DOWNLOAD_PROGRESS,
            DOWNLOAD_PAUSED,
            DOWNLOAD_INTERRUPTED,
            CANCEL_DOWNLOAD_ID,
            CLEAR_PENDING_DOWNLOADS
        }

        private final Queue<Pair<MethodID, Object>> mExpectedCalls =
                new ConcurrentLinkedQueue<Pair<MethodID, Object>>();

        public MockDownloadNotifier(Context context) {
            super(context);
            mContext = context;
            expect(MethodID.CLEAR_PENDING_DOWNLOADS, null);
        }

        /**
         * Helper method to simulate that the DownloadNotificationService is connected.
         */
        public void onServiceConnected() {
            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    mService = new MockDownloadNotificationService();
                    mService.setContext(new AdvancedMockContext(
                            mContext.getApplicationContext()));
                    mService.onCreate();
                }
            });
            setDownloadNotificationService(mService);
        }

        public MockDownloadNotifier expect(MethodID method, Object param) {
            mExpectedCalls.clear();
            mExpectedCalls.add(getMethodSignature(method, param));
            return this;
        }

        public void waitTillExpectedCallsComplete() {
            CriteriaHelper.pollInstrumentationThread(
                    new Criteria("Failed while waiting for all calls to complete.") {
                        @Override
                        public boolean isSatisfied() {
                            return mExpectedCalls.isEmpty();
                        }
                    });
        }

        public MockDownloadNotifier andThen(MethodID method, Object param) {
            mExpectedCalls.add(getMethodSignature(method, param));
            return this;
        }

        static Pair<MethodID, Object> getMethodSignature(MethodID methodId, Object param) {
            return new Pair<MethodID, Object>(methodId, param);
        }

        void assertCorrectExpectedCall(MethodID methodId, Object param, boolean matchParams) {
            Log.w("MockDownloadNotifier", "Called: " + methodId);
            Assert.assertFalse("Unexpected call:, no call expected, but got: " + methodId,
                    mExpectedCalls.isEmpty());
            Pair<MethodID, Object> actual = getMethodSignature(methodId, param);
            Pair<MethodID, Object> expected = mExpectedCalls.poll();
            Assert.assertEquals("Unexpected call", expected.first, actual.first);
            if (matchParams) {
                Assert.assertTrue(
                        "Incorrect arguments", MatchHelper.macthes(expected.second, actual.second));
            }
        }

        @Override
        public void notifyDownloadSuccessful(DownloadInfo downloadInfo,
                long systemDownloadId, boolean canResolve, boolean isSupportedMimeType) {
            assertCorrectExpectedCall(MethodID.DOWNLOAD_SUCCESSFUL, downloadInfo, false);
            Assert.assertEquals("application/unknown", downloadInfo.getMimeType());
            super.notifyDownloadSuccessful(downloadInfo, systemDownloadId, canResolve,
                    isSupportedMimeType);
        }

        @Override
        public void notifyDownloadFailed(DownloadInfo downloadInfo) {
            assertCorrectExpectedCall(MethodID.DOWNLOAD_FAILED, downloadInfo, true);

        }

        @Override
        public void notifyDownloadProgress(
                DownloadInfo downloadInfo, long startTime, boolean canDownloadWhileMetered) {
            assertCorrectExpectedCall(MethodID.DOWNLOAD_PROGRESS, downloadInfo, true);
        }

        @Override
        public void notifyDownloadPaused(DownloadInfo downloadInfo) {
            assertCorrectExpectedCall(MethodID.DOWNLOAD_PAUSED, downloadInfo, true);
        }

        @Override
        public void notifyDownloadInterrupted(DownloadInfo downloadInfo, boolean isAutoResumable) {
            assertCorrectExpectedCall(MethodID.DOWNLOAD_INTERRUPTED, downloadInfo, true);
        }

        @Override
        public void notifyDownloadCanceled(ContentId id) {
            assertCorrectExpectedCall(MethodID.CANCEL_DOWNLOAD_ID, id, true);
        }

        @Override
        public void resumePendingDownloads() {}
    }

    /**
     * Mock implementation of the DownloadSnackbarController.
     */
    static class MockDownloadSnackbarController extends DownloadSnackbarController {
        private boolean mSucceeded;
        private boolean mFailed;

        public MockDownloadSnackbarController() {
            super(null);
        }

        public void waitForSnackbarControllerToFinish(final boolean success) {
            CriteriaHelper.pollInstrumentationThread(
                    new Criteria("Failed while waiting for all calls to complete.") {
                        @Override
                        public boolean isSatisfied() {
                            return success ? mSucceeded : mFailed;
                        }
                    });
        }

        @Override
        public void onDownloadSucceeded(
                DownloadInfo downloadInfo, int notificationId, long downloadId,
                boolean canBeResolved, boolean usesAndroidDownloadManager) {
            mSucceeded = true;
        }

        @Override
        public void onDownloadFailed(String errorMessage, boolean showAllDownloads) {
            mFailed = true;
        }
    }

    /**
     * A set that each object can be matched ^only^ once. Once matched, the object
     * will be removed from the set. This is useful to write expectations
     * for a sequence of calls where order of calls is not defined. Client can
     * do the following. OneTimeMatchSet matchSet = new OneTimeMatchSet(possibleValue1,
     * possibleValue2, possibleValue3); mockObject.expect(method1, matchSet).andThen(method1,
     * matchSet).andThen(method3, matchSet); .... Some work.
     * mockObject.waitTillExpectedCallsComplete(); assertTrue(matchSet.mMatches.empty());
     */
    private static class OneTimeMatchSet {
        private final HashSet<Object> mMatches;

        OneTimeMatchSet(Object... params) {
            mMatches = new HashSet<Object>();
            Collections.addAll(mMatches, params);
        }

        public boolean matches(Object obj) {
            if (obj == null) return false;
            if (this == obj) return true;
            if (!mMatches.contains(obj)) return false;

            // Remove the object since it has been matched.
            mMatches.remove(obj);
            return true;
        }
    }

    /**
     * Class that helps matching 2 objects with either of them may be a OneTimeMatchSet object.
     */
    private static class MatchHelper {
        public static boolean macthes(Object obj1, Object obj2) {
            if (obj1 == null) return obj2 == null;
            if (obj1.equals(obj2)) return true;
            if (obj1 instanceof OneTimeMatchSet) {
                return ((OneTimeMatchSet) obj1).matches(obj2);
            } else if (obj2 instanceof OneTimeMatchSet) {
                return ((OneTimeMatchSet) obj2).matches(obj1);
            }
            return false;
        }
    }

    private static class DownloadManagerServiceForTest extends DownloadManagerService {
        boolean mResumed;
        DownloadSnackbarController mDownloadSnackbarController;

        public DownloadManagerServiceForTest(Context context, MockDownloadNotifier mockNotifier,
                long updateDelayInMillis) {
            super(context, mockNotifier, getTestHandler(), updateDelayInMillis);
        }

        @Override
        protected boolean addCompletedDownload(DownloadItem downloadItem) {
            downloadItem.setSystemDownloadId(1L);
            return true;
        }

        @Override
        protected void init() {}

        @Override
        public void resumeDownload(ContentId id, DownloadItem item, boolean hasUserGesture) {
            mResumed = true;
        }

        @Override
        protected void scheduleUpdateIfNeeded() {
            ThreadUtils.runOnUiThreadBlocking(new Runnable() {
                @Override
                public void run() {
                    DownloadManagerServiceForTest.super.scheduleUpdateIfNeeded();
                }
            });
        }

        @Override
        protected void setDownloadSnackbarController(
                DownloadSnackbarController downloadSnackbarController) {
            mDownloadSnackbarController = downloadSnackbarController;
            super.setDownloadSnackbarController(downloadSnackbarController);
        }

        @Override
        protected void onDownloadFailed(String fileName, int reason) {
            mDownloadSnackbarController.onDownloadFailed("", false);
        }
    }

    @Before
    public void setUp() throws Exception {
        RecordHistogram.setDisabledForTests(true);
    }

    @After
    public void tearDown() throws Exception {
        RecordHistogram.setDisabledForTests(false);
    }

    private static Handler getTestHandler() {
        HandlerThread handlerThread = new HandlerThread("handlerThread");
        handlerThread.start();
        return new Handler(handlerThread.getLooper());
    }

    private DownloadInfo getDownloadInfo() {
        return new Builder()
                .setBytesReceived(100)
                .setDownloadGuid(UUID.randomUUID().toString())
                .build();
    }

    private Context getTestContext() {
        return new AdvancedMockContext(
                InstrumentationRegistry.getInstrumentation().getTargetContext());
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    @RetryOnFailure
    public void testAllDownloadProgressIsCalledForSlowUpdates() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        DownloadInfo downloadInfo = getDownloadInfo();

        notifier.expect(MethodID.DOWNLOAD_PROGRESS, downloadInfo);
        dService.onDownloadUpdated(downloadInfo);
        notifier.waitTillExpectedCallsComplete();

        // Now post multiple download updated calls and make sure all are received.
        DownloadInfo update1 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(10, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();
        DownloadInfo update2 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(30, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();
        DownloadInfo update3 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(30, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, update1)
                .andThen(MethodID.DOWNLOAD_PROGRESS, update2)
                .andThen(MethodID.DOWNLOAD_PROGRESS, update3);

        dService.onDownloadUpdated(update1);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadUpdated(update2);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadUpdated(update3);
        notifier.waitTillExpectedCallsComplete();
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    public void testOnlyTwoProgressForFastUpdates() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, LONG_UPDATE_DELAY_FOR_TEST);
        DownloadInfo downloadInfo = getDownloadInfo();
        DownloadInfo update1 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(10, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();
        DownloadInfo update2 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(10, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();
        DownloadInfo update3 =
                Builder.fromDownloadInfo(downloadInfo)
                        .setProgress(new Progress(10, 100L, OfflineItemProgressUnit.PERCENTAGE))
                        .build();

        // Should get 2 update calls, the first and the last. The 2nd update will be merged into
        // the last one.
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, update1)
                .andThen(MethodID.DOWNLOAD_PROGRESS, update3);
        dService.onDownloadUpdated(update1);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadUpdated(update2);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadUpdated(update3);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        notifier.waitTillExpectedCallsComplete();
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    public void testDownloadCompletedIsCalled() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        MockDownloadSnackbarController snackbarController = new MockDownloadSnackbarController();
        final DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                DownloadManagerService.setDownloadManagerService(dService);
            }
        });
        dService.setDownloadSnackbarController(snackbarController);
        // Try calling download completed directly.
        DownloadInfo successful = getDownloadInfo();
        notifier.onServiceConnected();
        notifier.expect(MethodID.DOWNLOAD_SUCCESSFUL, successful);

        dService.onDownloadCompleted(successful);
        notifier.waitTillExpectedCallsComplete();
        snackbarController.waitForSnackbarControllerToFinish(true);

        // Now check that a successful notification appears after a download progress.
        DownloadInfo progress = getDownloadInfo();
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, progress)
                .andThen(MethodID.DOWNLOAD_SUCCESSFUL, progress);
        dService.onDownloadUpdated(progress);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadCompleted(progress);
        notifier.waitTillExpectedCallsComplete();
        snackbarController.waitForSnackbarControllerToFinish(true);
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    public void testDownloadFailedIsCalled() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        MockDownloadSnackbarController snackbarController = new MockDownloadSnackbarController();
        final DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                DownloadManagerService.setDownloadManagerService(dService);
            }
        });
        dService.setDownloadSnackbarController(snackbarController);
        // Check that if an interrupted download cannot be resumed, it will trigger a download
        // failure.
        DownloadInfo failure =
                Builder.fromDownloadInfo(getDownloadInfo()).setIsResumable(false).build();
        notifier.expect(MethodID.DOWNLOAD_FAILED, failure);
        dService.onDownloadInterrupted(failure, false);
        notifier.waitTillExpectedCallsComplete();
        snackbarController.waitForSnackbarControllerToFinish(false);
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    public void testDownloadPausedIsCalled() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        DownloadManagerService.disableNetworkListenerForTest();
        DownloadInfo interrupted =
                Builder.fromDownloadInfo(getDownloadInfo()).setIsResumable(true).build();
        notifier.expect(MethodID.DOWNLOAD_INTERRUPTED, interrupted);
        dService.onDownloadInterrupted(interrupted, true);
        notifier.waitTillExpectedCallsComplete();
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    @RetryOnFailure
    public void testMultipleDownloadProgress() {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);

        DownloadInfo download1 = getDownloadInfo();
        DownloadInfo download2 = getDownloadInfo();
        DownloadInfo download3 = getDownloadInfo();
        OneTimeMatchSet matchSet = new OneTimeMatchSet(download1, download2, download3);
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, matchSet)
                .andThen(MethodID.DOWNLOAD_PROGRESS, matchSet)
                .andThen(MethodID.DOWNLOAD_PROGRESS, matchSet);
        dService.onDownloadUpdated(download1);
        dService.onDownloadUpdated(download2);
        dService.onDownloadUpdated(download3);

        notifier.waitTillExpectedCallsComplete();
        Assert.assertTrue("All downloads should be updated.", matchSet.mMatches.isEmpty());
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    @RetryOnFailure
    public void testInterruptedDownloadAreAutoResumed() throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        final DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        DownloadManagerService.disableNetworkListenerForTest();
        DownloadInfo interrupted =
                Builder.fromDownloadInfo(getDownloadInfo()).setIsResumable(true).build();
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, interrupted)
                .andThen(MethodID.DOWNLOAD_INTERRUPTED, interrupted);
        dService.onDownloadUpdated(interrupted);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadInterrupted(interrupted, true);
        notifier.waitTillExpectedCallsComplete();
        int resumableIdCount = dService.mAutoResumableDownloadIds.size();
        dService.onConnectionTypeChanged(ConnectionType.CONNECTION_WIFI);
        Assert.assertEquals(resumableIdCount - 1, dService.mAutoResumableDownloadIds.size());
        CriteriaHelper.pollUiThread(new Criteria() {
            @Override
            public boolean isSatisfied() {
                return dService.mResumed;
            }
        });
    }

    @Test
    @MediumTest
    @Feature({"Download"})
    public void testInterruptedUnmeteredDownloadCannotAutoResumeOnMeteredNetwork()
            throws InterruptedException {
        MockDownloadNotifier notifier = new MockDownloadNotifier(getTestContext());
        final DownloadManagerServiceForTest dService = new DownloadManagerServiceForTest(
                getTestContext(), notifier, UPDATE_DELAY_FOR_TEST);
        DownloadManagerService.disableNetworkListenerForTest();
        DownloadInfo interrupted =
                Builder.fromDownloadInfo(getDownloadInfo()).setIsResumable(true).build();
        notifier.expect(MethodID.DOWNLOAD_PROGRESS, interrupted)
                .andThen(MethodID.DOWNLOAD_INTERRUPTED, interrupted);
        dService.onDownloadUpdated(interrupted);
        Thread.sleep(DELAY_BETWEEN_CALLS);
        dService.onDownloadInterrupted(interrupted, true);
        notifier.waitTillExpectedCallsComplete();
        DownloadManagerService.setIsNetworkMeteredForTest(true);
        int resumableIdCount = dService.mAutoResumableDownloadIds.size();
        dService.onConnectionTypeChanged(ConnectionType.CONNECTION_2G);
        Assert.assertEquals(resumableIdCount, dService.mAutoResumableDownloadIds.size());
    }

    /**
     * Test to make sure {@link DownloadManagerService#shouldOpenAfterDownload}
     * returns the right result for varying MIME types and Content-Dispositions.
     */
    @Test
    @SmallTest
    @Feature({"Download"})
    public void testShouldOpenAfterDownload() {
        // Should not open any download type MIME types.
        Assert.assertFalse(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setMimeType("application/download")
                        .setHasUserGesture(true)
                        .build()));
        Assert.assertFalse(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setMimeType("application/x-download")
                        .setHasUserGesture(true)
                        .build()));
        Assert.assertFalse(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setMimeType("application/octet-stream")
                        .setHasUserGesture(true)
                        .build()));

        // Should open PDFs.
        Assert.assertTrue(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setMimeType("application/pdf")
                        .setHasUserGesture(true)
                        .build()));
        Assert.assertTrue(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setContentDisposition("filename=test.pdf")
                        .setMimeType("application/pdf")
                        .setHasUserGesture(true)
                        .build()));

        // Require user gesture.
        Assert.assertFalse(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setMimeType("application/pdf")
                        .setHasUserGesture(false)
                        .build()));
        Assert.assertFalse(DownloadManagerService.shouldOpenAfterDownload(
                new DownloadInfo.Builder()
                        .setContentDisposition("filename=test.pdf")
                        .setMimeType("application/pdf")
                        .setHasUserGesture(false)
                        .build()));
    }
}
