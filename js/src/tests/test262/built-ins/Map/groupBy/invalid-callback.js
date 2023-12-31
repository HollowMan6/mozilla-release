// |reftest| shell-option(--enable-array-grouping) skip-if(!Object.groupBy||!xulRuntime.shell) -- array-grouping is not enabled unconditionally, requires shell-options
// Copyright (c) 2023 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-map.groupby
description: Map.groupBy called with non-callable throws TypeError
info: |
  Map.groupBy ( items, callbackfn )

  ...
  GroupBy ( items, callbackfn, coercion )

  2. If IsCallable(callbackfn) is false, throw a TypeError exception.
  ...
features: [array-grouping, Map]
---*/


assert.throws(TypeError, function() {
  Map.groupBy([], null)
}, "null callback throws TypeError");

assert.throws(TypeError, function() {
  Map.groupBy([], undefined)
}, "undefined callback throws TypeError");

assert.throws(TypeError, function() {
  Map.groupBy([], {})
}, "object callback throws TypeError");

reportCompare(0, 0);
