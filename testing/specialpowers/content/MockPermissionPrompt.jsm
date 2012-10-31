/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var EXPORTED_SYMBOLS = ["MockPermissionPrompt"];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cm = Components.manager;
const Cu = Components.utils;

const CONTRACT_ID = "@mozilla.org/content-permission/prompt;1";

Cu.import("resource://gre/modules/FileUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

var registrar = Cm.QueryInterface(Ci.nsIComponentRegistrar);
var oldClassID, oldFactory;
var newClassID = Cc["@mozilla.org/uuid-generator;1"].getService(Ci.nsIUUIDGenerator).generateUUID();
var newFactory = {
  createInstance: function(aOuter, aIID) {
    if (aOuter)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return new MockPermissionPromptInstance().QueryInterface(aIID);
  },
  lockFactory: function(aLock) {
    throw Components.results.NS_ERROR_NOT_IMPLEMENTED;
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIFactory])
};

var MockPermissionPrompt = {
  init: function() {
    this.reset();
    if (!registrar.isCIDRegistered(newClassID)) {
      oldClassID = registrar.contractIDToCID(CONTRACT_ID);
      oldFactory = Cm.getClassObject(Cc[CONTRACT_ID], Ci.nsIFactory);
      registrar.unregisterFactory(oldClassID, oldFactory);
      registrar.registerFactory(newClassID, "", CONTRACT_ID, newFactory);
    }
  },
  
  reset: function() {
  },
  
  cleanup: function() {
    this.reset();
    if (oldFactory) {
      registrar.unregisterFactory(newClassID, newFactory);
      registrar.registerFactory(oldClassID, "", CONTRACT_ID, oldFactory);
    }
  },
};

function MockPermissionPromptInstance() { };
MockPermissionPromptInstance.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIContentPermissionPrompt]),

  promptResult: Ci.nsIPermissionManager.UNKNOWN_ACTION,

  prompt: function(request) {

    this.promptResult = Services.perms.testExactPermissionFromPrincipal(request.principal,
                                                                        request.type);
    if (this.promptResult == Ci.nsIPermissionManager.ALLOW_ACTION) {
      request.allow();
    }
    else {
      request.cancel();
    }
  }
};

// Expose everything to content. We call reset() here so that all of the relevant
// lazy expandos get added.
MockPermissionPrompt.reset();
function exposeAll(obj) {
  var props = {};
  for (var prop in obj)
    props[prop] = 'rw';
  obj.__exposedProps__ = props;
}
exposeAll(MockPermissionPrompt);
exposeAll(MockPermissionPromptInstance.prototype);
