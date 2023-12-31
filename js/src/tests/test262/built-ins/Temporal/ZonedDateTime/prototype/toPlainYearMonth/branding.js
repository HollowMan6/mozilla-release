// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.zoneddatetime.prototype.toplainyearmonth
description: Throw a TypeError if the receiver is invalid
features: [Symbol, Temporal]
---*/

const toPlainYearMonth = Temporal.ZonedDateTime.prototype.toPlainYearMonth;

assert.sameValue(typeof toPlainYearMonth, "function");

assert.throws(TypeError, () => toPlainYearMonth.call(undefined), "undefined");
assert.throws(TypeError, () => toPlainYearMonth.call(null), "null");
assert.throws(TypeError, () => toPlainYearMonth.call(true), "true");
assert.throws(TypeError, () => toPlainYearMonth.call(""), "empty string");
assert.throws(TypeError, () => toPlainYearMonth.call(Symbol()), "symbol");
assert.throws(TypeError, () => toPlainYearMonth.call(1), "1");
assert.throws(TypeError, () => toPlainYearMonth.call({}), "plain object");
assert.throws(TypeError, () => toPlainYearMonth.call(Temporal.ZonedDateTime), "Temporal.ZonedDateTime");
assert.throws(TypeError, () => toPlainYearMonth.call(Temporal.ZonedDateTime.prototype), "Temporal.ZonedDateTime.prototype");

reportCompare(0, 0);
