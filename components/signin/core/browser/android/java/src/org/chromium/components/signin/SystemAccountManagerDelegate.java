// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.signin;

import android.Manifest;
import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AccountManagerCallback;
import android.accounts.AccountManagerFuture;
import android.accounts.AuthenticatorDescription;
import android.accounts.AuthenticatorException;
import android.accounts.OperationCanceledException;
import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.os.SystemClock;

import com.google.android.gms.auth.GoogleAuthException;
import com.google.android.gms.auth.GoogleAuthUtil;
import com.google.android.gms.auth.GooglePlayServicesAvailabilityException;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ObserverList;
import org.chromium.base.ThreadUtils;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.metrics.RecordHistogram;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

/**
 * Default implementation of {@link AccountManagerDelegate} which delegates all calls to the
 * Android account manager.
 */
@MainDex
public class SystemAccountManagerDelegate implements AccountManagerDelegate {
    private final AccountManager mAccountManager;
    private final ObserverList<AccountsChangeObserver> mObservers = new ObserverList<>();

    private static final String TAG = "Auth";

    public SystemAccountManagerDelegate() {
        mAccountManager = AccountManager.get(ContextUtils.getApplicationContext());

        BroadcastReceiver accountsChangedBroadcastReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(final Context context, final Intent intent) {
                fireOnAccountsChangedNotification();
            }
        };
        IntentFilter accountsChangedIntentFilter = new IntentFilter();
        accountsChangedIntentFilter.addAction(AccountManager.LOGIN_ACCOUNTS_CHANGED_ACTION);
        ContextUtils.getApplicationContext().registerReceiver(
                accountsChangedBroadcastReceiver, accountsChangedIntentFilter);
    }

    protected void checkCanUseGooglePlayServices() throws AccountManagerDelegateException {
        Context context = ContextUtils.getApplicationContext();
        final int resultCode =
                GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(context);
        if (resultCode == ConnectionResult.SUCCESS) {
            return;
        }

        if (GoogleApiAvailability.getInstance().isUserResolvableError(resultCode)) {
            throw new GmsAvailabilityException(
                    String.format("Can't use Google Play Services: %s",
                            GoogleApiAvailability.getInstance().getErrorString(resultCode)),
                    resultCode);
        }
    }

    @Override
    public void addObserver(AccountsChangeObserver observer) {
        mObservers.addObserver(observer);
    }

    @Override
    public void removeObserver(AccountsChangeObserver observer) {
        mObservers.removeObserver(observer);
    }

    @Override
    public Account[] getAccountsSync() throws AccountManagerDelegateException {
        // Account seeding relies on GoogleAuthUtil.getAccountId to get GAIA ids,
        // so don't report any accounts if Google Play Services are out of date.
        checkCanUseGooglePlayServices();

        if (!hasGetAccountsPermission()) {
            return new Account[] {};
        }
        long now = SystemClock.elapsedRealtime();
        Account[] accounts = mAccountManager.getAccountsByType(GoogleAuthUtil.GOOGLE_ACCOUNT_TYPE);
        long elapsed = SystemClock.elapsedRealtime() - now;
        recordElapsedTimeHistogram("Signin.AndroidGetAccountsTime_AccountManager", elapsed);
        if (ThreadUtils.runningOnUiThread()) {
            recordElapsedTimeHistogram(
                    "Signin.AndroidGetAccountsTimeUiThread_AccountManager", elapsed);
        }
        return accounts;
    }

    @Override
    public String getAuthToken(Account account, String authTokenScope) throws AuthException {
        assert !ThreadUtils.runningOnUiThread();
        assert AccountManagerFacade.GOOGLE_ACCOUNT_TYPE.equals(account.type);
        try {
            return GoogleAuthUtil.getTokenWithNotification(
                    ContextUtils.getApplicationContext(), account, authTokenScope, null);
        } catch (GoogleAuthException ex) {
            // This case includes a UserRecoverableNotifiedException, but most clients will have
            // their own retry mechanism anyway.
            throw new AuthException(AuthException.NONTRANSIENT,
                    "Error while getting token for scope '" + authTokenScope + "'", ex);
        } catch (IOException ex) {
            throw new AuthException(AuthException.TRANSIENT, ex);
        }
    }

    @Override
    public void invalidateAuthToken(String authToken) throws AuthException {
        try {
            GoogleAuthUtil.clearToken(ContextUtils.getApplicationContext(), authToken);
        } catch (GooglePlayServicesAvailabilityException ex) {
            throw new AuthException(AuthException.NONTRANSIENT, ex);
        } catch (GoogleAuthException ex) {
            throw new AuthException(AuthException.NONTRANSIENT, ex);
        } catch (IOException ex) {
            throw new AuthException(AuthException.TRANSIENT, ex);
        }
    }

    @Override
    public AuthenticatorDescription[] getAuthenticatorTypes() {
        return mAccountManager.getAuthenticatorTypes();
    }

    @Override
    public boolean hasFeatures(Account account, String[] features) {
        if (!hasGetAccountsPermission()) {
            return false;
        }
        try {
            return mAccountManager.hasFeatures(account, features, null, null).getResult();
        } catch (AuthenticatorException | IOException e) {
            Log.e(TAG, "Error while checking features: ", e);
        } catch (OperationCanceledException e) {
            Log.e(TAG, "Checking features was cancelled. This should not happen.");
        }
        return false;
    }

    /**
     * Records a histogram value for how long time an action has taken using
     * {@link RecordHistogram#recordTimesHistogram(String, long, TimeUnit))} iff the browser
     * process has been initialized.
     *
     * @param histogramName the name of the histogram.
     * @param elapsedMs the elapsed time in milliseconds.
     */
    protected static void recordElapsedTimeHistogram(String histogramName, long elapsedMs) {
        if (!LibraryLoader.isInitialized()) return;
        RecordHistogram.recordTimesHistogram(histogramName, elapsedMs, TimeUnit.MILLISECONDS);
    }

    @Override
    public void updateCredentials(
            Account account, Activity activity, final Callback<Boolean> callback) {
        ThreadUtils.assertOnUiThread();
        if (!hasManageAccountsPermission()) {
            if (callback != null) {
                ThreadUtils.postOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        callback.onResult(false);
                    }
                });
            }
            return;
        }

        AccountManagerCallback<Bundle> realCallback = new AccountManagerCallback<Bundle>() {
            @Override
            public void run(AccountManagerFuture<Bundle> future) {
                Bundle bundle = null;
                try {
                    bundle = future.getResult();
                } catch (AuthenticatorException | IOException e) {
                    Log.e(TAG, "Error while update credentials: ", e);
                } catch (OperationCanceledException e) {
                    Log.w(TAG, "Updating credentials was cancelled.");
                }
                boolean success = bundle != null
                        && bundle.getString(AccountManager.KEY_ACCOUNT_TYPE) != null;
                if (callback != null) {
                    callback.onResult(success);
                }
            }
        };
        // Android 4.4 throws NullPointerException if null is passed
        Bundle emptyOptions = new Bundle();
        mAccountManager.updateCredentials(
                account, "android", emptyOptions, activity, realCallback, null);
    }

    protected boolean hasGetAccountsPermission() {
        return ApiCompatibilityUtils.checkPermission(ContextUtils.getApplicationContext(),
                       Manifest.permission.GET_ACCOUNTS, Process.myPid(), Process.myUid())
                == PackageManager.PERMISSION_GRANTED;
    }

    protected boolean hasManageAccountsPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return true;
        }
        return ApiCompatibilityUtils.checkPermission(ContextUtils.getApplicationContext(),
                       "android.permission.MANAGE_ACCOUNTS", Process.myPid(), Process.myUid())
                == PackageManager.PERMISSION_GRANTED;
    }

    private void fireOnAccountsChangedNotification() {
        for (AccountsChangeObserver observer : mObservers) {
            observer.onAccountsChanged();
        }
    }
}
