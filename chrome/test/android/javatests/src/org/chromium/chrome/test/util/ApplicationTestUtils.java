// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.util;

import android.annotation.TargetApi;
import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.PowerManager;

import junit.framework.Assert;
import junit.framework.AssertionFailedError;

import org.chromium.base.ActivityState;
import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CallbackHelper;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.omaha.OmahaBase;
import org.chromium.chrome.browser.omaha.VersionNumberGetter;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;

import java.util.concurrent.Callable;

/**
 * Methods used for testing Chrome at the Application-level.
 */
public class ApplicationTestUtils {
    private static final String TAG = "ApplicationTestUtils";
    private static final float FLOAT_EPSILON = 0.001f;

    private static PowerManager.WakeLock sWakeLock;

    // TODO(jbudorick): fix deprecation warning crbug.com/537347
    @SuppressWarnings("deprecation")
    public static void setUp(Context context, boolean clearAppData) {
        if (clearAppData) {
            // Clear data and remove any tasks listed in Android's Overview menu between test runs.
            clearAppData(context);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                finishAllChromeTasks(context);
            }
        }

        // Make sure the screen is on during test runs.
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        sWakeLock = pm.newWakeLock(PowerManager.SCREEN_DIM_WAKE_LOCK
                | PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.ON_AFTER_RELEASE, TAG);
        sWakeLock.acquire();

        // Disable Omaha related activities.
        OmahaBase.setIsDisabledForTesting(true);
        VersionNumberGetter.setEnableUpdateDetection(false);
    }

    public static void tearDown(Context context) throws Exception {
        Assert.assertNotNull("Uninitialized wake lock", sWakeLock);
        sWakeLock.release();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            try {
                finishAllChromeTasks(context);
            } catch (AssertionFailedError exception) {
            }
        }
    }

    /**
     * Clear all files and folders in the Chrome application directory except 'lib'.
     * The 'cache' directory is recreated as an empty directory.
     * @param context Target instrumentation context.
     */
    public static void clearAppData(Context context) {
        ApplicationData.clearAppData(context);
    }

    /** Send the user to the Android home screen. */
    public static void fireHomeScreenIntent(Context context) throws Exception {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_HOME);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
        waitUntilChromeInBackground();
    }

    /** Simulate starting Chrome from the launcher with a Main Intent. */
    public static void launchChrome(Context context) throws Exception {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.setPackage(context.getPackageName());
        intent.addCategory(Intent.CATEGORY_LAUNCHER);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);
        waitUntilChromeInForeground();
    }

    /** Waits until Chrome is in the background. */
    public static void waitUntilChromeInBackground() {
        CriteriaHelper.pollInstrumentationThread(new Criteria() {
            @Override
            public boolean isSatisfied() {
                int state = ApplicationStatus.getStateForApplication();
                return state == ApplicationState.HAS_STOPPED_ACTIVITIES
                        || state == ApplicationState.HAS_DESTROYED_ACTIVITIES;
            }
        });
    }

    /** Waits until Chrome is in the foreground. */
    public static void waitUntilChromeInForeground() {
        CriteriaHelper.pollInstrumentationThread(
                Criteria.equals(ApplicationState.HAS_RUNNING_ACTIVITIES, new Callable<Integer>() {
                    @Override
                    public Integer call() {
                        return ApplicationStatus.getStateForApplication();
                    }
                }));
    }

    /** Finishes the given activity and waits for its onDestroy() to be called. */
    public static void finishActivity(final Activity activity) throws Exception {
        final CallbackHelper callbackHelper = new CallbackHelper();
        final ApplicationStatus.ActivityStateListener activityStateListener =
                new ApplicationStatus.ActivityStateListener() {
                    @Override
                    public void onActivityStateChange(Activity activity, int newState) {
                        if (newState == ActivityState.DESTROYED) {
                            callbackHelper.notifyCalled();
                        }
                    }
                };
        try {
            boolean alreadyDestroyed = ThreadUtils.runOnUiThreadBlocking(new Callable<Boolean>() {
                @Override
                public Boolean call() {
                    if (ApplicationStatus.getStateForActivity(activity)
                            == ActivityState.DESTROYED) {
                        return true;
                    }
                    ApplicationStatus.registerStateListenerForActivity(
                            activityStateListener, activity);
                    activity.finish();
                    return false;
                }
            });
            if (!alreadyDestroyed) {
                callbackHelper.waitForCallback(0);
            }
        } finally {
            ApplicationStatus.unregisterActivityStateListener(activityStateListener);
        }
    }

    /** Finishes all tasks Chrome has listed in Android's Overview. */
    @TargetApi(Build.VERSION_CODES.LOLLIPOP)
    public static void finishAllChromeTasks(final Context context) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                try {
                    // Close all of the tasks one by one.
                    ActivityManager activityManager =
                            (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                    for (ActivityManager.AppTask task : activityManager.getAppTasks()) {
                        task.finishAndRemoveTask();
                    }
                } catch (Exception e) {
                    // Ignore any exceptions the Android framework throws so that otherwise passing
                    // tests don't fail during tear down. See crbug.com/653731.
                }
            }
        });

        CriteriaHelper.pollUiThread(Criteria.equals(0, new Callable<Integer>() {
            @Override
            public Integer call() {
                return getNumChromeTasks(context);
            }
        }));
    }

    /** Counts how many tasks Chrome has listed in Android's Overview. */
    @TargetApi(Build.VERSION_CODES.LOLLIPOP)
    public static int getNumChromeTasks(Context context) {
        ActivityManager activityManager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        return activityManager.getAppTasks().size();
    }

    /**
     * Waits till the ContentViewCore receives the expected page scale factor
     * from the compositor and asserts that this happens.
     *
     * Proper use of this function requires waiting for a page scale factor that isn't 1.0f because
     * the default seems to be 1.0f.
     */
    public static void assertWaitForPageScaleFactorMatch(
            final ChromeActivity activity, final float expectedScale) {
        CriteriaHelper.pollInstrumentationThread(new Criteria() {
            @Override
            public boolean isSatisfied() {
                if (activity.getCurrentContentViewCore() == null) return false;

                float scale = activity.getCurrentContentViewCore().getPageScaleFactor();
                updateFailureReason(
                        "Expecting scale factor of: " + expectedScale + ", got: " + scale);
                return Math.abs(scale - expectedScale) < FLOAT_EPSILON;
            }
        }, CriteriaHelper.DEFAULT_MAX_TIME_TO_POLL, CriteriaHelper.DEFAULT_POLLING_INTERVAL);
    }
}
