// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.calendar.prototype.day
description: >
    Calendar.dateFromFields method is called with a null-prototype fields object
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const calendar = TemporalHelpers.calendarCheckFieldsPrototypePollution();
const instance = new Temporal.Calendar("iso8601");
const arg = { year: 2000, month: 5, day: 2, calendar };
instance.day(arg);
assert.sameValue(calendar.dateFromFieldsCallCount, 1, "dateFromFields should be called on the property bag's calendar");

reportCompare(0, 0);
