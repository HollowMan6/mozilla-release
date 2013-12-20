/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* bit vectors for sets of CSS properties */

#ifndef nsCSSPropertySet_h__
#define nsCSSPropertySet_h__

#include "mozilla/ArrayUtils.h"

#include "nsCSSProperty.h"
#include <limits.h> // for CHAR_BIT

/**
 * nsCSSPropertySet maintains a set of non-shorthand CSS properties.  In
 * other words, for each longhand CSS property we support, it has a bit
 * for whether that property is in the set.
 */
class nsCSSPropertySet {
public:
    nsCSSPropertySet() { Empty(); }
    // auto-generated copy-constructor OK

    void AssertInSetRange(nsCSSProperty aProperty) const {
        NS_ASSERTION(0 <= aProperty &&
                     aProperty < eCSSProperty_COUNT_no_shorthands,
                     "out of bounds");
    }

    // Conversion of aProperty to |size_t| after AssertInSetRange
    // lets the compiler generate significantly tighter code.

    void AddProperty(nsCSSProperty aProperty) {
        AssertInSetRange(aProperty);
        size_t p = aProperty;
        mProperties[p / kBitsInChunk] |=
          property_set_type(1) << (p % kBitsInChunk);
    }

    void RemoveProperty(nsCSSProperty aProperty) {
        AssertInSetRange(aProperty);
        size_t p = aProperty;
        mProperties[p / kBitsInChunk] &=
            ~(property_set_type(1) << (p % kBitsInChunk));
    }

    bool HasProperty(nsCSSProperty aProperty) const {
        AssertInSetRange(aProperty);
        size_t p = aProperty;
        return (mProperties[p / kBitsInChunk] &
                (property_set_type(1) << (p % kBitsInChunk))) != 0;
    }

    void Empty() {
        memset(mProperties, 0, sizeof(mProperties));
    }

    void AssertIsEmpty(const char* aText) const {
        for (size_t i = 0; i < mozilla::ArrayLength(mProperties); ++i) {
            NS_ASSERTION(mProperties[i] == 0, aText);
        }
    }

private:
    typedef unsigned long property_set_type;
public:
    // number of bits in |property_set_type|.
    static const size_t kBitsInChunk = sizeof(property_set_type)*CHAR_BIT;
    // number of |property_set_type|s in the set
    static const size_t kChunkCount =
        (eCSSProperty_COUNT_no_shorthands + kBitsInChunk - 1) / kBitsInChunk;

    /*
     * For fast enumeration of all the bits that are set, callers can
     * check each chunk against zero (since in normal cases few bits are
     * likely to be set).
     */
    bool HasPropertyInChunk(size_t aChunk) const {
        return mProperties[aChunk] != 0;
    }
    bool HasPropertyAt(size_t aChunk, size_t aBit) const {
        return (mProperties[aChunk] & (property_set_type(1) << aBit)) != 0;
    }
    static nsCSSProperty CSSPropertyAt(size_t aChunk, size_t aBit) {
        return nsCSSProperty(aChunk * kBitsInChunk + aBit);
    }

private:
    property_set_type mProperties[kChunkCount];
};

#endif /* !defined(nsCSSPropertySet_h__) */
