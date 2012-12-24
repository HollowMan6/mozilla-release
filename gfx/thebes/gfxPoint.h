/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_POINT_H
#define GFX_POINT_H

#include "nsMathUtils.h"
#include "mozilla/gfx/BaseSize.h"
#include "mozilla/gfx/BasePoint.h"
#include "nsSize.h"
#include "nsPoint.h"

#include "gfxTypes.h"

typedef nsIntSize gfxIntSize;

struct THEBES_API gfxSize : public mozilla::gfx::BaseSize<gfxFloat, gfxSize> {
    typedef mozilla::gfx::BaseSize<gfxFloat, gfxSize> Super;

    gfxSize() : Super() {}
    gfxSize(gfxFloat aWidth, gfxFloat aHeight) : Super(aWidth, aHeight) {}
    gfxSize(const nsIntSize& aSize) : Super(aSize.width, aSize.height) {}
};

struct THEBES_API gfxPoint : public mozilla::gfx::BasePoint<gfxFloat, gfxPoint> {
    typedef mozilla::gfx::BasePoint<gfxFloat, gfxPoint> Super;

    gfxPoint() : Super() {}
    gfxPoint(gfxFloat aX, gfxFloat aY) : Super(aX, aY) {}
    gfxPoint(const nsIntPoint& aPoint) : Super(aPoint.x, aPoint.y) {}

    // Round() is *not* rounding to nearest integer if the values are negative.
    // They are always rounding as floor(n + 0.5).
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=410748#c14
    gfxPoint& Round() {
        x = floor(x + 0.5);
        y = floor(y + 0.5);
        return *this;
    }

    nsIntPoint RoundedAwayFromZero() const {
        return nsIntPoint(NS_lround(x), NS_lround(y));
    }

    bool WithinEpsilonOf(const gfxPoint& aPoint, gfxFloat aEpsilon) {
        return fabs(aPoint.x - x) < aEpsilon && fabs(aPoint.y - y) < aEpsilon;
    }
};

inline gfxPoint
operator*(const gfxPoint& aPoint, const gfxSize& aSize)
{
  return gfxPoint(aPoint.x * aSize.width, aPoint.y * aSize.height);
}

inline gfxPoint
operator/(const gfxPoint& aPoint, const gfxSize& aSize)
{
  return gfxPoint(aPoint.x / aSize.width, aPoint.y / aSize.height);
}

inline gfxSize
operator/(gfxFloat aValue, const gfxSize& aSize)
{
  return gfxSize(aValue / aSize.width, aValue / aSize.height);
}

#endif /* GFX_POINT_H */
