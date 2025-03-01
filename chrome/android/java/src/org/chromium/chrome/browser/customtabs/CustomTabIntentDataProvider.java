// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.customtabs;

import android.app.Activity;
import android.app.PendingIntent;
import android.app.PendingIntent.CanceledException;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.support.annotation.IntDef;
import android.support.customtabs.CustomTabsIntent;
import android.support.customtabs.CustomTabsSessionToken;
import android.text.TextUtils;
import android.util.Pair;
import android.view.View;
import android.widget.RemoteViews;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Log;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.util.IntentUtils;
import org.chromium.chrome.browser.widget.TintedDrawable;
import org.chromium.ui.widget.Toast;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;

/**
 * A model class that parses intent from third-party apps and provides results to
 * {@link CustomTabActivity}.
 */
public class CustomTabIntentDataProvider {
    private static final String TAG = "CustomTabIntentData";

    // The type of UI for Custom Tab to use.
    @Retention(RetentionPolicy.SOURCE)
    @IntDef({
            CUSTOM_TABS_UI_TYPE_DEFAULT, CUSTOM_TABS_UI_TYPE_MEDIA_VIEWER,
            CUSTOM_TABS_UI_TYPE_PAYMENT_REQUEST, CUSTOM_TABS_UI_TYPE_INFO_PAGE,
            CUSTOM_TABS_UI_TYPE_READER_MODE,
    })
    public @interface CustomTabsUiType {}
    public static final int CUSTOM_TABS_UI_TYPE_DEFAULT = 0;
    public static final int CUSTOM_TABS_UI_TYPE_MEDIA_VIEWER = 1;
    public static final int CUSTOM_TABS_UI_TYPE_PAYMENT_REQUEST = 2;
    public static final int CUSTOM_TABS_UI_TYPE_INFO_PAGE = 3;
    public static final int CUSTOM_TABS_UI_TYPE_READER_MODE = 4;

    /**
     * Extra used to keep the caller alive. Its value is an Intent.
     */
    public static final String EXTRA_KEEP_ALIVE = "android.support.customtabs.extra.KEEP_ALIVE";

    /**
     * Herb: Extra that indicates whether or not the Custom Tab is being launched by an Intent fired
     * by Chrome itself.
     */
    public static final String EXTRA_IS_OPENED_BY_CHROME =
            "org.chromium.chrome.browser.customtabs.IS_OPENED_BY_CHROME";

    /** URL that should be loaded in place of the URL passed along in the data. */
    public static final String EXTRA_MEDIA_VIEWER_URL =
            "org.chromium.chrome.browser.customtabs.MEDIA_VIEWER_URL";

    /** Extra that enables embedded media experience. */
    public static final String EXTRA_ENABLE_EMBEDDED_MEDIA_EXPERIENCE =
            "org.chromium.chrome.browser.customtabs.EXTRA_ENABLE_EMBEDDED_MEDIA_EXPERIENCE";

    /** Indicates the type of UI Custom Tab should use. */
    public static final String EXTRA_UI_TYPE =
            "org.chromium.chrome.browser.customtabs.EXTRA_UI_TYPE";

    /** Extra that defines the initial background color (RGB color stored as an integer). */
    public static final String EXTRA_INITIAL_BACKGROUND_COLOR =
            "org.chromium.chrome.browser.customtabs.EXTRA_INITIAL_BACKGROUND_COLOR";

    /** Extra that enables the client to disable the star button in menu. */
    public static final String EXTRA_DISABLE_STAR_BUTTON =
            "org.chromium.chrome.browser.customtabs.EXTRA_DISABLE_STAR_BUTTON";

    /** Extra that enables the client to disable the download button in menu. */
    public static final String EXTRA_DISABLE_DOWNLOAD_BUTTON =
            "org.chromium.chrome.browser.customtabs.EXTRA_DISABLE_DOWNLOAD_BUTTON";

    //TODO(yusufo): Move this to CustomTabsIntent.
    /** Signals custom tabs to favor sending initial urls to external handler apps if possible. */
    public static final String EXTRA_SEND_TO_EXTERNAL_DEFAULT_HANDLER =
            "android.support.customtabs.extra.SEND_TO_EXTERNAL_HANDLER";

    private static final int MAX_CUSTOM_MENU_ITEMS = 5;
    private static final String ANIMATION_BUNDLE_PREFIX =
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? "android:activity." : "android:";
    private static final String BUNDLE_PACKAGE_NAME = ANIMATION_BUNDLE_PREFIX + "packageName";
    private static final String BUNDLE_ENTER_ANIMATION_RESOURCE =
            ANIMATION_BUNDLE_PREFIX + "animEnterRes";
    private static final String BUNDLE_EXIT_ANIMATION_RESOURCE =
            ANIMATION_BUNDLE_PREFIX + "animExitRes";
    private static final String FIRST_PARTY_PITFALL_MSG =
            "The intent contains a non-default UI type, but it is not from a first-party app. "
            + "To make locally-built Chrome a first-party app, sign with release-test "
            + "signing keys and run on userdebug devices. See use_signing_keys GN arg.";

    private final CustomTabsSessionToken mSession;
    private final boolean mIsTrustedIntent;
    private final Intent mKeepAliveServiceIntent;
    private final int mUiType;
    private final int mTitleVisibilityState;
    private final String mMediaViewerUrl;
    private final boolean mEnableEmbeddedMediaExperience;
    private final int mInitialBackgroundColor;
    private final boolean mDisableStar;
    private final boolean mDisableDownload;

    private int mToolbarColor;
    private int mBottomBarColor;
    private boolean mEnableUrlBarHiding;
    private List<CustomButtonParams> mCustomButtonParams;
    private Drawable mCloseButtonIcon;
    private List<Pair<String, PendingIntent>> mMenuEntries = new ArrayList<>();
    private Bundle mAnimationBundle;
    private boolean mShowShareItem;
    private CustomButtonParams mToolbarButton;
    private List<CustomButtonParams> mBottombarButtons = new ArrayList<>(2);
    private RemoteViews mRemoteViews;
    private int[] mClickableViewIds;
    private PendingIntent mRemoteViewsPendingIntent;
    // OnFinished listener for PendingIntents. Used for testing only.
    private PendingIntent.OnFinished mOnFinished;

    /** Herb: Whether this CustomTabActivity was explicitly started by another Chrome Activity. */
    private boolean mIsOpenedByChrome;

    /**
     * Add extras to customize menu items for opening payment request UI custom tab from Chrome.
     */
    public static void addPaymentRequestUIExtras(Intent intent) {
        intent.putExtra(EXTRA_UI_TYPE, CUSTOM_TABS_UI_TYPE_PAYMENT_REQUEST);
        intent.putExtra(EXTRA_IS_OPENED_BY_CHROME, true);
        IntentHandler.addTrustedIntentExtras(intent);
    }

    /**
     * Add extras to customize menu items for opening Reader Mode UI custom tab from Chrome.
     */
    public static void addReaderModeUIExtras(Intent intent) {
        intent.putExtra(EXTRA_UI_TYPE, CUSTOM_TABS_UI_TYPE_READER_MODE);
        intent.putExtra(EXTRA_IS_OPENED_BY_CHROME, true);
        IntentHandler.addTrustedIntentExtras(intent);
    }

    /**
     * Constructs a {@link CustomTabIntentDataProvider}.
     */
    public CustomTabIntentDataProvider(Intent intent, Context context) {
        if (intent == null) assert false;
        mSession = CustomTabsSessionToken.getSessionTokenFromIntent(intent);
        mIsTrustedIntent = IntentHandler.isIntentChromeOrFirstParty(intent);

        retrieveCustomButtons(intent, context);
        retrieveToolbarColor(intent, context);
        retrieveBottomBarColor(intent);
        mInitialBackgroundColor = retrieveInitialBackgroundColor(intent);

        mEnableUrlBarHiding = IntentUtils.safeGetBooleanExtra(
                intent, CustomTabsIntent.EXTRA_ENABLE_URLBAR_HIDING, true);
        mKeepAliveServiceIntent = IntentUtils.safeGetParcelableExtra(intent, EXTRA_KEEP_ALIVE);

        Bitmap bitmap = IntentUtils.safeGetParcelableExtra(intent,
                CustomTabsIntent.EXTRA_CLOSE_BUTTON_ICON);
        if (bitmap != null && !checkCloseButtonSize(context, bitmap)) {
            IntentUtils.safeRemoveExtra(intent, CustomTabsIntent.EXTRA_CLOSE_BUTTON_ICON);
            bitmap.recycle();
            bitmap = null;
        }
        if (bitmap == null) {
            mCloseButtonIcon = TintedDrawable.constructTintedDrawable(context.getResources(),
                    R.drawable.btn_close);
        } else {
            mCloseButtonIcon = new BitmapDrawable(context.getResources(), bitmap);
        }

        List<Bundle> menuItems =
                IntentUtils.getParcelableArrayListExtra(intent, CustomTabsIntent.EXTRA_MENU_ITEMS);
        if (menuItems != null) {
            for (int i = 0; i < Math.min(MAX_CUSTOM_MENU_ITEMS, menuItems.size()); i++) {
                Bundle bundle = menuItems.get(i);
                String title =
                        IntentUtils.safeGetString(bundle, CustomTabsIntent.KEY_MENU_ITEM_TITLE);
                PendingIntent pendingIntent =
                        IntentUtils.safeGetParcelable(bundle, CustomTabsIntent.KEY_PENDING_INTENT);
                if (TextUtils.isEmpty(title) || pendingIntent == null) continue;
                mMenuEntries.add(new Pair<String, PendingIntent>(title, pendingIntent));
            }
        }

        mIsOpenedByChrome =
                IntentUtils.safeGetBooleanExtra(intent, EXTRA_IS_OPENED_BY_CHROME, false);

        final int requestedUiType =
                IntentUtils.safeGetIntExtra(intent, EXTRA_UI_TYPE, CUSTOM_TABS_UI_TYPE_DEFAULT);
        mUiType = verifiedUiType(requestedUiType, context);

        mAnimationBundle = IntentUtils.safeGetBundleExtra(
                intent, CustomTabsIntent.EXTRA_EXIT_ANIMATION_BUNDLE);
        mTitleVisibilityState = IntentUtils.safeGetIntExtra(intent,
                CustomTabsIntent.EXTRA_TITLE_VISIBILITY_STATE, CustomTabsIntent.NO_TITLE);
        mShowShareItem = IntentUtils.safeGetBooleanExtra(intent,
                CustomTabsIntent.EXTRA_DEFAULT_SHARE_MENU_ITEM, false);
        mRemoteViews = IntentUtils.safeGetParcelableExtra(intent,
                CustomTabsIntent.EXTRA_REMOTEVIEWS);
        mClickableViewIds = IntentUtils.safeGetIntArrayExtra(intent,
                CustomTabsIntent.EXTRA_REMOTEVIEWS_VIEW_IDS);
        mRemoteViewsPendingIntent = IntentUtils.safeGetParcelableExtra(intent,
                CustomTabsIntent.EXTRA_REMOTEVIEWS_PENDINGINTENT);
        mMediaViewerUrl = isMediaViewer()
                ? IntentUtils.safeGetStringExtra(intent, EXTRA_MEDIA_VIEWER_URL)
                : null;
        mEnableEmbeddedMediaExperience = mIsTrustedIntent
                && IntentUtils.safeGetBooleanExtra(
                           intent, EXTRA_ENABLE_EMBEDDED_MEDIA_EXPERIENCE, false);
        mDisableStar = IntentUtils.safeGetBooleanExtra(intent, EXTRA_DISABLE_STAR_BUTTON, false);
        mDisableDownload = IntentUtils.safeGetBooleanExtra(intent, EXTRA_DISABLE_DOWNLOAD_BUTTON,
                false);
    }

    /**
     * Get the verified UI type, according to the intent extras, and whether the intent is trusted.
     * @param requestedUiType requested UI type in the intent, unqualified
     * @return verified UI type
     */
    private int verifiedUiType(int requestedUiType, Context context) {
        if (!mIsTrustedIntent) {
            if (ChromeVersionInfo.isLocalBuild()) {
                Toast.makeText(context, FIRST_PARTY_PITFALL_MSG, Toast.LENGTH_LONG).show();
                Log.w(TAG, FIRST_PARTY_PITFALL_MSG);
            }
            return CUSTOM_TABS_UI_TYPE_DEFAULT;
        }

        if (requestedUiType == CUSTOM_TABS_UI_TYPE_PAYMENT_REQUEST) {
            if (!mIsOpenedByChrome) {
                return CUSTOM_TABS_UI_TYPE_DEFAULT;
            }
        }

        return requestedUiType;
    }

    /**
     * Gets custom buttons from the intent and updates {@link #mCustomButtonParams},
     * {@link #mBottombarButtons} and {@link #mToolbarButton}.
     */
    private void retrieveCustomButtons(Intent intent, Context context) {
        mCustomButtonParams = CustomButtonParams.fromIntent(context, intent);
        if (mCustomButtonParams != null) {
            for (CustomButtonParams params : mCustomButtonParams) {
                if (params.showOnToolbar()) {
                    mToolbarButton = params;
                } else {
                    mBottombarButtons.add(params);
                }
            }
        }
    }

    /**
     * Processes the color passed from the client app and updates {@link #mToolbarColor}.
     */
    private void retrieveToolbarColor(Intent intent, Context context) {
        int defaultColor = ApiCompatibilityUtils.getColor(context.getResources(),
                R.color.default_primary_color);
        int color = IntentUtils.safeGetIntExtra(intent, CustomTabsIntent.EXTRA_TOOLBAR_COLOR,
                defaultColor);
        mToolbarColor = removeTransparencyFromColor(color);
    }

    /**
     * Must be called after calling {@link #retrieveToolbarColor(Intent, Context)}.
     */
    private void retrieveBottomBarColor(Intent intent) {
        int defaultColor = mToolbarColor;
        int color = IntentUtils.safeGetIntExtra(intent,
                CustomTabsIntent.EXTRA_SECONDARY_TOOLBAR_COLOR, defaultColor);
        mBottomBarColor = removeTransparencyFromColor(color);
    }

    /**
     * Returns the color to initialize the background of the Custom Tab with.
     * If no valid color is set, Color.TRANSPARENT is returned.
     */
    private int retrieveInitialBackgroundColor(Intent intent) {
        int defaultColor = Color.TRANSPARENT;
        int color = IntentUtils.safeGetIntExtra(
                intent, EXTRA_INITIAL_BACKGROUND_COLOR, defaultColor);
        return color == Color.TRANSPARENT ? color : removeTransparencyFromColor(color);
    }

    /**
     * Removes the alpha channel of the given color and returns the processed value.
     */
    private int removeTransparencyFromColor(int color) {
        return color | 0xFF000000;
    }

    /**
     * @return The session specified in the intent, or null.
     */
    public CustomTabsSessionToken getSession() {
        return mSession;
    }

    /**
     * @return The keep alive service intent specified in the intent, or null.
     */
    public Intent getKeepAliveServiceIntent() {
        return mKeepAliveServiceIntent;
    }

    /**
     * @return Whether url bar hiding should be enabled in the custom tab. Default is false.
     */
    public boolean shouldEnableUrlBarHiding() {
        return mEnableUrlBarHiding;
    }

    /**
     * @return The toolbar color specified in the intent. Will return the color of
     *         default_primary_color, if not set in the intent.
     */
    public int getToolbarColor() {
        return mToolbarColor;
    }

    /**
     * @return The drawable of the icon of close button shown in the custom tab toolbar. If the
     *         client app provides an icon in valid size, use this icon; else return the default
     *         drawable.
     */
    public Drawable getCloseButtonDrawable() {
        return mCloseButtonIcon;
    }

    /**
     * @return The title visibility state for the toolbar.
     *         Default is {@link CustomTabsIntent#NO_TITLE}.
     */
    public int getTitleVisibilityState() {
        return mTitleVisibilityState;
    }

    /**
     * @return Whether the default share item should be shown in the menu.
     */
    public boolean shouldShowShareMenuItem() {
        return mShowShareItem;
    }

    /**
     * @return The params for the custom button that shows on the toolbar. If there is no applicable
     *         buttons, returns null.
     */
    public CustomButtonParams getCustomButtonOnToolbar() {
        return mToolbarButton;
    }

    /**
     * @return The list of params representing the buttons on the bottombar.
     */
    public List<CustomButtonParams> getCustomButtonsOnBottombar() {
        return mBottombarButtons;
    }

    /**
     * @return Whether the bottom bar should be shown.
     */
    public boolean shouldShowBottomBar() {
        return !mBottombarButtons.isEmpty() || mRemoteViews != null;
    }

    /**
     * @return The color of the bottom bar, or {@link #getToolbarColor()} if not specified.
     */
    public int getBottomBarColor() {
        return mBottomBarColor;
    }

    /**
     * @return The {@link RemoteViews} to show on the bottom bar, or null if the extra is not
     *         specified.
     */
    public RemoteViews getBottomBarRemoteViews() {
        return mRemoteViews;
    }

    /**
     * @return A array of {@link View} ids, of which the onClick event is handled by the custom tab.
     */
    public int[] getClickableViewIDs() {
        if (mClickableViewIds == null) return null;
        return mClickableViewIds.clone();
    }

    /**
     * @return The {@link PendingIntent} that is sent when the user clicks on the remote view.
     */
    public PendingIntent getRemoteViewsPendingIntent() {
        return mRemoteViewsPendingIntent;
    }

    /**
     * Gets params for all custom buttons, which is the combination of
     * {@link #getCustomButtonsOnBottombar()} and {@link #getCustomButtonOnToolbar()}.
     */
    public List<CustomButtonParams> getAllCustomButtons() {
        return mCustomButtonParams;
    }

    /**
     * @return The {@link CustomButtonParams} having the given id. Returns null if no such params
     *         can be found.
     */
    public CustomButtonParams getButtonParamsForId(int id) {
        for (CustomButtonParams params : mCustomButtonParams) {
            // A custom button params will always carry an ID. If the client calls updateVisuals()
            // without an id, we will assign the toolbar action button id to it.
            if (id == params.getId()) return params;
        }
        return null;
    }

    /**
     * @return Titles of menu items that were passed from client app via intent.
     */
    public List<String> getMenuTitles() {
        ArrayList<String> list = new ArrayList<>();
        for (Pair<String, PendingIntent> pair : mMenuEntries) {
            list.add(pair.first);
        }
        return list;
    }

    /**
     * Triggers the client-defined action when the user clicks a custom menu item.
     * @param menuIndex The index that the menu item is shown in the result of
     *                  {@link #getMenuTitles()}
     */
    public void clickMenuItemWithUrl(ChromeActivity activity, int menuIndex, String url) {
        Intent addedIntent = new Intent();
        addedIntent.setData(Uri.parse(url));
        try {
            // Media viewers pass in PendingIntents that contain CHOOSER Intents.  Setting the data
            // in these cases prevents the Intent from firing correctly.
            String title = mMenuEntries.get(menuIndex).first;
            PendingIntent pendingIntent = mMenuEntries.get(menuIndex).second;
            pendingIntent.send(
                    activity, 0, isMediaViewer() ? null : addedIntent, mOnFinished, null);
            if (shouldEnableEmbeddedMediaExperience()
                    && TextUtils.equals(
                               title, activity.getString(R.string.download_manager_open_with))) {
                RecordUserAction.record("CustomTabsMenuCustomMenuItem.DownloadsUI.OpenWith");
            }
        } catch (CanceledException e) {
            Log.e(TAG, "Custom tab in Chrome failed to send pending intent.");
        }
    }

    /**
     * @return Whether chrome should animate when it finishes. We show animations only if the client
     *         app has supplied the correct animation resources via intent extra.
     */
    public boolean shouldAnimateOnFinish() {
        return mAnimationBundle != null && getClientPackageName() != null;
    }

    /**
     * @return The package name of the client app. This is used for a workaround in order to
     *         retrieve the client's animation resources.
     */
    public String getClientPackageName() {
        if (mAnimationBundle == null) return null;
        return mAnimationBundle.getString(BUNDLE_PACKAGE_NAME);
    }

    /**
     * @return The resource id for enter animation, which is used in
     *         {@link Activity#overridePendingTransition(int, int)}.
     */
    public int getAnimationEnterRes() {
        return shouldAnimateOnFinish() ? mAnimationBundle.getInt(BUNDLE_ENTER_ANIMATION_RESOURCE)
                : 0;
    }

    /**
     * @return The resource id for exit animation, which is used in
     *         {@link Activity#overridePendingTransition(int, int)}.
     */
    public int getAnimationExitRes() {
        return shouldAnimateOnFinish() ? mAnimationBundle.getInt(BUNDLE_EXIT_ANIMATION_RESOURCE)
                : 0;
    }

    /**
     * Sends the pending intent for the custom button on toolbar with the given url as data.
     * @param context The context to use for sending the {@link PendingIntent}.
     * @param url The url to attach as additional data to the {@link PendingIntent}.
     */
    public void sendButtonPendingIntentWithUrl(Context context, String url) {
        Intent addedIntent = new Intent();
        addedIntent.setData(Uri.parse(url));
        try {
            getCustomButtonOnToolbar().getPendingIntent().send(context, 0, addedIntent, mOnFinished,
                    null);
        } catch (CanceledException e) {
            Log.e(TAG, "CanceledException while sending pending intent in custom tab");
        }
    }

    private boolean checkCloseButtonSize(Context context, Bitmap bitmap) {
        int size = context.getResources().getDimensionPixelSize(R.dimen.toolbar_icon_height);
        if (bitmap.getHeight() == size && bitmap.getWidth() == size) return true;
        return false;
    }

    /**
     * Set the callback object for {@link PendingIntent}s that are sent in this class. For testing
     * purpose only.
     */
    @VisibleForTesting
    void setPendingIntentOnFinishedForTesting(PendingIntent.OnFinished onFinished) {
        mOnFinished = onFinished;
    }

    /**
     * @return See {@link #EXTRA_IS_OPENED_BY_CHROME}.
     */
    boolean isOpenedByChrome() {
        return mIsOpenedByChrome;
    }

    /**
     * Checks whether or not the Intent is from Chrome or other trusted first party.
     */
    boolean isTrustedIntent() {
        return mIsTrustedIntent;
    }

    /**
     * @return See {@link #EXTRA_UI_TYPE}.
     */
    boolean isMediaViewer() {
        return mUiType == CUSTOM_TABS_UI_TYPE_MEDIA_VIEWER;
    }

    @CustomTabsUiType
    int getUiType() {
        return mUiType;
    }

    /**
     * @return See {@link #EXTRA_MEDIA_VIEWER_URL}.
     */
    String getMediaViewerUrl() {
        return mMediaViewerUrl;
    }

    /**
     * @return See {@link #EXTRA_ENABLE_EMBEDDED_MEDIA_EXPERIENCE}
     */
    boolean shouldEnableEmbeddedMediaExperience() {
        return mEnableEmbeddedMediaExperience;
    }

    /**
     * @return If the Custom Tab is an info page.
     * See {@link #EXTRA_UI_TYPE}.
     */
    boolean isInfoPage() {
        return mUiType == CUSTOM_TABS_UI_TYPE_INFO_PAGE;
    }

    /**
     * See {@link #EXTRA_INITIAL_BACKGROUND_COLOR}.
     * @return The color if it was specified in the Intent, Color.TRANSPARENT otherwise.
     */
    int getInitialBackgroundColor() {
        return mInitialBackgroundColor;
    }

    /**
     * @return Whether there should be a star button in the menu.
     */
    boolean shouldShowStarButton() {
        return !mDisableStar;
    }

    /**
     * @return Whether there should be a download button in the menu.
     */
    boolean shouldShowDownloadButton() {
        return !mDisableDownload;
    }
}
