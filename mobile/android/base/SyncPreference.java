/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.preference.Preference;
import android.util.AttributeSet;
import android.util.Log;

import org.mozilla.gecko.sync.setup.activities.SetupSyncActivity;
import org.mozilla.gecko.sync.setup.SyncAccounts;

class SyncPreference extends Preference {
    private Context mContext;

    public SyncPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
        mContext = context;
    }

    @Override
    protected void onClick() {
        // Make sure we use the same account type as our bundled version of Sync!
        final String accountType = org.mozilla.gecko.sync.setup.Constants.ACCOUNTTYPE_SYNC;

        // Show Sync setup if no accounts exist; otherwise, show account settings.
        Account[] accounts = AccountManager.get(mContext).getAccountsByType(accountType);
        if (accounts.length > 0) {
            SyncAccounts.openSyncSettings(mContext);
        } else {
            Intent intent = new Intent(mContext, SetupSyncActivity.class);
            mContext.startActivity(intent);
        }
    }
}
