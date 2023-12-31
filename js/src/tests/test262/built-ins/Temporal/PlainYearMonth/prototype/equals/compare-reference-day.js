// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainyearmonth.prototype.equals
description: equals() takes the reference day into account
features: [Temporal]
---*/

const iso = Temporal.Calendar.from("iso8601");
const ym1 = new Temporal.PlainYearMonth(2000, 1, iso, 1);
const ym2 = new Temporal.PlainYearMonth(2000, 1, iso, 2);
assert.sameValue(ym1.equals(ym2), false);

reportCompare(0, 0);
