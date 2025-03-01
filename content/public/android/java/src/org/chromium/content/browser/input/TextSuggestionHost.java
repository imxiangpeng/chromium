// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.input;

import android.content.Context;
import android.view.View;

import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.content.browser.RenderCoordinates;
import org.chromium.content.browser.WindowAndroidProvider;
import org.chromium.content_public.browser.WebContents;

/**
 * Handles displaying the Android spellcheck/text suggestion menu (provided by
 * SuggestionsPopupWindow) when requested by the C++ class TextSuggestionHostAndroid and applying
 * the commands in that menu (by calling back to the C++ class).
 */
@JNINamespace("content")
public class TextSuggestionHost {
    private long mNativeTextSuggestionHost;
    private final Context mContext;
    private final WebContents mWebContents;
    private final View mContainerView;
    private final WindowAndroidProvider mWindowAndroidProvider;
    private final RenderCoordinates mRenderCoordinates;

    private SuggestionsPopupWindow mSuggestionsPopupWindow;

    public TextSuggestionHost(Context context, WebContents webContents, View containerView,
            WindowAndroidProvider windowAndroidProvider, RenderCoordinates renderCoordinates) {
        mContext = context;
        mWebContents = webContents;
        mContainerView = containerView;
        mWindowAndroidProvider = windowAndroidProvider;
        mRenderCoordinates = renderCoordinates;

        mNativeTextSuggestionHost = nativeInit(webContents);
    }

    @CalledByNative
    private void showSpellCheckSuggestionMenu(
            double caretX, double caretY, String markedText, String[] suggestions) {
        if (mSuggestionsPopupWindow == null) {
            mSuggestionsPopupWindow = new SuggestionsPopupWindow(
                    mContext, this, mContainerView, mWindowAndroidProvider);
        }

        mSuggestionsPopupWindow.setHighlightedText(markedText);
        mSuggestionsPopupWindow.setSpellCheckSuggestions(suggestions);

        float density = mRenderCoordinates.getDeviceScaleFactor();
        mSuggestionsPopupWindow.show(
                density * caretX, density * caretY + mRenderCoordinates.getContentOffsetYPix());
    }

    /**
     * Hides the text suggestion menu (and informs Blink that it was closed).
     */
    @CalledByNative
    public void hidePopups() {
        if (mSuggestionsPopupWindow != null && mSuggestionsPopupWindow.isShowing()) {
            mSuggestionsPopupWindow.dismiss();
            mSuggestionsPopupWindow = null;
        }
    }

    /**
     * Tells Blink to replace the active suggestion range with the specified replacement.
     */
    public void applySpellCheckSuggestion(String suggestion) {
        nativeApplySpellCheckSuggestion(mNativeTextSuggestionHost, suggestion);
    }

    /**
     * Tells Blink to delete the active suggestion range.
     */
    public void deleteActiveSuggestionRange() {
        nativeDeleteActiveSuggestionRange(mNativeTextSuggestionHost);
    }

    /**
     * Tells Blink to remove spelling markers under all instances of the specified word.
     */
    public void newWordAddedToDictionary(String word) {
        nativeNewWordAddedToDictionary(mNativeTextSuggestionHost, word);
    }

    /**
     * Tells Blink the suggestion menu was closed (and also clears the reference to the
     * SuggestionsPopupWindow instance so it can be garbage collected).
     */
    public void suggestionMenuClosed(boolean dismissedByItemTap) {
        if (!dismissedByItemTap) {
            nativeSuggestionMenuClosed(mNativeTextSuggestionHost);
        }
        mSuggestionsPopupWindow = null;
    }

    @CalledByNative
    private void destroy() {
        mNativeTextSuggestionHost = 0;
    }

    /**
     * @return The SuggestionsPopupWindow, if one exists.
     */
    @VisibleForTesting
    public SuggestionsPopupWindow getSuggestionsPopupWindowForTesting() {
        return mSuggestionsPopupWindow;
    }

    private native long nativeInit(WebContents webContents);
    private native void nativeApplySpellCheckSuggestion(
            long nativeTextSuggestionHostAndroid, String suggestion);
    private native void nativeDeleteActiveSuggestionRange(long nativeTextSuggestionHostAndroid);
    private native void nativeNewWordAddedToDictionary(
            long nativeTextSuggestionHostAndroid, String word);
    private native void nativeSuggestionMenuClosed(long nativeTextSuggestionHostAndroid);
}
