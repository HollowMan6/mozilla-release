// |reftest| shell-option(--enable-arraybuffer-transfer) skip-if(!this.hasOwnProperty('SharedArrayBuffer')||!ArrayBuffer.prototype.transfer||!xulRuntime.shell) -- SharedArrayBuffer,arraybuffer-transfer is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-arraybuffer.prototype.transfer
description: Throws a TypeError if `this` value is a SharedArrayBuffer
info: |
  ArrayBuffer.prototype.transfer ( [ newLength ] )

  1. Let O be the this value.
  2. Perform ? RequireInternalSlot(O, [[ArrayBufferData]]).
  3. If IsSharedArrayBuffer(O) is true, throw a TypeError exception.
  [...]
features: [SharedArrayBuffer, arraybuffer-transfer]
---*/

var sab = new SharedArrayBuffer(0);

assert.throws(TypeError, function() {
  ArrayBuffer.prototype.transfer.call(sab);
}, '`this` value cannot be a SharedArrayBuffer');

reportCompare(0, 0);
