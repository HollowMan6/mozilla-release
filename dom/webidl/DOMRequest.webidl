/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface Window;
interface nsISupports;

enum DOMRequestReadyState { "pending", "done" };

interface DOMRequest : EventTarget {
  readonly attribute DOMRequestReadyState readyState;

  readonly attribute any result;
  readonly attribute nsISupports? error;

  [SetterThrows]
  attribute EventHandler onsuccess;
  [SetterThrows]
  attribute EventHandler onerror;
};
