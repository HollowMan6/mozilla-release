/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

const APP_UPDATE_URL_PREF = "app.update.url";
const REPLACE_KEY = "%OS_VERSION%";

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/osfile.jsm");
Cu.import("resource://gre/modules/TelemetryLog.jsm");

function startup() {
  if (Services.appinfo.OS != "WINNT") {
    return;
  }

  try {
    let windir = Services.dirsvc.get("WinD", Ci.nsILocalFile).path;

    let file1 = OS.Path.join(windir, "System32\\qipcap.dll");
    let file2 = OS.Path.join(windir, "System32\\qipcap64.dll");
    let file3 = OS.Path.join(windir, "sysnative\\qipcap.dll");
    let file4 = OS.Path.join(windir, "sysnative\\qipcap64.dll");

    let existPromises = [
      OS.File.exists(file1),
      OS.File.exists(file2),
      OS.File.exists(file3),
      OS.File.exists(file4)
    ];

    // Update the app.update.url pref to include a special mark
    // telling whether any of those files are present or not.
    Promise.all(existPromises).then(results => {
      let newValue = "(nowebsense)";

      if (results.some(value => value == true)) {
        newValue = "(websense)";
      }

      let branch = Services.prefs.getDefaultBranch("");
      let curValue = branch.getCharPref(APP_UPDATE_URL_PREF);
      newValue = curValue.replace(REPLACE_KEY + "/", REPLACE_KEY + newValue + "/");
      branch.setCharPref(APP_UPDATE_URL_PREF, newValue);

    }, reject => {
      Cu.reportError("Hotfix unexpected error: " + reject);
      TelemetryLog.log("WEBSENSE_PROMISE_ERROR", [reject]);
    });
  } catch (ex) {
    Cu.reportError(ex);
    TelemetryLog.log("WEBSENSE_DETECTION_ERROR", [ex]);
  }
}

function shutdown() {}
function install() {}
function uninstall() {}
