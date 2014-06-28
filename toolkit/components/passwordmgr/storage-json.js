/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* vim: set sw=4 ts=4 et lcs=trail\:.,tab\:>~ : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * nsILoginManagerStorage implementation for the JSON back-end.
 */

////////////////////////////////////////////////////////////////////////////////
//// Globals

"use strict";

const { classes: Cc, interfaces: Ci, utils: Cu, results: Cr } = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "LoginHelper",
                                  "resource://gre/modules/LoginHelper.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "LoginImport",
                                  "resource://gre/modules/LoginImport.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "LoginStore",
                                  "resource://gre/modules/LoginStore.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "OS",
                                  "resource://gre/modules/osfile.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "gUUIDGenerator",
                                   "@mozilla.org/uuid-generator;1",
                                   "nsIUUIDGenerator");

////////////////////////////////////////////////////////////////////////////////
//// LoginManagerStorage_json

this.LoginManagerStorage_json = function () {}

this.LoginManagerStorage_json.prototype = {
    classID : Components.ID("{c00c432d-a0c9-46d7-bef6-9c45b4d07341}"),
    QueryInterface : XPCOMUtils.generateQI([Ci.nsILoginManagerStorage]),

    __crypto : null,  // nsILoginManagerCrypto service
    get _crypto() {
        if (!this.__crypto)
            this.__crypto = Cc["@mozilla.org/login-manager/crypto/SDR;1"].
                            getService(Ci.nsILoginManagerCrypto);
        return this.__crypto;
    },

    /*
     * log
     *
     * Internal function for logging debug messages to the Error Console.
     */
    log : function (message) {
        if (!this._debug)
            return;
        dump("PwMgr json: " + message + "\n");
        Services.console.logStringMessage("PwMgr json: " + message);
    },
    _debug : false,


    /*
     * initialize
     *
     */
    initialize : function () {
        this._debug = Services.prefs.getBoolPref("signon.debug");

        try {
            // Force initialization of the crypto module.
            // See bug 717490 comment 17.
            this._crypto;

            // Set the reference to LoginStore synchronously.
            let jsonPath = OS.Path.join(OS.Constants.Path.profileDir,
                                        "logins.json");
            this._store = new LoginStore(jsonPath);

            return Task.spawn(function () {
                // Load the data asynchronously.
                this.log("Opening database at " + this._store.path);
                yield this._store.load();

                // The import from previous versions operates the first time
                // that this built-in storage back-end is used.  This may be
                // later than expected, in case add-ons have registered an
                // alternate storage that disabled the default one.
                try {
                    if (Services.prefs.getBoolPref("signon.importedFromSqlite")) {
                        return;
                    }
                } catch (ex) {
                    // If the preference does not exist, we need to import.
                }

                // Import only happens asynchronously.
                let sqlitePath = OS.Path.join(OS.Constants.Path.profileDir,
                                              "signons.sqlite");
                if (yield OS.File.exists(sqlitePath)) {
                    let loginImport = new LoginImport(this._store, sqlitePath);
                    // Failures during import, for example due to a corrupt
                    // file or a schema version that is too old, will not
                    // prevent us from marking the operation as completed.
                    // At the next startup, we will not try the import again.
                    yield loginImport.import().catch(Cu.reportError);
                    this._store.saveSoon();
                }

                // We won't attempt import again on next startup.
                Services.prefs.setBoolPref("signon.importedFromSqlite", true);
            }.bind(this)).catch(Cu.reportError);
        } catch (e) {
            this.log("Initialization failed: " + e);
            throw "Initialization failed";
        }
    },


    /*
     * terminate
     *
     * Internal method used by regression tests only.  It is called before
     * replacing this storage module with a new instance.
     */
    terminate : function () {
        this._store._saver.disarm();
        return this._store.save();
    },


    /*
     * addLogin
     *
     */
    addLogin : function (login) {
        this._store.ensureDataReady();

        // Throws if there are bogus values.
        LoginHelper.checkLoginValues(login);

        let [encUsername, encPassword, encType] = this._encryptLogin(login);

        // Clone the login, so we don't modify the caller's object.
        let loginClone = login.clone();

        // Initialize the nsILoginMetaInfo fields, unless the caller gave us values
        loginClone.QueryInterface(Ci.nsILoginMetaInfo);
        if (loginClone.guid) {
            if (!this._isGuidUnique(loginClone.guid))
                throw "specified GUID already exists";
        } else {
            loginClone.guid = gUUIDGenerator.generateUUID().toString();
        }

        // Set timestamps
        let currentTime = Date.now();
        if (!loginClone.timeCreated)
            loginClone.timeCreated = currentTime;
        if (!loginClone.timeLastUsed)
            loginClone.timeLastUsed = currentTime;
        if (!loginClone.timePasswordChanged)
            loginClone.timePasswordChanged = currentTime;
        if (!loginClone.timesUsed)
            loginClone.timesUsed = 1;

        this._store.data.logins.push({
            id:                  this._store.data.nextId++,
            hostname:            loginClone.hostname,
            httpRealm:           loginClone.httpRealm,
            formSubmitURL:       loginClone.formSubmitURL,
            usernameField:       loginClone.usernameField,
            passwordField:       loginClone.passwordField,
            encryptedUsername:   encUsername,
            encryptedPassword:   encPassword,
            guid:                loginClone.guid,
            encType:             encType,
            timeCreated:         loginClone.timeCreated,
            timeLastUsed:        loginClone.timeLastUsed,
            timePasswordChanged: loginClone.timePasswordChanged,
            timesUsed:           loginClone.timesUsed
        });
        this._store.saveSoon();

        // Send a notification that a login was added.
        this._sendNotification("addLogin", loginClone);
    },


    /*
     * removeLogin
     *
     */
    removeLogin : function (login) {
        this._store.ensureDataReady();

        let [idToDelete, storedLogin] = this._getIdForLogin(login);
        if (!idToDelete)
            throw "No matching logins";

        let foundIndex = this._store.data.logins.findIndex(l => l.id == idToDelete);
        if (foundIndex != -1) {
            this._store.data.logins.splice(foundIndex, 1);
            this._store.saveSoon();
        }

        this._sendNotification("removeLogin", storedLogin);
    },


    /*
     * modifyLogin
     *
     */
    modifyLogin : function (oldLogin, newLoginData) {
        this._store.ensureDataReady();

        let [idToModify, oldStoredLogin] = this._getIdForLogin(oldLogin);
        if (!idToModify)
            throw "No matching logins";

        let newLogin = LoginHelper.buildModifiedLogin(oldStoredLogin, newLoginData);

        // Check if the new GUID is duplicate.
        if (newLogin.guid != oldStoredLogin.guid &&
            !this._isGuidUnique(newLogin.guid)) 
        {
            throw "specified GUID already exists";
        }

        // Look for an existing entry in case key properties changed.
        if (!newLogin.matches(oldLogin, true)) {
            let logins = this.findLogins({}, newLogin.hostname,
                                         newLogin.formSubmitURL,
                                         newLogin.httpRealm);

            if (logins.some(login => newLogin.matches(login, true)))
                throw "This login already exists.";
        }

        // Get the encrypted value of the username and password.
        let [encUsername, encPassword, encType] = this._encryptLogin(newLogin);

        for (let loginItem of this._store.data.logins) {
            if (loginItem.id == idToModify) {
                loginItem.hostname = newLogin.hostname;
                loginItem.httpRealm = newLogin.httpRealm;
                loginItem.formSubmitURL = newLogin.formSubmitURL;
                loginItem.usernameField = newLogin.usernameField;
                loginItem.passwordField = newLogin.passwordField;
                loginItem.encryptedUsername = encUsername;
                loginItem.encryptedPassword = encPassword;
                loginItem.guid = newLogin.guid;
                loginItem.encType = encType;
                loginItem.timeCreated = newLogin.timeCreated;
                loginItem.timeLastUsed = newLogin.timeLastUsed;
                loginItem.timePasswordChanged = newLogin.timePasswordChanged;
                loginItem.timesUsed = newLogin.timesUsed;
                this._store.saveSoon();
                break;
            }
        }

        this._sendNotification("modifyLogin", [oldStoredLogin, newLogin]);
    },


    /*
     * getAllLogins
     *
     * Returns an array of nsILoginInfo.
     */
    getAllLogins : function (count) {
        let [logins, ids] = this._searchLogins({});

        // decrypt entries for caller.
        logins = this._decryptLogins(logins);

        this.log("_getAllLogins: returning " + logins.length + " logins.");
        if (count)
            count.value = logins.length; // needed for XPCOM
        return logins;
    },


    /*
     * searchLogins
     *
     * Public wrapper around _searchLogins to convert the nsIPropertyBag to a
     * JavaScript object and decrypt the results.
     *
     * Returns an array of decrypted nsILoginInfo.
     */
    searchLogins : function(count, matchData) {
        let realMatchData = {};
        // Convert nsIPropertyBag to normal JS object
        let propEnum = matchData.enumerator;
        while (propEnum.hasMoreElements()) {
            let prop = propEnum.getNext().QueryInterface(Ci.nsIProperty);
            realMatchData[prop.name] = prop.value;
        }

        let [logins, ids] = this._searchLogins(realMatchData);

        // Decrypt entries found for the caller.
        logins = this._decryptLogins(logins);

        count.value = logins.length; // needed for XPCOM
        return logins;
    },


    /*
     * _searchLogins
     *
     * Private method to perform arbitrary searches on any field. Decryption is
     * left to the caller.
     *
     * Returns [logins, ids] for logins that match the arguments, where logins
     * is an array of encrypted nsLoginInfo and ids is an array of associated
     * ids in the database.
     */
    _searchLogins : function (matchData) {
        this._store.ensureDataReady();

        let conditions = [];

        function match(aLogin) {
            for (let field in matchData) {
                let value = matchData[field];
                switch (field) {
                    // Historical compatibility requires this special case
                    case "formSubmitURL":
                        if (value != null) {
                            if (aLogin.formSubmitURL != "" && aLogin.formSubmitURL != value) {
                                return false;
                            }
                            break;
                        }
                    // Normal cases.
                    case "hostname":
                    case "httpRealm":
                    case "id":
                    case "usernameField":
                    case "passwordField":
                    case "encryptedUsername":
                    case "encryptedPassword":
                    case "guid":
                    case "encType":
                    case "timeCreated":
                    case "timeLastUsed":
                    case "timePasswordChanged":
                    case "timesUsed":
                        if (value == null && aLogin[field]) {
                            return false;
                        } else if (aLogin[field] != value) {
                            return false;
                        }
                        break;
                    // Fail if caller requests an unknown property.
                    default:
                        throw "Unexpected field: " + field;
                }
            }
            return true;
        }

        let foundLogins = [], foundIds = [];
        for (let loginItem of this._store.data.logins) {
            if (match(loginItem)) {
                // Create the new nsLoginInfo object, push to array
                let login = Cc["@mozilla.org/login-manager/loginInfo;1"].
                            createInstance(Ci.nsILoginInfo);
                login.init(loginItem.hostname, loginItem.formSubmitURL,
                           loginItem.httpRealm, loginItem.encryptedUsername,
                           loginItem.encryptedPassword, loginItem.usernameField,
                           loginItem.passwordField);
                // set nsILoginMetaInfo values
                login.QueryInterface(Ci.nsILoginMetaInfo);
                login.guid = loginItem.guid;
                login.timeCreated = loginItem.timeCreated;
                login.timeLastUsed = loginItem.timeLastUsed;
                login.timePasswordChanged = loginItem.timePasswordChanged;
                login.timesUsed = loginItem.timesUsed;
                foundLogins.push(login);
                foundIds.push(loginItem.id);
            }
        }

        this.log("_searchLogins: returning " + foundLogins.length + " logins");
        return [foundLogins, foundIds];
    },

    /*
     * removeAllLogins
     *
     * Removes all logins from storage.
     *
     * Disabled hosts are kept, as one presumably doesn't want to erase those.
     */
    removeAllLogins : function () {
        this._store.ensureDataReady();

        this.log("Removing all logins");
        this._store.data.logins = [];
        this._store.saveSoon();

        this._sendNotification("removeAllLogins", null);
    },


    /*
     * getAllDisabledHosts
     *
     */
    getAllDisabledHosts : function (count) {
        this._store.ensureDataReady();

        let disabledHosts = this._store.data.disabledHosts.slice(0);

        this.log("_getAllDisabledHosts: returning " + disabledHosts.length + " disabled hosts.");
        if (count)
            count.value = disabledHosts.length; // needed for XPCOM
        return disabledHosts;
    },


    /*
     * getLoginSavingEnabled
     *
     */
    getLoginSavingEnabled : function (hostname) {
        this._store.ensureDataReady();

        this.log("Getting login saving is enabled for " + hostname);
        return this._store.data.disabledHosts.indexOf(hostname) == -1;
    },


    /*
     * setLoginSavingEnabled
     *
     */
    setLoginSavingEnabled : function (hostname, enabled) {
        this._store.ensureDataReady();

        // Throws if there are bogus values.
        LoginHelper.checkHostnameValue(hostname);

        this.log("Setting login saving enabled for " + hostname + " to " + enabled);
        let foundIndex = this._store.data.disabledHosts.indexOf(hostname);
        if (enabled) {
            if (foundIndex != -1) {
                this._store.data.disabledHosts.splice(foundIndex, 1);
                this._store.saveSoon();
            }
        } else {
            if (foundIndex == -1) {
                this._store.data.disabledHosts.push(hostname);
                this._store.saveSoon();
            }
        }

        this._sendNotification(enabled ? "hostSavingEnabled" : "hostSavingDisabled", hostname);
    },


    /*
     * findLogins
     *
     */
    findLogins : function (count, hostname, formSubmitURL, httpRealm) {
        let loginData = {
            hostname: hostname,
            formSubmitURL: formSubmitURL,
            httpRealm: httpRealm
        };
        let matchData = { };
        for each (let field in ["hostname", "formSubmitURL", "httpRealm"])
            if (loginData[field] != '')
                matchData[field] = loginData[field];
        let [logins, ids] = this._searchLogins(matchData);

        // Decrypt entries found for the caller.
        logins = this._decryptLogins(logins);

        this.log("_findLogins: returning " + logins.length + " logins");
        count.value = logins.length; // needed for XPCOM
        return logins;
    },


    /*
     * countLogins
     *
     */
    countLogins : function (hostname, formSubmitURL, httpRealm) {
        let count = {};
        let loginData = {
            hostname: hostname,
            formSubmitURL: formSubmitURL,
            httpRealm: httpRealm
        };
        let matchData = { };
        for each (let field in ["hostname", "formSubmitURL", "httpRealm"])
            if (loginData[field] != '')
                matchData[field] = loginData[field];
        let [logins, ids] = this._searchLogins(matchData);

        this.log("_countLogins: counted logins: " + logins.length);
        return logins.length;
    },


    /*
     * uiBusy
     */
    get uiBusy() {
        return this._crypto.uiBusy;
    },


    /*
     * isLoggedIn
     */
    get isLoggedIn() {
        return this._crypto.isLoggedIn;
    },


    /*
     * _sendNotification
     *
     * Send a notification when stored data is changed.
     */
    _sendNotification : function (changeType, data) {
        let dataObject = data;
        // Can't pass a raw JS string or array though notifyObservers(). :-(
        if (data instanceof Array) {
            dataObject = Cc["@mozilla.org/array;1"].
                         createInstance(Ci.nsIMutableArray);
            for (let i = 0; i < data.length; i++)
                dataObject.appendElement(data[i], false);
        } else if (typeof(data) == "string") {
            dataObject = Cc["@mozilla.org/supports-string;1"].
                         createInstance(Ci.nsISupportsString);
            dataObject.data = data;
        }
        Services.obs.notifyObservers(dataObject, "passwordmgr-storage-changed", changeType);
    },


    /*
     * _getIdForLogin
     *
     * Returns an array with two items: [id, login]. If the login was not
     * found, both items will be null. The returned login contains the actual
     * stored login (useful for looking at the actual nsILoginMetaInfo values).
     */
    _getIdForLogin : function (login) {
        let matchData = { };
        for each (let field in ["hostname", "formSubmitURL", "httpRealm"])
            if (login[field] != '')
                matchData[field] = login[field];
        let [logins, ids] = this._searchLogins(matchData);

        let id = null;
        let foundLogin = null;

        // The specified login isn't encrypted, so we need to ensure
        // the logins we're comparing with are decrypted. We decrypt one entry
        // at a time, lest _decryptLogins return fewer entries and screw up
        // indices between the two.
        for (let i = 0; i < logins.length; i++) {
            let [decryptedLogin] = this._decryptLogins([logins[i]]);

            if (!decryptedLogin || !decryptedLogin.equals(login))
                continue;

            // We've found a match, set id and break
            foundLogin = decryptedLogin;
            id = ids[i];
            break;
        }

        return [id, foundLogin];
    },


    /*
     * _isGuidUnique
     *
     * Checks to see if the specified GUID already exists.
     */
    _isGuidUnique : function (guid) {
        this._store.ensureDataReady();

        return this._store.data.logins.every(l => l.guid != guid);
    },


    /*
     * _encryptLogin
     *
     * Returns the encrypted username, password, and encrypton type for the specified
     * login. Can throw if the user cancels a master password entry.
     */
    _encryptLogin : function (login) {
        let encUsername = this._crypto.encrypt(login.username);
        let encPassword = this._crypto.encrypt(login.password);
        let encType     = this._crypto.defaultEncType;

        return [encUsername, encPassword, encType];
    },


    /*
     * _decryptLogins
     *
     * Decrypts username and password fields in the provided array of
     * logins.
     *
     * The entries specified by the array will be decrypted, if possible.
     * An array of successfully decrypted logins will be returned. The return
     * value should be given to external callers (since still-encrypted
     * entries are useless), whereas internal callers generally don't want
     * to lose unencrypted entries (eg, because the user clicked Cancel
     * instead of entering their master password)
     */
    _decryptLogins : function (logins) {
        let result = [];

        for each (let login in logins) {
            try {
                login.username = this._crypto.decrypt(login.username);
                login.password = this._crypto.decrypt(login.password);
            } catch (e) {
                // If decryption failed (corrupt entry?), just skip it.
                // Rethrow other errors (like canceling entry of a master pw)
                if (e.result == Cr.NS_ERROR_FAILURE)
                    continue;
                throw e;
            }
            result.push(login);
        }

        return result;
    },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([LoginManagerStorage_json]);
