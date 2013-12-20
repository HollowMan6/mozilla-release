/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://url.spec.whatwg.org/#urlsearchparams
 *
 * To the extent possible under law, the editors have waived all copyright
 * and related or neighboring rights to this work. In addition, as of 17
 * February 2013, the editors have made this specification available under
 * the Open Web Foundation Agreement Version 1.0, which is available at
 * http://www.openwebfoundation.org/legal/the-owf-1-0-agreements/owfa-1-0.
 */

[Constructor(optional DOMString init = ""),
 Constructor(URLSearchParams init)]
interface URLSearchParams {
  DOMString? get(DOMString name);
  sequence<DOMString> getAll(DOMString name);
  void set(DOMString name, DOMString value);
  void append(DOMString name, DOMString value);
  boolean has(DOMString name);
  void delete(DOMString name);
  readonly attribute unsigned long size;
};
