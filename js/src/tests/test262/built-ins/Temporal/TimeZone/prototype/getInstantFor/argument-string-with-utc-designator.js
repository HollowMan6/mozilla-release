// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.timezone.prototype.getinstantfor
description: RangeError thrown if a string with UTC designator is used as a PlainDateTime
features: [Temporal, arrow-function]
---*/

const invalidStrings = [
  "2019-10-01T09:00:00Z",
  "2019-10-01T09:00:00Z[UTC]",
];
const instance = new Temporal.TimeZone("UTC");
invalidStrings.forEach((arg) => {
  assert.throws(
    RangeError,
    () => instance.getInstantFor(arg),
    "String with UTC designator should not be valid as a PlainDateTime"
  );
});

reportCompare(0, 0);
