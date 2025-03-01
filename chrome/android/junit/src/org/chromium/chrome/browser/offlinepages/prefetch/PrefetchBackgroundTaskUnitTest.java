// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.offlinepages.prefetch;

import android.content.Context;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.mockito.Spy;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.internal.ShadowExtractor;
import org.robolectric.shadows.multidex.ShadowMultiDex;

import org.chromium.base.library_loader.ProcessInitException;
import org.chromium.chrome.browser.init.BrowserParts;
import org.chromium.chrome.browser.init.ChromeBrowserInitializer;
import org.chromium.components.background_task_scheduler.BackgroundTask.TaskFinishedCallback;
import org.chromium.components.background_task_scheduler.BackgroundTaskScheduler;
import org.chromium.components.background_task_scheduler.BackgroundTaskSchedulerFactory;
import org.chromium.components.background_task_scheduler.TaskIds;
import org.chromium.components.background_task_scheduler.TaskInfo;
import org.chromium.components.background_task_scheduler.TaskParameters;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.concurrent.TimeUnit;

/** Unit tests for {@link PrefetchBackgroundTask}. */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE,
        shadows = {PrefetchBackgroundTaskUnitTest.ShadowBackgroundTaskScheduler.class,
                ShadowMultiDex.class})
public class PrefetchBackgroundTaskUnitTest {
    /**
     * Shadow of BackgroundTaskScheduler system service.
     */
    @Implements(BackgroundTaskScheduler.class)
    public static class ShadowBackgroundTaskScheduler {
        private HashMap<Integer, TaskInfo> mTaskInfos = new HashMap<>();

        @Implementation
        public boolean schedule(Context context, TaskInfo taskInfo) {
            mTaskInfos.put(taskInfo.getTaskId(), taskInfo);
            return true;
        }

        @Implementation
        public void cancel(Context context, int taskId) {
            mTaskInfos.remove(taskId);
        }

        public TaskInfo getTaskInfo(int taskId) {
            return mTaskInfos.get(taskId);
        }

        public void clear() {
            mTaskInfos = new HashMap<>();
        }
    }

    @Spy
    private PrefetchBackgroundTask mPrefetchBackgroundTask = new PrefetchBackgroundTask();
    @Mock
    private ChromeBrowserInitializer mChromeBrowserInitializer;
    @Captor
    ArgumentCaptor<BrowserParts> mBrowserParts;
    private ShadowBackgroundTaskScheduler mShadowTaskScheduler;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        doNothing().when(mChromeBrowserInitializer).handlePreNativeStartup(any(BrowserParts.class));
        try {
            doAnswer(new Answer<Void>() {
                @Override
                public Void answer(InvocationOnMock invocation) {
                    mBrowserParts.getValue().finishNativeInitialization();
                    return null;
                }
            })
                    .when(mChromeBrowserInitializer)
                    .handlePostNativeStartup(eq(true), mBrowserParts.capture());
        } catch (ProcessInitException ex) {
            fail("Unexpected exception while initializing mock of ChromeBrowserInitializer.");
        }

        ChromeBrowserInitializer.setForTesting(mChromeBrowserInitializer);

        doAnswer(new Answer() {
            public Object answer(InvocationOnMock invocation) {
                mPrefetchBackgroundTask.setNativeTask(1);
                return Boolean.TRUE;
            }
        })
                .when(mPrefetchBackgroundTask)
                .nativeStartPrefetchTask(any());
        doReturn(true).when(mPrefetchBackgroundTask).nativeOnStopTask(1);
        doReturn(null).when(mPrefetchBackgroundTask).getProfile();

        mShadowTaskScheduler = (ShadowBackgroundTaskScheduler) ShadowExtractor.extract(
                BackgroundTaskSchedulerFactory.getScheduler());
    }

    @After
    public void tearDown() {
        mShadowTaskScheduler.clear();
    }

    @Test
    public void scheduleTask() {
        final int additionalDelaySeconds = 15;
        PrefetchBackgroundTask.scheduleTask(additionalDelaySeconds, true);
        TaskInfo scheduledTask =
                mShadowTaskScheduler.getTaskInfo(TaskIds.OFFLINE_PAGES_PREFETCH_JOB_ID);
        assertNotNull(scheduledTask);
        assertEquals(TimeUnit.SECONDS.toMillis(PrefetchBackgroundTask.DEFAULT_START_DELAY_SECONDS
                             + additionalDelaySeconds),
                scheduledTask.getOneOffInfo().getWindowStartTimeMs());
        assertEquals(true, scheduledTask.isPersisted());
        assertEquals(TaskInfo.NETWORK_TYPE_UNMETERED, scheduledTask.getRequiredNetworkType());
    }

    @Test
    public void cancelTask() {
        TaskInfo scheduledTask =
                mShadowTaskScheduler.getTaskInfo(TaskIds.OFFLINE_PAGES_PREFETCH_JOB_ID);
        assertNull(scheduledTask);

        PrefetchBackgroundTask.scheduleTask(0, true);
        scheduledTask = mShadowTaskScheduler.getTaskInfo(TaskIds.OFFLINE_PAGES_PREFETCH_JOB_ID);
        assertNotNull(scheduledTask);
        assertEquals(TimeUnit.SECONDS.toMillis(PrefetchBackgroundTask.DEFAULT_START_DELAY_SECONDS),
                scheduledTask.getOneOffInfo().getWindowStartTimeMs());

        PrefetchBackgroundTask.cancelTask();
        scheduledTask = mShadowTaskScheduler.getTaskInfo(TaskIds.OFFLINE_PAGES_PREFETCH_JOB_ID);
        assertNull(scheduledTask);
    }

    @Test
    public void createNativeTask() {
        final ArrayList<Boolean> reschedules = new ArrayList<>();
        TaskParameters params = mock(TaskParameters.class);
        when(params.getTaskId()).thenReturn(TaskIds.OFFLINE_PAGES_PREFETCH_JOB_ID);

        mPrefetchBackgroundTask.onStartTask(null, params, new TaskFinishedCallback() {
            @Override
            public void taskFinished(boolean needsReschedule) {
                reschedules.add(needsReschedule);
            }
        });
        mPrefetchBackgroundTask.doneProcessing(false);

        assertEquals(1, reschedules.size());
        assertEquals(false, reschedules.get(0));
    }
}
