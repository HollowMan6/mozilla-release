// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.calendar.prototype.daysinmonth
description: The "daysInMonth" property of Temporal.Calendar.prototype
includes: [propertyHelper.js]
features: [Temporal]
---*/

assert.sameValue(
  typeof Temporal.Calendar.prototype.daysInMonth,
  "function",
  "`typeof Calendar.prototype.daysInMonth` is `function`"
);

verifyProperty(Temporal.Calendar.prototype, "daysInMonth", {
  writable: true,
  enumerable: false,
  configurable: true,
});

reportCompare(0, 0);
