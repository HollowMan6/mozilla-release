/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Weave.
 *
 * The Initial Developer of the Original Code is Mozilla.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Dan Mills <thunder@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

const EXPORTED_SYMBOLS = ['Collection'];

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://weave/log4moz.js");
Cu.import("resource://weave/constants.js");
Cu.import("resource://weave/util.js");
Cu.import("resource://weave/async.js");
Cu.import("resource://weave/crypto.js");
Cu.import("resource://weave/resource.js");
Cu.import("resource://weave/base_records/wbo.js");
Cu.import("resource://weave/base_records/crypto.js");
Cu.import("resource://weave/base_records/keys.js");

Function.prototype.async = Async.sugar;

function Collection(uri, recordObj) {
  this._Coll_init(uri);
  this._recordObj = recordObj;
}
Collection.prototype = {
  __proto__: Resource.prototype,
  _logName: "Collection",

  _Coll_init: function Coll_init(uri) {
    this._init(uri);
    this.pushFilter(new JsonFilter());
    this._full = true;
    this._older = 0;
    this._data = [];
  },

  _rebuildURL: function Coll__rebuildURL() {
    // XXX should consider what happens if it's not a URL...
    this.uri.QueryInterface(Ci.nsIURL);

    let args = [];
    if (this.older)
      args.push('older=' + this.older);
    if (this.full)
      args.push('full=1');
    if (this.sort)
      args.push('sort=' + this.sort);

    this.uri.query = (args.length > 0)? '?' + args.join('&') : '';
  },

  // get full items
  get full() { return this._full; },
  set full(value) {
    this._full = value;
    this._rebuildURL();
  },

  // get only items modified since some date
  get older() { return this._older; },
  set older(value) {
    this._older = value;
    this._rebuildURL();
  },

  // get items sorted by some criteria. valid values:
  // oldest (oldest first)
  // newest (newest first)
  // index
  // depthindex
  get sort() { return this._sort; },
  set sort(value) {
    this._sort = value;
    this._rebuildURL();
  },

  get iter() {
    if (!this._iter)
      this._iter = new CollectionIterator(this);
    return this._iter;
  },

  pushData: function Coll_pushData(data) {
    this._data.push(data);
  },

  clearRecords: function Coll_clearRecords() {
    this._data = [];
  }
};

// FIXME: iterator should return WBOs / CryptoWrappers / Keys ?
// FIXME: need to reset iterator after a get/put/post
function CollectionIterator(coll) {
  this._init(coll);
}
CollectionIterator.prototype = {
  _init: function CollIter__init(coll) {
    this._coll = coll;
    this._idx = 0;
  },

  get count() { return this._coll.data.length; },

  next: function CollIter_next(onComplete) {
    let fn = function CollIter__next() {
      let self = yield;

      if (this._idx >= this.count)
        return;
      let item = this._coll.data[this._idx++];
      let record = new this._coll._recordObj();
      record.deserialize(Svc.Json.encode(item)); // FIXME: inefficient
      record.baseUri = this._coll.uri;

      self.done(record);
    };
    fn.async(this, onComplete);
  },

  reset: function CollIter_reset() {
    this._idx = 0;
  }
};