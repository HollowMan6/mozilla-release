// |reftest| skip-if(!this.hasOwnProperty('Temporal')) -- Temporal is not enabled unconditionally
// Copyright (C) 2023 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.calendar.prototype.yearMonthFromFields
description: Does not throw a RangeError if only one of era/eraYear fields is present
features: [Temporal]
includes: [temporalHelpers.js]
---*/

const base = { year: 2000, month: 5, day: 2, era: 'ce' };
const instance = new Temporal.Calendar('iso8601');
TemporalHelpers.assertPlainYearMonth(instance.yearMonthFromFields({ ...base }), 2000, 5, 'M05');

const base2 = { year: 2000, month: 5, day: 2, eraYear: 1 };
TemporalHelpers.assertPlainYearMonth(instance.yearMonthFromFields({ ...base }), 2000, 5, 'M05');

reportCompare(0, 0);
