/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Deprecated",
                                  "resource://gre/modules/Deprecated.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FormHistory",
                                  "resource://gre/modules/FormHistory.jsm");

function FormAutoComplete() {
    this.init();
}

/**
 * FormAutoComplete
 *
 * Implements the nsIFormAutoComplete interface in the main process.
 */
FormAutoComplete.prototype = {
    classID          : Components.ID("{c11c21b2-71c9-4f87-a0f8-5e13f50495fd}"),
    QueryInterface   : XPCOMUtils.generateQI([Ci.nsIFormAutoComplete, Ci.nsISupportsWeakReference]),

    _prefBranch         : null,
    _debug              : true, // mirrors browser.formfill.debug
    _enabled            : true, // mirrors browser.formfill.enable preference
    _agedWeight         : 2,
    _bucketSize         : 1,
    _maxTimeGroupings   : 25,
    _timeGroupingSize   : 7 * 24 * 60 * 60 * 1000 * 1000,
    _expireDays         : null,
    _boundaryWeight     : 25,
    _prefixWeight       : 5,

    // Only one query is performed at a time, which will be stored in _pendingQuery
    // while the query is being performed. It will be cleared when the query finishes,
    // is cancelled, or an error occurs. If a new query occurs while one is already
    // pending, the existing one is cancelled. The pending query will be an
    // mozIStoragePendingStatement object.
    _pendingQuery       : null,

    init : function() {
        // Preferences. Add observer so we get notified of changes.
        this._prefBranch = Services.prefs.getBranch("browser.formfill.");
        this._prefBranch.addObserver("", this.observer, true);
        this.observer._self = this;

        this._debug            = this._prefBranch.getBoolPref("debug");
        this._enabled          = this._prefBranch.getBoolPref("enable");
        this._agedWeight       = this._prefBranch.getIntPref("agedWeight");
        this._bucketSize       = this._prefBranch.getIntPref("bucketSize");
        this._maxTimeGroupings = this._prefBranch.getIntPref("maxTimeGroupings");
        this._timeGroupingSize = this._prefBranch.getIntPref("timeGroupingSize") * 1000 * 1000;
        this._expireDays       = this._prefBranch.getIntPref("expire_days");
    },

    observer : {
        _self : null,

        QueryInterface : XPCOMUtils.generateQI([Ci.nsIObserver,
                                                Ci.nsISupportsWeakReference]),

        observe : function (subject, topic, data) {
            let self = this._self;
            if (topic == "nsPref:changed") {
                let prefName = data;
                self.log("got change to " + prefName + " preference");

                switch (prefName) {
                    case "agedWeight":
                        self._agedWeight = self._prefBranch.getIntPref(prefName);
                        break;
                    case "debug":
                        self._debug = self._prefBranch.getBoolPref(prefName);
                        break;
                    case "enable":
                        self._enabled = self._prefBranch.getBoolPref(prefName);
                        break;
                    case "maxTimeGroupings":
                        self._maxTimeGroupings = self._prefBranch.getIntPref(prefName);
                        break;
                    case "timeGroupingSize":
                        self._timeGroupingSize = self._prefBranch.getIntPref(prefName) * 1000 * 1000;
                        break;
                    case "bucketSize":
                        self._bucketSize = self._prefBranch.getIntPref(prefName);
                        break;
                    case "boundaryWeight":
                        self._boundaryWeight = self._prefBranch.getIntPref(prefName);
                        break;
                    case "prefixWeight":
                        self._prefixWeight = self._prefBranch.getIntPref(prefName);
                        break;
                    default:
                        self.log("Oops! Pref not handled, change ignored.");
                }
            }
        }
    },


    /*
     * log
     *
     * Internal function for logging debug messages to the Error Console
     * window
     */
    log : function (message) {
        if (!this._debug)
            return;
        dump("FormAutoComplete: " + message + "\n");
        Services.console.logStringMessage("FormAutoComplete: " + message);
    },


    autoCompleteSearch : function (aInputName, aUntrimmedSearchString, aField, aPreviousResult) {
      Deprecated.warning("nsIFormAutoComplete::autoCompleteSearch is deprecated", "https://bugzilla.mozilla.org/show_bug.cgi?id=697377");

      let result = null;
      let listener = {
        onSearchCompletion: function (r) result = r
      };
      this._autoCompleteSearchShared(aInputName, aUntrimmedSearchString, aField, aPreviousResult, listener);

      // Just wait for the result to to be available.
      let thread = Components.classes["@mozilla.org/thread-manager;1"].getService().currentThread;
      while (!result && this._pendingQuery) {
        thread.processNextEvent(true);
      }

      return result;
    },

    autoCompleteSearchAsync : function (aInputName, aUntrimmedSearchString, aField, aPreviousResult, aListener) {
      this._autoCompleteSearchShared(aInputName, aUntrimmedSearchString, aField, aPreviousResult, aListener);
    },

    /*
     * autoCompleteSearchShared
     *
     * aInputName    -- |name| attribute from the form input being autocompleted.
     * aUntrimmedSearchString -- current value of the input
     * aField -- nsIDOMHTMLInputElement being autocompleted (may be null if from chrome)
     * aPreviousResult -- previous search result, if any.
     * aListener -- nsIFormAutoCompleteObserver that listens for the nsIAutoCompleteResult
     *              that may be returned asynchronously.
     */
    _autoCompleteSearchShared : function (aInputName, aUntrimmedSearchString, aField, aPreviousResult, aListener) {
        function sortBytotalScore (a, b) {
            return b.totalScore - a.totalScore;
        }

        let result = null;
        if (!this._enabled) {
            result = new FormAutoCompleteResult(FormHistory, [], aInputName, aUntrimmedSearchString);
            if (aListener) {
              aListener.onSearchCompletion(result);
            }
            return;
        }

        // don't allow form inputs (aField != null) to get results from search bar history
        if (aInputName == 'searchbar-history' && aField) {
            this.log('autoCompleteSearch for input name "' + aInputName + '" is denied');
            result = new FormAutoCompleteResult(FormHistory, [], aInputName, aUntrimmedSearchString);
            if (aListener) {
              aListener.onSearchCompletion(result);
            }
            return;
        }

        this.log("AutoCompleteSearch invoked. Search is: " + aUntrimmedSearchString);
        let searchString = aUntrimmedSearchString.trim().toLowerCase();

        // reuse previous results if:
        // a) length greater than one character (others searches are special cases) AND
        // b) the the new results will be a subset of the previous results
        if (aPreviousResult && aPreviousResult.searchString.trim().length > 1 &&
            searchString.indexOf(aPreviousResult.searchString.trim().toLowerCase()) >= 0) {
            this.log("Using previous autocomplete result");
            result = aPreviousResult;
            result.wrappedJSObject.searchString = aUntrimmedSearchString;

            let searchTokens = searchString.split(/\s+/);
            // We have a list of results for a shorter search string, so just
            // filter them further based on the new search string and add to a new array.
            let entries = result.wrappedJSObject.entries;
            let filteredEntries = [];
            for (let i = 0; i < entries.length; i++) {
                let entry = entries[i];
                // Remove results that do not contain the token
                // XXX bug 394604 -- .toLowerCase can be wrong for some intl chars
                if(searchTokens.some(function (tok) entry.textLowerCase.indexOf(tok) < 0))
                    continue;
                this._calculateScore(entry, searchString, searchTokens);
                this.log("Reusing autocomplete entry '" + entry.text +
                         "' (" + entry.frecency +" / " + entry.totalScore + ")");
                filteredEntries.push(entry);
            }
            filteredEntries.sort(sortBytotalScore);
            result.wrappedJSObject.entries = filteredEntries;

            if (aListener) {
              aListener.onSearchCompletion(result);
            }
        } else {
            this.log("Creating new autocomplete search result.");

            // Start with an empty list.
            result = new FormAutoCompleteResult(FormHistory, [], aInputName, aUntrimmedSearchString);

            let processEntry = function(aEntries) {
              if (aField && aField.maxLength > -1) {
                result.entries =
                  aEntries.filter(function (el) { return el.text.length <= aField.maxLength; });
              } else {
                result.entries = aEntries;
              }

              if (aListener) {
                aListener.onSearchCompletion(result);
              }
            }

            this.getAutoCompleteValues(aInputName, searchString, processEntry);
        }
    },

    stopAutoCompleteSearch : function () {
        if (this._pendingQuery) {
            this._pendingQuery.cancel();
            this._pendingQuery = null;
        }
    },

    /*
     * Get the values for an autocomplete list given a search string.
     *
     *  fieldName - fieldname field within form history (the form input name)
     *  searchString - string to search for
     *  callback - called when the values are available. Passed an array of objects,
     *             containing properties for each result. The callback is only called
     *             when successful.
     */
    getAutoCompleteValues : function (fieldName, searchString, callback) {
        let params = {
            agedWeight:         this._agedWeight,
            bucketSize:         this._bucketSize,
            expiryDate:         1000 * (Date.now() - this._expireDays * 24 * 60 * 60 * 1000),
            fieldname:          fieldName,
            maxTimeGroupings:   this._maxTimeGroupings,
            timeGroupingSize:   this._timeGroupingSize,
            prefixWeight:       this._prefixWeight,
            boundaryWeight:     this._boundaryWeight
        }

        this.stopAutoCompleteSearch();

        let results = [];
        let processResults = {
          handleResult: aResult => {
            results.push(aResult);
          },
          handleError: aError => {
            this.log("getAutocompleteValues failed: " + aError.message);
          },
          handleCompletion: aReason => {
            this._pendingQuery = null;
            if (!aReason) {
              callback(results);
            }
          }
        };

        this._pendingQuery = FormHistory.getAutoCompleteResults(searchString, params, processResults);
    },

    /*
     * _calculateScore
     *
     * entry    -- an nsIAutoCompleteResult entry
     * aSearchString -- current value of the input (lowercase)
     * searchTokens -- array of tokens of the search string
     *
     * Returns: an int
     */
    _calculateScore : function (entry, aSearchString, searchTokens) {
        let boundaryCalc = 0;
        // for each word, calculate word boundary weights
        for each (let token in searchTokens) {
            boundaryCalc += (entry.textLowerCase.indexOf(token) == 0);
            boundaryCalc += (entry.textLowerCase.indexOf(" " + token) >= 0);
        }
        boundaryCalc = boundaryCalc * this._boundaryWeight;
        // now add more weight if we have a traditional prefix match and
        // multiply boundary bonuses by boundary weight
        boundaryCalc += this._prefixWeight *
                        (entry.textLowerCase.
                         indexOf(aSearchString) == 0);
        entry.totalScore = Math.round(entry.frecency * Math.max(1, boundaryCalc));
    }

}; // end of FormAutoComplete implementation

/**
 * FormAutoCompleteChild
 *
 * Implements the nsIFormAutoComplete interface in a child content process,
 * and forwards the auto-complete requests to the parent process which
 * also implements a nsIFormAutoComplete interface and has
 * direct access to the FormHistory database.
 */
function FormAutoCompleteChild() {
  this.init();
}

FormAutoCompleteChild.prototype = {
    classID          : Components.ID("{c11c21b2-71c9-4f87-a0f8-5e13f50495fd}"),
    QueryInterface   : XPCOMUtils.generateQI([Ci.nsIFormAutoComplete, Ci.nsISupportsWeakReference]),

    _debug: false,
    _enabled: true,

    /*
     * init
     *
     * Initializes the content-process side of the FormAutoComplete component,
     * and add a listener for the message that the parent process sends when
     * a result is produced.
     */
    init: function() {
      this._debug    = Services.prefs.getBoolPref("browser.formfill.debug");
      this._enabled  = Services.prefs.getBoolPref("browser.formfill.enable");
      this.log("init");
    },

    /*
     * log
     *
     * Internal function for logging debug messages
     */
    log : function (message) {
      if (!this._debug)
        return;
      dump("FormAutoCompleteChild: " + message + "\n");
    },

    autoCompleteSearch : function (aInputName, aUntrimmedSearchString, aField, aPreviousResult) {
      // This function is deprecated
    },

    autoCompleteSearchAsync : function (aInputName, aUntrimmedSearchString, aField, aPreviousResult, aListener) {
      this.log("autoCompleteSearchAsync");

      this._pendingListener = aListener;

      let rect = aField.getBoundingClientRect();

      let topLevelDocshell = aField.ownerDocument.defaultView
                                   .QueryInterface(Ci.nsIInterfaceRequestor)
                                   .getInterface(Ci.nsIDocShell)
                                   .sameTypeRootTreeItem
                                   .QueryInterface(Ci.nsIDocShell);

      let mm = topLevelDocshell.QueryInterface(Ci.nsIInterfaceRequestor)
                               .getInterface(Ci.nsIContentFrameMessageManager);

      mm.sendAsyncMessage("FormHistory:AutoCompleteSearchAsync", {
        inputName: aInputName,
        untrimmedSearchString: aUntrimmedSearchString,
        left: rect.left,
        top: rect.top,
        width: rect.width,
        height: rect.height
      });

      mm.addMessageListener("FormAutoComplete:AutoCompleteSearchAsyncResult",
        function searchFinished(message) {
          mm.removeMessageListener("FormAutoComplete:AutoCompleteSearchAsyncResult", searchFinished);
          let result = new FormAutoCompleteResult(
            null,
            [{text: res} for (res of message.data.results)],
            null,
            null
          );
          if (aListener) {
            aListener.onSearchCompletion(result);
          }
        }
      );

      this.log("autoCompleteSearchAsync message was sent");
    },

    stopAutoCompleteSearch : function () {
       this.log("stopAutoCompleteSearch");
    },
}; // end of FormAutoCompleteChild implementation

// nsIAutoCompleteResult implementation
function FormAutoCompleteResult (formHistory, entries, fieldName, searchString) {
    this.formHistory = formHistory;
    this.entries = entries;
    this.fieldName = fieldName;
    this.searchString = searchString;
}

FormAutoCompleteResult.prototype = {
    QueryInterface : XPCOMUtils.generateQI([Ci.nsIAutoCompleteResult,
                                            Ci.nsISupportsWeakReference]),

    // private
    formHistory : null,
    entries : null,
    fieldName : null,

    _checkIndexBounds : function (index) {
        if (index < 0 || index >= this.entries.length)
            throw Components.Exception("Index out of range.", Cr.NS_ERROR_ILLEGAL_VALUE);
    },

    // Allow autoCompleteSearch to get at the JS object so it can
    // modify some readonly properties for internal use.
    get wrappedJSObject() {
        return this;
    },

    // Interfaces from idl...
    searchString : null,
    errorDescription : "",
    get defaultIndex() {
        if (entries.length == 0)
            return -1;
        else
            return 0;
    },
    get searchResult() {
        if (this.entries.length == 0)
            return Ci.nsIAutoCompleteResult.RESULT_NOMATCH;
        return Ci.nsIAutoCompleteResult.RESULT_SUCCESS;
    },
    get matchCount() {
        return this.entries.length;
    },

    getValueAt : function (index) {
        this._checkIndexBounds(index);
        return this.entries[index].text;
    },

    getLabelAt: function(index) {
        return getValueAt(index);
    },

    getCommentAt : function (index) {
        this._checkIndexBounds(index);
        return "";
    },

    getStyleAt : function (index) {
        this._checkIndexBounds(index);
        return "";
    },

    getImageAt : function (index) {
        this._checkIndexBounds(index);
        return "";
    },

    removeValueAt : function (index, removeFromDB) {
        this._checkIndexBounds(index);

        let [removedEntry] = this.entries.splice(index, 1);

        if (removeFromDB) {
          this.formHistory.update({ op: "remove",
                                    fieldname: this.fieldName,
                                    value: removedEntry.text });
        }
    }
};


let remote = Services.appinfo.browserTabsRemote;
if (Services.appinfo.processType == Services.appinfo.PROCESS_TYPE_CONTENT && remote) {
  // Register the stub FormAutoComplete module in the child which will
  // forward messages to the parent through the process message manager.
  let component = [FormAutoCompleteChild];
  this.NSGetFactory = XPCOMUtils.generateNSGetFactory(component);
} else {
  let component = [FormAutoComplete];
  this.NSGetFactory = XPCOMUtils.generateNSGetFactory(component);
}
