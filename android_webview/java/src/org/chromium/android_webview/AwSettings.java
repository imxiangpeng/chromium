// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Message;
import android.os.Process;
import android.provider.Settings;
import android.util.Log;
import android.webkit.WebSettings;
import android.webkit.WebSettings.LayoutAlgorithm;
import android.webkit.WebSettings.PluginState;
import android.webkit.WebSettings.ZoomDensity;

import org.chromium.base.BuildInfo;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.content_public.browser.WebContents;

/**
 * Stores Android WebView specific settings that does not need to be synced to WebKit.
 *
 * Methods in this class can be called from any thread, including threads created by
 * the client of WebView.
 */
@JNINamespace("android_webview")
public class AwSettings {
    private static final String LOGTAG = AwSettings.class.getSimpleName();
    private static final boolean TRACE = false;

    private static final String TAG = "AwSettings";

    // This class must be created on the UI thread. Afterwards, it can be
    // used from any thread. Internally, the class uses a message queue
    // to call native code on the UI thread only.

    // Values passed in on construction.
    private final boolean mHasInternetPermission;

    private ZoomSupportChangeListener mZoomChangeListener;
    private double mDIPScale = 1.0;

    // Lock to protect all settings.
    private final Object mAwSettingsLock = new Object();

    private LayoutAlgorithm mLayoutAlgorithm = LayoutAlgorithm.NARROW_COLUMNS;
    private int mTextSizePercent = 100;
    private String mStandardFontFamily = "sans-serif";
    private String mFixedFontFamily = "monospace";
    private String mSansSerifFontFamily = "sans-serif";
    private String mSerifFontFamily = "serif";
    private String mCursiveFontFamily = "cursive";
    private String mFantasyFontFamily = "fantasy";
    private String mDefaultTextEncoding = "UTF-8";
    private String mUserAgent;
    private int mMinimumFontSize = 8;
    private int mMinimumLogicalFontSize = 8;
    private int mDefaultFontSize = 16;
    private int mDefaultFixedFontSize = 13;
    private boolean mLoadsImagesAutomatically = true;
    private boolean mImagesEnabled = true;
    private boolean mJavaScriptEnabled;
    private boolean mAllowUniversalAccessFromFileURLs;
    private boolean mAllowFileAccessFromFileURLs;
    private boolean mJavaScriptCanOpenWindowsAutomatically;
    private boolean mSupportMultipleWindows;
    private PluginState mPluginState = PluginState.OFF;
    private boolean mAppCacheEnabled;
    private boolean mDomStorageEnabled;
    private boolean mDatabaseEnabled;
    private boolean mUseWideViewport;
    private boolean mZeroLayoutHeightDisablesViewportQuirk;
    private boolean mForceZeroLayoutHeight;
    private boolean mLoadWithOverviewMode;
    private boolean mMediaPlaybackRequiresUserGesture = true;
    private String mDefaultVideoPosterURL;
    private float mInitialPageScalePercent;
    private boolean mSpatialNavigationEnabled;  // Default depends on device features.
    private boolean mEnableSupportedHardwareAcceleratedFeatures;
    private int mMixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW;
    private boolean mCSSHexAlphaColorEnabled = false;

    private boolean mOffscreenPreRaster;
    private int mDisabledMenuItems = WebSettings.MENU_ITEM_NONE;

    // Although this bit is stored on AwSettings it is actually controlled via the CookieManager.
    private boolean mAcceptThirdPartyCookies;

    // if null, default to AwContentsStatics.getSafeBrowsingEnabledByManifest()
    private Boolean mSafeBrowsingEnabled;

    private final boolean mSupportLegacyQuirks;
    private final boolean mAllowEmptyDocumentPersistence;
    private final boolean mAllowGeolocationOnInsecureOrigins;
    private final boolean mDoNotUpdateSelectionOnMutatingSelectionRange;

    private final boolean mPasswordEchoEnabled;

    // Not accessed by the native side.
    private boolean mBlockNetworkLoads;  // Default depends on permission of embedding APK.
    private boolean mAllowContentUrlAccess = true;
    private boolean mAllowFileUrlAccess = true;
    private int mCacheMode = WebSettings.LOAD_DEFAULT;
    private boolean mShouldFocusFirstNode = true;
    private boolean mGeolocationEnabled = true;
    private boolean mAutoCompleteEnabled = !BuildInfo.isAtLeastO();
    private boolean mFullscreenSupported;
    private boolean mSupportZoom = true;
    private boolean mBuiltInZoomControls;
    private boolean mDisplayZoomControls = true;

    static class LazyDefaultUserAgent{
        // Lazy Holder pattern
        private static final String sInstance = nativeGetDefaultUserAgent();
    }

    // Protects access to settings global fields.
    private static final Object sGlobalContentSettingsLock = new Object();
    // For compatibility with the legacy WebView, we can only enable AppCache when the path is
    // provided. However, we don't use the path, so we just check if we have received it from the
    // client.
    private static boolean sAppCachePathIsSet;

    // The native side of this object. It's lifetime is bounded by the WebContent it is attached to.
    private long mNativeAwSettings;

    // Custom handler that queues messages to call native code on the UI thread.
    private final EventHandler mEventHandler;

    private static final int MINIMUM_FONT_SIZE = 1;
    private static final int MAXIMUM_FONT_SIZE = 72;

    // Class to handle messages to be processed on the UI thread.
    private class EventHandler {
        // Message id for running a Runnable with mAwSettingsLock held.
        private static final int RUN_RUNNABLE_BLOCKING = 0;
        // Actual UI thread handler
        private Handler mHandler;
        // Synchronization flag.
        private boolean mSynchronizationPending;

        EventHandler() {
        }

        void bindUiThread() {
            if (mHandler != null) return;
            mHandler = new Handler(ThreadUtils.getUiThreadLooper()) {
                @Override
                public void handleMessage(Message msg) {
                    switch (msg.what) {
                        case RUN_RUNNABLE_BLOCKING:
                            synchronized (mAwSettingsLock) {
                                if (mNativeAwSettings != 0) {
                                    ((Runnable) msg.obj).run();
                                }
                                mSynchronizationPending = false;
                                mAwSettingsLock.notifyAll();
                            }
                            break;
                    }
                }
            };
        }

        void runOnUiThreadBlockingAndLocked(Runnable r) {
            assert Thread.holdsLock(mAwSettingsLock);
            if (mHandler == null) return;
            if (ThreadUtils.runningOnUiThread()) {
                r.run();
            } else {
                assert !mSynchronizationPending;
                mSynchronizationPending = true;
                mHandler.sendMessage(Message.obtain(null, RUN_RUNNABLE_BLOCKING, r));
                try {
                    while (mSynchronizationPending) {
                        mAwSettingsLock.wait();
                    }
                } catch (InterruptedException e) {
                    Log.e(TAG, "Interrupted waiting a Runnable to complete", e);
                    mSynchronizationPending = false;
                }
            }
        }

        void maybePostOnUiThread(Runnable r) {
            if (mHandler != null) {
                mHandler.post(r);
            }
        }

        void updateWebkitPreferencesLocked() {
            runOnUiThreadBlockingAndLocked(new Runnable() {
                @Override
                public void run() {
                    updateWebkitPreferencesOnUiThreadLocked();
                }
            });
        }
    }

    interface ZoomSupportChangeListener {
        public void onGestureZoomSupportChanged(
                boolean supportsDoubleTapZoom, boolean supportsMultiTouchZoom);
    }

    public AwSettings(Context context, boolean isAccessFromFileURLsGrantedByDefault,
            boolean supportsLegacyQuirks, boolean allowEmptyDocumentPersistence,
            boolean allowGeolocationOnInsecureOrigins,
            boolean doNotUpdateSelectionOnMutatingSelectionRange) {
        boolean hasInternetPermission = context.checkPermission(
                android.Manifest.permission.INTERNET,
                Process.myPid(),
                Process.myUid()) == PackageManager.PERMISSION_GRANTED;
        synchronized (mAwSettingsLock) {
            mHasInternetPermission = hasInternetPermission;
            mBlockNetworkLoads = !hasInternetPermission;
            mEventHandler = new EventHandler();
            if (isAccessFromFileURLsGrantedByDefault) {
                mAllowUniversalAccessFromFileURLs = true;
                mAllowFileAccessFromFileURLs = true;
            }

            mUserAgent = LazyDefaultUserAgent.sInstance;

            // Best-guess a sensible initial value based on the features supported on the device.
            mSpatialNavigationEnabled = !context.getPackageManager().hasSystemFeature(
                    PackageManager.FEATURE_TOUCHSCREEN);

            // Respect the system setting for password echoing.
            mPasswordEchoEnabled = Settings.System.getInt(context.getContentResolver(),
                    Settings.System.TEXT_SHOW_PASSWORD, 1) == 1;

            // By default, scale the text size by the system font scale factor. Embedders
            // may override this by invoking setTextZoom().
            mTextSizePercent *= context.getResources().getConfiguration().fontScale;

            mSupportLegacyQuirks = supportsLegacyQuirks;
            mAllowEmptyDocumentPersistence = allowEmptyDocumentPersistence;
            mAllowGeolocationOnInsecureOrigins = allowGeolocationOnInsecureOrigins;
            mDoNotUpdateSelectionOnMutatingSelectionRange =
                    doNotUpdateSelectionOnMutatingSelectionRange;
        }
        // Defer initializing the native side until a native WebContents instance is set.
    }

    @CalledByNative
    private void nativeAwSettingsGone(long nativeAwSettings) {
        assert mNativeAwSettings != 0 && mNativeAwSettings == nativeAwSettings;
        mNativeAwSettings = 0;
    }

    @CalledByNative
    private double getDIPScaleLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDIPScale;
    }

    void setDIPScale(double dipScale) {
        synchronized (mAwSettingsLock) {
            mDIPScale = dipScale;
            // TODO(joth): This should also be synced over to native side, but right now
            // the setDIPScale call is always followed by a setWebContents() which covers this.
        }
    }

    void setZoomListener(ZoomSupportChangeListener zoomChangeListener) {
        synchronized (mAwSettingsLock) {
            mZoomChangeListener = zoomChangeListener;
        }
    }

    void setWebContents(WebContents webContents) {
        synchronized (mAwSettingsLock) {
            if (mNativeAwSettings != 0) {
                nativeDestroy(mNativeAwSettings);
                assert mNativeAwSettings == 0;  // nativeAwSettingsGone should have been called.
            }
            if (webContents != null) {
                mEventHandler.bindUiThread();
                mNativeAwSettings = nativeInit(webContents);
                updateEverythingLocked();
            }
        }
    }

    private void updateEverythingLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        assert mNativeAwSettings != 0;
        nativeUpdateEverythingLocked(mNativeAwSettings);
        onGestureZoomSupportChanged(
                supportsDoubleTapZoomLocked(), supportsMultiTouchZoomLocked());
    }

    /**
     * See {@link android.webkit.WebSettings#setBlockNetworkLoads}.
     */
    public void setBlockNetworkLoads(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setBlockNetworkLoads=" + flag);
        synchronized (mAwSettingsLock) {
            if (!flag && !mHasInternetPermission) {
                throw new SecurityException("Permission denied - "
                        + "application missing INTERNET permission");
            }
            mBlockNetworkLoads = flag;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getBlockNetworkLoads}.
     */
    public boolean getBlockNetworkLoads() {
        synchronized (mAwSettingsLock) {
            return mBlockNetworkLoads;
        }
    }

    /**
     * Enable/disable third party cookies for an AwContents
     * @param accept true if we should accept third party cookies
     */
    public void setAcceptThirdPartyCookies(boolean accept) {
        if (TRACE) Log.d(LOGTAG, "setAcceptThirdPartyCookies=" + accept);
        synchronized (mAwSettingsLock) {
            mAcceptThirdPartyCookies = accept;
        }
    }

    /**
     * Enable/Disable SafeBrowsing per WebView
     * @param enabled true if this WebView should have SafeBrowsing
     */
    public void setSafeBrowsingEnabled(boolean enabled) {
        synchronized (mAwSettingsLock) {
            mSafeBrowsingEnabled = enabled;
        }
    }

    /**
     * Return whether third party cookies are enabled for an AwContents
     * @return true if accept third party cookies
     */
    public boolean getAcceptThirdPartyCookies() {
        synchronized (mAwSettingsLock) {
            return mAcceptThirdPartyCookies;
        }
    }

    /**
     * Return whether Safe Browsing has been enabled for the current WebView
     * @return true if SafeBrowsing is enabled
     */
    public boolean getSafeBrowsingEnabled() {
        synchronized (mAwSettingsLock) {
            Boolean userOptIn = AwSafeBrowsingConfigHelper.getSafeBrowsingUserOptIn();

            // If we don't know yet what the user's preference is, we go through Safe Browsing logic
            // anyway and correct the assumption before sending data to GMS.
            if (userOptIn != null && !userOptIn) return false;

            if (mSafeBrowsingEnabled == null) {
                return AwContentsStatics.getSafeBrowsingEnabledByManifest();
            }
            return mSafeBrowsingEnabled;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setAllowFileAccess}.
     */
    public void setAllowFileAccess(boolean allow) {
        if (TRACE) Log.d(LOGTAG, "setAllowFileAccess=" + allow);
        synchronized (mAwSettingsLock) {
            mAllowFileUrlAccess = allow;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getAllowFileAccess}.
     */
    public boolean getAllowFileAccess() {
        synchronized (mAwSettingsLock) {
            return mAllowFileUrlAccess;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setAllowContentAccess}.
     */
    public void setAllowContentAccess(boolean allow) {
        if (TRACE) Log.d(LOGTAG, "setAllowContentAccess=" + allow);
        synchronized (mAwSettingsLock) {
            mAllowContentUrlAccess = allow;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getAllowContentAccess}.
     */
    public boolean getAllowContentAccess() {
        synchronized (mAwSettingsLock) {
            return mAllowContentUrlAccess;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setCacheMode}.
     */
    public void setCacheMode(int mode) {
        if (TRACE) Log.d(LOGTAG, "setCacheMode=" + mode);
        synchronized (mAwSettingsLock) {
            mCacheMode = mode;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getCacheMode}.
     */
    public int getCacheMode() {
        synchronized (mAwSettingsLock) {
            return mCacheMode;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setNeedInitialFocus}.
     */
    public void setShouldFocusFirstNode(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setNeedInitialFocusNode=" + flag);
        synchronized (mAwSettingsLock) {
            mShouldFocusFirstNode = flag;
        }
    }

    /**
     * See {@link android.webkit.WebView#setInitialScale}.
     */
    public void setInitialPageScale(final float scaleInPercent) {
        if (TRACE) Log.d(LOGTAG, "setInitialScale=" + scaleInPercent);
        synchronized (mAwSettingsLock) {
            if (mInitialPageScalePercent != scaleInPercent) {
                mInitialPageScalePercent = scaleInPercent;
                mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                    @Override
                    public void run() {
                        if (mNativeAwSettings != 0) {
                            nativeUpdateInitialPageScaleLocked(mNativeAwSettings);
                        }
                    }
                });
            }
        }
    }

    @CalledByNative
    private float getInitialPageScalePercentLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mInitialPageScalePercent;
    }

    void setSpatialNavigationEnabled(boolean enable) {
        synchronized (mAwSettingsLock) {
            if (mSpatialNavigationEnabled != enable) {
                mSpatialNavigationEnabled = enable;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    @CalledByNative
    private boolean getSpatialNavigationLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSpatialNavigationEnabled;
    }

    void setEnableSupportedHardwareAcceleratedFeatures(boolean enable) {
        synchronized (mAwSettingsLock) {
            if (mEnableSupportedHardwareAcceleratedFeatures != enable) {
                mEnableSupportedHardwareAcceleratedFeatures = enable;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    @CalledByNative
    private boolean getEnableSupportedHardwareAcceleratedFeaturesLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mEnableSupportedHardwareAcceleratedFeatures;
    }

    public void setFullscreenSupported(boolean supported) {
        synchronized (mAwSettingsLock) {
            if (mFullscreenSupported != supported) {
                mFullscreenSupported = supported;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    @CalledByNative
    private boolean getFullscreenSupportedLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mFullscreenSupported;
    }

    /**
     * See {@link android.webkit.WebSettings#setNeedInitialFocus}.
     */
    public boolean shouldFocusFirstNode() {
        synchronized (mAwSettingsLock) {
            return mShouldFocusFirstNode;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setGeolocationEnabled}.
     */
    public void setGeolocationEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setGeolocationEnabled=" + flag);
        synchronized (mAwSettingsLock) {
            mGeolocationEnabled = flag;
        }
    }

    /**
     * @return Returns if geolocation is currently enabled.
     */
    boolean getGeolocationEnabled() {
        synchronized (mAwSettingsLock) {
            return mGeolocationEnabled;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setSaveFormData}.
     */
    public void setSaveFormData(final boolean enable) {
        if (TRACE) Log.d(LOGTAG, "setSaveFormData=" + enable);
        synchronized (mAwSettingsLock) {
            if (mAutoCompleteEnabled != enable) {
                mAutoCompleteEnabled = enable;
                mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                    @Override
                    public void run() {
                        if (mNativeAwSettings != 0) {
                            nativeUpdateFormDataPreferencesLocked(mNativeAwSettings);
                        }
                    }
                });
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getSaveFormData}.
     */
    public boolean getSaveFormData() {
        synchronized (mAwSettingsLock) {
            return getSaveFormDataLocked();
        }
    }

    @CalledByNative
    private boolean getSaveFormDataLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mAutoCompleteEnabled;
    }

    public void setUserAgent(int ua) {
        // Minimal implementation for backwards compatibility: just supports resetting to default.
        if (ua == 0) {
            setUserAgentString(null);
        } else {
            Log.w(LOGTAG, "setUserAgent not supported, ua=" + ua);
        }
    }

    /**
     * @returns the default User-Agent used by each ContentViewCore instance, i.e. unless
     * overridden by {@link #setUserAgentString()}
     */
    public static String getDefaultUserAgent() {
        return LazyDefaultUserAgent.sInstance;
    }

    /**
     * See {@link android.webkit.WebSettings#setUserAgentString}.
     */
    public void setUserAgentString(String ua) {
        if (TRACE) Log.d(LOGTAG, "setUserAgentString=" + ua);
        synchronized (mAwSettingsLock) {
            final String oldUserAgent = mUserAgent;
            if (ua == null || ua.length() == 0) {
                mUserAgent = LazyDefaultUserAgent.sInstance;
            } else {
                mUserAgent = ua;
            }
            if (!oldUserAgent.equals(mUserAgent)) {
                mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                    @Override
                    public void run() {
                        if (mNativeAwSettings != 0) {
                            nativeUpdateUserAgentLocked(mNativeAwSettings);
                        }
                    }
                });
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getUserAgentString}.
     */
    public String getUserAgentString() {
        synchronized (mAwSettingsLock) {
            return getUserAgentLocked();
        }
    }

    @CalledByNative
    private String getUserAgentLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mUserAgent;
    }

    /**
     * See {@link android.webkit.WebSettings#setLoadWithOverviewMode}.
     */
    public void setLoadWithOverviewMode(boolean overview) {
        if (TRACE) Log.d(LOGTAG, "setLoadWithOverviewMode=" + overview);
        synchronized (mAwSettingsLock) {
            if (mLoadWithOverviewMode != overview) {
                mLoadWithOverviewMode = overview;
                mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                    @Override
                    public void run() {
                        if (mNativeAwSettings != 0) {
                            updateWebkitPreferencesOnUiThreadLocked();
                            nativeResetScrollAndScaleState(mNativeAwSettings);
                        }
                    }
                });
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getLoadWithOverviewMode}.
     */
    public boolean getLoadWithOverviewMode() {
        synchronized (mAwSettingsLock) {
            return getLoadWithOverviewModeLocked();
        }
    }

    @CalledByNative
    private boolean getLoadWithOverviewModeLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mLoadWithOverviewMode;
    }

    /**
     * See {@link android.webkit.WebSettings#setTextZoom}.
     */
    public void setTextZoom(final int textZoom) {
        if (TRACE) Log.d(LOGTAG, "setTextZoom=" + textZoom);
        synchronized (mAwSettingsLock) {
            if (mTextSizePercent != textZoom) {
                mTextSizePercent = textZoom;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getTextZoom}.
     */
    public int getTextZoom() {
        synchronized (mAwSettingsLock) {
            return getTextSizePercentLocked();
        }
    }

    public void setDefaultZoom(ZoomDensity zoom) {
        if (zoom != ZoomDensity.MEDIUM) {
            Log.w(LOGTAG, "setDefaultZoom not supported, zoom=" + zoom);
        }
    }

    public ZoomDensity getDefaultZoom() {
        // Intentional no-op.
        return ZoomDensity.MEDIUM;
    }

    @CalledByNative
    private int getTextSizePercentLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mTextSizePercent;
    }

    /**
     * See {@link android.webkit.WebSettings#setStandardFontFamily}.
     */
    public void setStandardFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setStandardFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mStandardFontFamily.equals(font)) {
                mStandardFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getStandardFontFamily}.
     */
    public String getStandardFontFamily() {
        synchronized (mAwSettingsLock) {
            return getStandardFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getStandardFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mStandardFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setFixedFontFamily}.
     */
    public void setFixedFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setFixedFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mFixedFontFamily.equals(font)) {
                mFixedFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getFixedFontFamily}.
     */
    public String getFixedFontFamily() {
        synchronized (mAwSettingsLock) {
            return getFixedFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getFixedFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mFixedFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setSansSerifFontFamily}.
     */
    public void setSansSerifFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setSansSerifFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mSansSerifFontFamily.equals(font)) {
                mSansSerifFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getSansSerifFontFamily}.
     */
    public String getSansSerifFontFamily() {
        synchronized (mAwSettingsLock) {
            return getSansSerifFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getSansSerifFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSansSerifFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setSerifFontFamily}.
     */
    public void setSerifFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setSerifFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mSerifFontFamily.equals(font)) {
                mSerifFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getSerifFontFamily}.
     */
    public String getSerifFontFamily() {
        synchronized (mAwSettingsLock) {
            return getSerifFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getSerifFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSerifFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setCursiveFontFamily}.
     */
    public void setCursiveFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setCursiveFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mCursiveFontFamily.equals(font)) {
                mCursiveFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getCursiveFontFamily}.
     */
    public String getCursiveFontFamily() {
        synchronized (mAwSettingsLock) {
            return getCursiveFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getCursiveFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mCursiveFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setFantasyFontFamily}.
     */
    public void setFantasyFontFamily(String font) {
        if (TRACE) Log.d(LOGTAG, "setFantasyFontFamily=" + font);
        synchronized (mAwSettingsLock) {
            if (font != null && !mFantasyFontFamily.equals(font)) {
                mFantasyFontFamily = font;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getFantasyFontFamily}.
     */
    public String getFantasyFontFamily() {
        synchronized (mAwSettingsLock) {
            return getFantasyFontFamilyLocked();
        }
    }

    @CalledByNative
    private String getFantasyFontFamilyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mFantasyFontFamily;
    }

    /**
     * See {@link android.webkit.WebSettings#setMinimumFontSize}.
     */
    public void setMinimumFontSize(int size) {
        if (TRACE) Log.d(LOGTAG, "setMinimumFontSize=" + size);
        synchronized (mAwSettingsLock) {
            size = clipFontSize(size);
            if (mMinimumFontSize != size) {
                mMinimumFontSize = size;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getMinimumFontSize}.
     */
    public int getMinimumFontSize() {
        synchronized (mAwSettingsLock) {
            return getMinimumFontSizeLocked();
        }
    }

    @CalledByNative
    private int getMinimumFontSizeLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mMinimumFontSize;
    }

    /**
     * See {@link android.webkit.WebSettings#setMinimumLogicalFontSize}.
     */
    public void setMinimumLogicalFontSize(int size) {
        if (TRACE) Log.d(LOGTAG, "setMinimumLogicalFontSize=" + size);
        synchronized (mAwSettingsLock) {
            size = clipFontSize(size);
            if (mMinimumLogicalFontSize != size) {
                mMinimumLogicalFontSize = size;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getMinimumLogicalFontSize}.
     */
    public int getMinimumLogicalFontSize() {
        synchronized (mAwSettingsLock) {
            return getMinimumLogicalFontSizeLocked();
        }
    }

    @CalledByNative
    private int getMinimumLogicalFontSizeLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mMinimumLogicalFontSize;
    }

    /**
     * See {@link android.webkit.WebSettings#setDefaultFontSize}.
     */
    public void setDefaultFontSize(int size) {
        if (TRACE) Log.d(LOGTAG, "setDefaultFontSize=" + size);
        synchronized (mAwSettingsLock) {
            size = clipFontSize(size);
            if (mDefaultFontSize != size) {
                mDefaultFontSize = size;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDefaultFontSize}.
     */
    public int getDefaultFontSize() {
        synchronized (mAwSettingsLock) {
            return getDefaultFontSizeLocked();
        }
    }

    @CalledByNative
    private int getDefaultFontSizeLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDefaultFontSize;
    }

    /**
     * See {@link android.webkit.WebSettings#setDefaultFixedFontSize}.
     */
    public void setDefaultFixedFontSize(int size) {
        if (TRACE) Log.d(LOGTAG, "setDefaultFixedFontSize=" + size);
        synchronized (mAwSettingsLock) {
            size = clipFontSize(size);
            if (mDefaultFixedFontSize != size) {
                mDefaultFixedFontSize = size;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDefaultFixedFontSize}.
     */
    public int getDefaultFixedFontSize() {
        synchronized (mAwSettingsLock) {
            return getDefaultFixedFontSizeLocked();
        }
    }

    @CalledByNative
    private int getDefaultFixedFontSizeLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDefaultFixedFontSize;
    }

    /**
     * See {@link android.webkit.WebSettings#setJavaScriptEnabled}.
     */
    public void setJavaScriptEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setJavaScriptEnabled=" + flag);
        synchronized (mAwSettingsLock) {
            if (mJavaScriptEnabled != flag) {
                mJavaScriptEnabled = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setAllowUniversalAccessFromFileURLs}.
     */
    public void setAllowUniversalAccessFromFileURLs(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setAllowUniversalAccessFromFileURLs=" + flag);
        synchronized (mAwSettingsLock) {
            if (mAllowUniversalAccessFromFileURLs != flag) {
                mAllowUniversalAccessFromFileURLs = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setAllowFileAccessFromFileURLs}.
     */
    public void setAllowFileAccessFromFileURLs(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setAllowFileAccessFromFileURLs=" + flag);
        synchronized (mAwSettingsLock) {
            if (mAllowFileAccessFromFileURLs != flag) {
                mAllowFileAccessFromFileURLs = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setLoadsImagesAutomatically}.
     */
    public void setLoadsImagesAutomatically(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setLoadsImagesAutomatically=" + flag);
        synchronized (mAwSettingsLock) {
            if (mLoadsImagesAutomatically != flag) {
                mLoadsImagesAutomatically = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getLoadsImagesAutomatically}.
     */
    public boolean getLoadsImagesAutomatically() {
        synchronized (mAwSettingsLock) {
            return getLoadsImagesAutomaticallyLocked();
        }
    }

    @CalledByNative
    private boolean getLoadsImagesAutomaticallyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mLoadsImagesAutomatically;
    }

    /**
     * See {@link android.webkit.WebSettings#setImagesEnabled}.
     */
    public void setImagesEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setBlockNetworkImage=" + flag);
        synchronized (mAwSettingsLock) {
            if (mImagesEnabled != flag) {
                mImagesEnabled = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getImagesEnabled}.
     */
    public boolean getImagesEnabled() {
        synchronized (mAwSettingsLock) {
            return mImagesEnabled;
        }
    }

    @CalledByNative
    private boolean getImagesEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mImagesEnabled;
    }

    /**
     * See {@link android.webkit.WebSettings#getJavaScriptEnabled}.
     */
    public boolean getJavaScriptEnabled() {
        synchronized (mAwSettingsLock) {
            return mJavaScriptEnabled;
        }
    }

    @CalledByNative
    private boolean getJavaScriptEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mJavaScriptEnabled;
    }

    /**
     * See {@link android.webkit.WebSettings#getAllowUniversalAccessFromFileURLs}.
     */
    public boolean getAllowUniversalAccessFromFileURLs() {
        synchronized (mAwSettingsLock) {
            return getAllowUniversalAccessFromFileURLsLocked();
        }
    }

    @CalledByNative
    private boolean getAllowUniversalAccessFromFileURLsLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mAllowUniversalAccessFromFileURLs;
    }

    /**
     * See {@link android.webkit.WebSettings#getAllowFileAccessFromFileURLs}.
     */
    public boolean getAllowFileAccessFromFileURLs() {
        synchronized (mAwSettingsLock) {
            return getAllowFileAccessFromFileURLsLocked();
        }
    }

    @CalledByNative
    private boolean getAllowFileAccessFromFileURLsLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mAllowFileAccessFromFileURLs;
    }

    /**
     * See {@link android.webkit.WebSettings#setPluginsEnabled}.
     */
    public void setPluginsEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setPluginsEnabled=" + flag);
        setPluginState(flag ? PluginState.ON : PluginState.OFF);
    }

    /**
     * See {@link android.webkit.WebSettings#setPluginState}.
     */
    public void setPluginState(PluginState state) {
        if (TRACE) Log.d(LOGTAG, "setPluginState=" + state);
        synchronized (mAwSettingsLock) {
            if (mPluginState != state) {
                mPluginState = state;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getPluginsEnabled}.
     */
    public boolean getPluginsEnabled() {
        synchronized (mAwSettingsLock) {
            return mPluginState == PluginState.ON;
        }
    }

    /**
     * Return true if plugins are disabled.
     * @return True if plugins are disabled.
     */
    @CalledByNative
    private boolean getPluginsDisabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mPluginState == PluginState.OFF;
    }

    /**
     * See {@link android.webkit.WebSettings#getPluginState}.
     */
    public PluginState getPluginState() {
        synchronized (mAwSettingsLock) {
            return mPluginState;
        }
    }


    /**
     * See {@link android.webkit.WebSettings#setJavaScriptCanOpenWindowsAutomatically}.
     */
    public void setJavaScriptCanOpenWindowsAutomatically(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setJavaScriptCanOpenWindowsAutomatically=" + flag);
        synchronized (mAwSettingsLock) {
            if (mJavaScriptCanOpenWindowsAutomatically != flag) {
                mJavaScriptCanOpenWindowsAutomatically = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getJavaScriptCanOpenWindowsAutomatically}.
     */
    public boolean getJavaScriptCanOpenWindowsAutomatically() {
        synchronized (mAwSettingsLock) {
            return getJavaScriptCanOpenWindowsAutomaticallyLocked();
        }
    }

    @CalledByNative
    private boolean getJavaScriptCanOpenWindowsAutomaticallyLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mJavaScriptCanOpenWindowsAutomatically;
    }

    /**
     * See {@link android.webkit.WebSettings#setLayoutAlgorithm}.
     */
    public void setLayoutAlgorithm(LayoutAlgorithm l) {
        if (TRACE) Log.d(LOGTAG, "setLayoutAlgorithm=" + l);
        synchronized (mAwSettingsLock) {
            if (mLayoutAlgorithm != l) {
                mLayoutAlgorithm = l;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getLayoutAlgorithm}.
     */
    public LayoutAlgorithm getLayoutAlgorithm() {
        synchronized (mAwSettingsLock) {
            return mLayoutAlgorithm;
        }
    }

    /**
     * Gets whether Text Auto-sizing layout algorithm is enabled.
     *
     * @return true if Text Auto-sizing layout algorithm is enabled
     */
    @SuppressLint("NewApi")  // WebSettings.LayoutAlgorithm#TEXT_AUTOSIZING requires API level 19.
    @CalledByNative
    private boolean getTextAutosizingEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mLayoutAlgorithm == LayoutAlgorithm.TEXT_AUTOSIZING;
    }

    /**
     * See {@link android.webkit.WebSettings#setSupportMultipleWindows}.
     */
    public void setSupportMultipleWindows(boolean support) {
        if (TRACE) Log.d(LOGTAG, "setSupportMultipleWindows=" + support);
        synchronized (mAwSettingsLock) {
            if (mSupportMultipleWindows != support) {
                mSupportMultipleWindows = support;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#supportMultipleWindows}.
     */
    public boolean supportMultipleWindows() {
        synchronized (mAwSettingsLock) {
            return mSupportMultipleWindows;
        }
    }

    @CalledByNative
    private boolean getSupportMultipleWindowsLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSupportMultipleWindows;
    }

    @CalledByNative
    private boolean getCSSHexAlphaColorEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mCSSHexAlphaColorEnabled;
    }

    public void setCSSHexAlphaColorEnabled(boolean enabled) {
        synchronized (mAwSettingsLock) {
            if (mCSSHexAlphaColorEnabled != enabled) {
                mCSSHexAlphaColorEnabled = enabled;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    @CalledByNative
    private boolean getSupportLegacyQuirksLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSupportLegacyQuirks;
    }

    @CalledByNative
    private boolean getAllowEmptyDocumentPersistenceLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mAllowEmptyDocumentPersistence;
    }

    @CalledByNative
    private boolean getAllowGeolocationOnInsecureOrigins() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mAllowGeolocationOnInsecureOrigins;
    }

    @CalledByNative
    private boolean getDoNotUpdateSelectionOnMutatingSelectionRange() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDoNotUpdateSelectionOnMutatingSelectionRange;
    }

    /**
     * See {@link android.webkit.WebSettings#setUseWideViewPort}.
     */
    public void setUseWideViewPort(boolean use) {
        if (TRACE) Log.d(LOGTAG, "setUseWideViewPort=" + use);
        synchronized (mAwSettingsLock) {
            if (mUseWideViewport != use) {
                mUseWideViewport = use;
                onGestureZoomSupportChanged(
                        supportsDoubleTapZoomLocked(), supportsMultiTouchZoomLocked());
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getUseWideViewPort}.
     */
    public boolean getUseWideViewPort() {
        synchronized (mAwSettingsLock) {
            return getUseWideViewportLocked();
        }
    }

    @CalledByNative
    private boolean getUseWideViewportLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mUseWideViewport;
    }

    public void setZeroLayoutHeightDisablesViewportQuirk(boolean enabled) {
        synchronized (mAwSettingsLock) {
            if (mZeroLayoutHeightDisablesViewportQuirk != enabled) {
                mZeroLayoutHeightDisablesViewportQuirk = enabled;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    public boolean getZeroLayoutHeightDisablesViewportQuirk() {
        synchronized (mAwSettingsLock) {
            return getZeroLayoutHeightDisablesViewportQuirkLocked();
        }
    }

    @CalledByNative
    private boolean getZeroLayoutHeightDisablesViewportQuirkLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mZeroLayoutHeightDisablesViewportQuirk;
    }

    public void setForceZeroLayoutHeight(boolean enabled) {
        synchronized (mAwSettingsLock) {
            if (mForceZeroLayoutHeight != enabled) {
                mForceZeroLayoutHeight = enabled;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    public boolean getForceZeroLayoutHeight() {
        synchronized (mAwSettingsLock) {
            return getForceZeroLayoutHeightLocked();
        }
    }

    @CalledByNative
    private boolean getForceZeroLayoutHeightLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mForceZeroLayoutHeight;
    }

    @CalledByNative
    private boolean getPasswordEchoEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mPasswordEchoEnabled;
    }

    /**
     * See {@link android.webkit.WebSettings#setAppCacheEnabled}.
     */
    public void setAppCacheEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setAppCacheEnabled=" + flag);
        synchronized (mAwSettingsLock) {
            if (mAppCacheEnabled != flag) {
                mAppCacheEnabled = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setAppCachePath}.
     */
    public void setAppCachePath(String path) {
        if (TRACE) Log.d(LOGTAG, "setAppCachePath=" + path);
        boolean needToSync = false;
        synchronized (sGlobalContentSettingsLock) {
            // AppCachePath can only be set once.
            if (!sAppCachePathIsSet && path != null && !path.isEmpty()) {
                sAppCachePathIsSet = true;
                needToSync = true;
            }
        }
        // The obvious problem here is that other WebViews will not be updated,
        // until they execute synchronization from Java to the native side.
        // But this is the same behaviour as it was in the legacy WebView.
        if (needToSync) {
            synchronized (mAwSettingsLock) {
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * Gets whether Application Cache is enabled.
     *
     * @return true if Application Cache is enabled
     */
    @CalledByNative
    private boolean getAppCacheEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        if (!mAppCacheEnabled) {
            return false;
        }
        synchronized (sGlobalContentSettingsLock) {
            return sAppCachePathIsSet;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setDomStorageEnabled}.
     */
    public void setDomStorageEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setDomStorageEnabled=" + flag);
        synchronized (mAwSettingsLock) {
            if (mDomStorageEnabled != flag) {
                mDomStorageEnabled = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDomStorageEnabled}.
     */
    public boolean getDomStorageEnabled() {
        synchronized (mAwSettingsLock) {
            return mDomStorageEnabled;
        }
    }

    @CalledByNative
    private boolean getDomStorageEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDomStorageEnabled;
    }

    /**
     * See {@link android.webkit.WebSettings#setDatabaseEnabled}.
     */
    public void setDatabaseEnabled(boolean flag) {
        if (TRACE) Log.d(LOGTAG, "setDatabaseEnabled=" + flag);
        synchronized (mAwSettingsLock) {
            if (mDatabaseEnabled != flag) {
                mDatabaseEnabled = flag;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDatabaseEnabled}.
     */
    public boolean getDatabaseEnabled() {
        synchronized (mAwSettingsLock) {
            return mDatabaseEnabled;
        }
    }

    @CalledByNative
    private boolean getDatabaseEnabledLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDatabaseEnabled;
    }

    /**
     * See {@link android.webkit.WebSettings#setDefaultTextEncodingName}.
     */
    public void setDefaultTextEncodingName(String encoding) {
        if (TRACE) Log.d(LOGTAG, "setDefaultTextEncodingName=" + encoding);
        synchronized (mAwSettingsLock) {
            if (encoding != null && !mDefaultTextEncoding.equals(encoding)) {
                mDefaultTextEncoding = encoding;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDefaultTextEncodingName}.
     */
    public String getDefaultTextEncodingName() {
        synchronized (mAwSettingsLock) {
            return getDefaultTextEncodingLocked();
        }
    }

    @CalledByNative
    private String getDefaultTextEncodingLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDefaultTextEncoding;
    }

    /**
     * See {@link android.webkit.WebSettings#setMediaPlaybackRequiresUserGesture}.
     */
    public void setMediaPlaybackRequiresUserGesture(boolean require) {
        if (TRACE) Log.d(LOGTAG, "setMediaPlaybackRequiresUserGesture=" + require);
        synchronized (mAwSettingsLock) {
            if (mMediaPlaybackRequiresUserGesture != require) {
                mMediaPlaybackRequiresUserGesture = require;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getMediaPlaybackRequiresUserGesture}.
     */
    public boolean getMediaPlaybackRequiresUserGesture() {
        synchronized (mAwSettingsLock) {
            return getMediaPlaybackRequiresUserGestureLocked();
        }
    }

    @CalledByNative
    private boolean getMediaPlaybackRequiresUserGestureLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mMediaPlaybackRequiresUserGesture;
    }

    /**
     * See {@link android.webkit.WebSettings#setDefaultVideoPosterURL}.
     */
    public void setDefaultVideoPosterURL(String url) {
        synchronized (mAwSettingsLock) {
            if (mDefaultVideoPosterURL != null && !mDefaultVideoPosterURL.equals(url)
                    || mDefaultVideoPosterURL == null && url != null) {
                mDefaultVideoPosterURL = url;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDefaultVideoPosterURL}.
     */
    public String getDefaultVideoPosterURL() {
        synchronized (mAwSettingsLock) {
            return getDefaultVideoPosterURLLocked();
        }
    }

    @CalledByNative
    private String getDefaultVideoPosterURLLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mDefaultVideoPosterURL;
    }

    private void onGestureZoomSupportChanged(
            final boolean supportsDoubleTapZoom, final boolean supportsMultiTouchZoom) {
        // Always post asynchronously here, to avoid doubling back onto the caller.
        mEventHandler.maybePostOnUiThread(new Runnable() {
            @Override
            public void run() {
                synchronized (mAwSettingsLock) {
                    if (mZoomChangeListener != null) {
                        mZoomChangeListener.onGestureZoomSupportChanged(
                                supportsDoubleTapZoom, supportsMultiTouchZoom);
                    }
                }
            }
        });
    }

    /**
     * See {@link android.webkit.WebSettings#setSupportZoom}.
     */
    public void setSupportZoom(boolean support) {
        if (TRACE) Log.d(LOGTAG, "setSupportZoom=" + support);
        synchronized (mAwSettingsLock) {
            if (mSupportZoom != support) {
                mSupportZoom = support;
                onGestureZoomSupportChanged(
                        supportsDoubleTapZoomLocked(), supportsMultiTouchZoomLocked());
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#supportZoom}.
     */
    public boolean supportZoom() {
        synchronized (mAwSettingsLock) {
            return mSupportZoom;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setBuiltInZoomControls}.
     */
    public void setBuiltInZoomControls(boolean enabled) {
        if (TRACE) Log.d(LOGTAG, "setBuiltInZoomControls=" + enabled);
        synchronized (mAwSettingsLock) {
            if (mBuiltInZoomControls != enabled) {
                mBuiltInZoomControls = enabled;
                onGestureZoomSupportChanged(
                        supportsDoubleTapZoomLocked(), supportsMultiTouchZoomLocked());
            }
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getBuiltInZoomControls}.
     */
    public boolean getBuiltInZoomControls() {
        synchronized (mAwSettingsLock) {
            return mBuiltInZoomControls;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#setDisplayZoomControls}.
     */
    public void setDisplayZoomControls(boolean enabled) {
        if (TRACE) Log.d(LOGTAG, "setDisplayZoomControls=" + enabled);
        synchronized (mAwSettingsLock) {
            mDisplayZoomControls = enabled;
        }
    }

    /**
     * See {@link android.webkit.WebSettings#getDisplayZoomControls}.
     */
    public boolean getDisplayZoomControls() {
        synchronized (mAwSettingsLock) {
            return mDisplayZoomControls;
        }
    }

    public void setMixedContentMode(int mode) {
        synchronized (mAwSettingsLock) {
            if (mMixedContentMode != mode) {
                mMixedContentMode = mode;
                mEventHandler.updateWebkitPreferencesLocked();
            }
        }
    }

    public int getMixedContentMode() {
        synchronized (mAwSettingsLock) {
            return mMixedContentMode;
        }
    }

    @CalledByNative
    private boolean getAllowRunningInsecureContentLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mMixedContentMode == WebSettings.MIXED_CONTENT_ALWAYS_ALLOW;
    }

    @CalledByNative
    private boolean getUseStricMixedContentCheckingLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mMixedContentMode == WebSettings.MIXED_CONTENT_NEVER_ALLOW;
    }

    public boolean getOffscreenPreRaster() {
        synchronized (mAwSettingsLock) {
            return getOffscreenPreRasterLocked();
        }
    }

    @CalledByNative
    private boolean getOffscreenPreRasterLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mOffscreenPreRaster;
    }

    /**
     * Sets whether this WebView should raster tiles when it is
     * offscreen but attached to window. Turning this on can avoid
     * rendering artifacts when animating an offscreen WebView on-screen.
     * In particular, insertVisualStateCallback requires this mode to function.
     * Offscreen WebViews in this mode uses more memory. Please follow
     * these guidelines to limit memory usage:
     * - Webview size should be not be larger than the device screen size.
     * - Limit simple mode to a small number of webviews. Use it for
     *   visible webviews and webviews about to be animated to visible.
     */
    public void setOffscreenPreRaster(boolean enabled) {
        synchronized (mAwSettingsLock) {
            if (enabled != mOffscreenPreRaster) {
                mOffscreenPreRaster = enabled;
                mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                    @Override
                    public void run() {
                        if (mNativeAwSettings != 0) {
                            nativeUpdateOffscreenPreRasterLocked(mNativeAwSettings);
                        }
                    }
                });
            }
        }
    }

    public int getDisabledActionModeMenuItems() {
        synchronized (mAwSettingsLock) {
            return mDisabledMenuItems;
        }
    }

    public void setDisabledActionModeMenuItems(int menuItems) {
        synchronized (mAwSettingsLock) {
            mDisabledMenuItems = menuItems;
        }
    }

    @VisibleForTesting
    public void updateAcceptLanguages() {
        synchronized (mAwSettingsLock) {
            mEventHandler.runOnUiThreadBlockingAndLocked(new Runnable() {
                @Override
                public void run() {
                    if (mNativeAwSettings != 0) {
                        nativeUpdateRendererPreferencesLocked(mNativeAwSettings);
                    }
                }
            });
        }
    }

    @CalledByNative
    private boolean supportsDoubleTapZoomLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSupportZoom && mBuiltInZoomControls && mUseWideViewport;
    }

    private boolean supportsMultiTouchZoomLocked() {
        assert Thread.holdsLock(mAwSettingsLock);
        return mSupportZoom && mBuiltInZoomControls;
    }

    boolean supportsMultiTouchZoom() {
        synchronized (mAwSettingsLock) {
            return supportsMultiTouchZoomLocked();
        }
    }

    boolean shouldDisplayZoomControls() {
        synchronized (mAwSettingsLock) {
            return supportsMultiTouchZoomLocked() && mDisplayZoomControls;
        }
    }

    private int clipFontSize(int size) {
        if (size < MINIMUM_FONT_SIZE) {
            return MINIMUM_FONT_SIZE;
        } else if (size > MAXIMUM_FONT_SIZE) {
            return MAXIMUM_FONT_SIZE;
        }
        return size;
    }

    @CalledByNative
    private boolean getRecordFullDocument() {
        assert Thread.holdsLock(mAwSettingsLock);
        return AwContentsStatics.getRecordFullDocument();
    }

    @CalledByNative
    private void updateEverything() {
        synchronized (mAwSettingsLock) {
            updateEverythingLocked();
        }
    }

    @CalledByNative
    private void populateWebPreferences(long webPrefsPtr) {
        synchronized (mAwSettingsLock) {
            assert mNativeAwSettings != 0;
            nativePopulateWebPreferencesLocked(mNativeAwSettings, webPrefsPtr);
        }
    }

    private void updateWebkitPreferencesOnUiThreadLocked() {
        assert mEventHandler.mHandler != null;
        ThreadUtils.assertOnUiThread();
        if (mNativeAwSettings != 0) {
            nativeUpdateWebkitPreferencesLocked(mNativeAwSettings);
        }
    }

    private native long nativeInit(WebContents webContents);

    private native void nativeDestroy(long nativeAwSettings);

    private native void nativePopulateWebPreferencesLocked(long nativeAwSettings, long webPrefsPtr);

    private native void nativeResetScrollAndScaleState(long nativeAwSettings);

    private native void nativeUpdateEverythingLocked(long nativeAwSettings);

    private native void nativeUpdateInitialPageScaleLocked(long nativeAwSettings);

    private native void nativeUpdateUserAgentLocked(long nativeAwSettings);

    private native void nativeUpdateWebkitPreferencesLocked(long nativeAwSettings);

    private static native String nativeGetDefaultUserAgent();

    private native void nativeUpdateFormDataPreferencesLocked(long nativeAwSettings);

    private native void nativeUpdateRendererPreferencesLocked(long nativeAwSettings);

    private native void nativeUpdateOffscreenPreRasterLocked(long nativeAwSettings);
}
