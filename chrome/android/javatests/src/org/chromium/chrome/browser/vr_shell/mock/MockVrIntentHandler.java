// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell.mock;

import android.content.Intent;

import org.chromium.chrome.browser.vr_shell.VrIntentHandler;

/**
 * Mock version of VrIntentHandler for testing.
 */
public class MockVrIntentHandler extends VrIntentHandler {
    private boolean mUseMockImplementation;
    private boolean mTreatIntentsAsTrusted;

    public MockVrIntentHandler(boolean useMockImplementation, boolean treatIntentsAsTrusted) {
        mUseMockImplementation = useMockImplementation;
        mTreatIntentsAsTrusted = treatIntentsAsTrusted;
    }

    @Override
    public boolean isTrustedDaydreamIntent(Intent intent) {
        if (mUseMockImplementation) {
            return mTreatIntentsAsTrusted;
        }
        return super.isTrustedDaydreamIntent(intent);
    }

    public void setUseMockImplementation(boolean enabled) {
        mUseMockImplementation = enabled;
    }

    public void setTreatIntentsAsTrusted(boolean trusted) {
        mTreatIntentsAsTrusted = trusted;
    }
}
