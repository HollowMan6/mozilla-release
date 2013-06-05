/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import org.mozilla.gecko.GeckoPreferences;
import org.mozilla.gecko.GeckoPreferenceFragment;
import org.mozilla.gecko.util.ThreadUtils;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Bundle;
import android.preference.PreferenceActivity;
import android.support.v4.app.NotificationCompat;
import android.support.v4.app.NotificationCompat.Builder;
import android.util.Log;

public class DataReportingNotification {

    private static final String LOGTAG = "DataReportNotification";

    public static final String ALERT_NAME_DATAREPORTING_NOTIFICATION = "datareporting-notification";

    private static final String DEFAULT_PREFS_BRANCH = AppConstants.ANDROID_PACKAGE_NAME + "_preferences";
    private static final String PREFS_POLICY_NOTIFIED_TIME = "datareporting.policy.dataSubmissionPolicyNotifiedTime";
    private static final String PREFS_POLICY_VERSION = "datareporting.policy.dataSubmissionPolicyVersion";
    private static final int DATA_REPORTING_VERSION = 1;

    public static void checkAndNotifyPolicy(Context context) {
        SharedPreferences dataPrefs = context.getSharedPreferences(DEFAULT_PREFS_BRANCH, 0);

        // Notify if user has not been notified or if policy version has changed.
        if ((!dataPrefs.contains(PREFS_POLICY_NOTIFIED_TIME)) ||
            (DATA_REPORTING_VERSION != dataPrefs.getInt(PREFS_POLICY_VERSION, -1))) {

            // Launch Data Choices fragment when notification is clicked.
            Intent prefIntent = new Intent(context, GeckoPreferences.class);

            // Build launch intent based on Android version.
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.HONEYCOMB) {
                prefIntent.putExtra("resource", "preferences_datareporting");
            } else {
                prefIntent.putExtra(PreferenceActivity.EXTRA_SHOW_FRAGMENT, GeckoPreferenceFragment.class.getName());

                Bundle fragmentArgs = new Bundle();
                fragmentArgs.putString("resource", "preferences_datareporting");
                prefIntent.putExtra(PreferenceActivity.EXTRA_SHOW_FRAGMENT_ARGUMENTS, fragmentArgs);
            }
            prefIntent.putExtra(ALERT_NAME_DATAREPORTING_NOTIFICATION, true);

            PendingIntent contentIntent = PendingIntent.getActivity(context, 0, prefIntent, PendingIntent.FLAG_UPDATE_CURRENT);

            // Create and send notification.
            String notificationTitle = context.getResources().getString(R.string.datareporting_notification_title);
            String notificationSummary;
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN) {
              notificationSummary = context.getResources().getString(R.string.datareporting_notification_action);
            } else {
              // Display partial version of Big Style notification for supporting devices.
              notificationSummary = context.getResources().getString(R.string.datareporting_notification_summary_short);
            }
            String notificationAction = context.getResources().getString(R.string.datareporting_notification_action);
            String notificationBigSummary = context.getResources().getString(R.string.datareporting_notification_summary);

            Notification notification = new NotificationCompat.Builder(context)
                                        .setContentTitle(notificationTitle)
                                        .setContentText(notificationSummary)
                                        .setSmallIcon(R.drawable.ic_status_logo)
                                        .setAutoCancel(true)
                                        .setContentIntent(contentIntent)
                                        .setStyle(new NotificationCompat.BigTextStyle()
                                                                        .bigText(notificationBigSummary))
                                        .addAction(R.drawable.firefox_settings_alert, notificationAction, contentIntent)
                                        .build();

            NotificationManager notificationManager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
            int notificationID = ALERT_NAME_DATAREPORTING_NOTIFICATION.hashCode();
            notificationManager.notify(notificationID, notification);

            // Record version and notification time.
            SharedPreferences.Editor editor = dataPrefs.edit();
            long now = System.currentTimeMillis();
            editor.putLong(PREFS_POLICY_NOTIFIED_TIME, now);
            editor.putInt(PREFS_POLICY_VERSION, DATA_REPORTING_VERSION);

            // If healthreport is enabled, set default preference value.
            if (AppConstants.MOZ_SERVICES_HEALTHREPORT) {
                editor.putBoolean(GeckoPreferences.PREFS_HEALTHREPORT_UPLOAD_ENABLED, true);
            }

            editor.commit();
        }
    }
}
