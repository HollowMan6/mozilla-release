/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;

public class NotificationService extends Service {
    private final IBinder mBinder = new NotificationBinder();
    private final NotificationHandler mHandler = new NotificationHandler(this) {
        @Override
        protected void setForegroundNotification(AlertNotification notification) {
            super.setForegroundNotification(notification);

            if (notification == null) {
                stopForeground(true);
            } else {
                startForeground(notification.getId(), notification);
            }
        }
    };

    public class NotificationBinder extends Binder {
        NotificationService getService() {
            // Return this instance of NotificationService so clients can call public methods
            return NotificationService.this;
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mBinder;
    }

    public NotificationHandler getNotificationHandler() {
        return mHandler;
    }
}
