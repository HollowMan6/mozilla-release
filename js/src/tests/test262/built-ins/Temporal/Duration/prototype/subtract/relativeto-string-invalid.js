// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.subtract
description: RangeError thrown if relativeTo is a string with the wrong format
features: [Temporal]
---*/

['bad string', '15:30:45.123456', 'iso8601', 'UTC', 'P1YT1H'].forEach((relativeTo) => {
  const duration = new Temporal.Duration(1, 0, 0, 41);
  assert.throws(RangeError, () => duration.subtract(new Temporal.Duration(0, 0, 0, 10), { relativeTo }));
});

reportCompare(0, 0);
