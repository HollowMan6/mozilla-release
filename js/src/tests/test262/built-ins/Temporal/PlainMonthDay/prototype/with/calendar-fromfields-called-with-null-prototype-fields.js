// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plainmonthday.prototype.with
description: >
    Calendar.monthDayFromFields method is called with a null-prototype fields object
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const calendar = TemporalHelpers.calendarCheckFieldsPrototypePollution();
const instance = new Temporal.PlainMonthDay(5, 2, calendar);
instance.with({ day: 24 });
assert.sameValue(calendar.monthDayFromFieldsCallCount, 1, "monthDayFromFields should have been called on the calendar");

reportCompare(0, 0);
