/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGANGLE_H__
#define __NS_SVGANGLE_H__

#include "nsAutoPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsError.h"
#include "nsISMILAttr.h"
#include "nsSVGElement.h"
#include "nsWrapperCache.h"
#include "mozilla/Attributes.h"

class nsISMILAnimationElement;
class nsSMILValue;
class nsSVGOrientType;

namespace mozilla {

// Angle Unit Types
static const unsigned short SVG_ANGLETYPE_UNKNOWN     = 0;
static const unsigned short SVG_ANGLETYPE_UNSPECIFIED = 1;
static const unsigned short SVG_ANGLETYPE_DEG         = 2;
static const unsigned short SVG_ANGLETYPE_RAD         = 3;
static const unsigned short SVG_ANGLETYPE_GRAD        = 4;

namespace dom {
class SVGAngle;
class SVGAnimatedAngle;
}
}

class nsSVGAngle
{
  friend class mozilla::dom::SVGAngle;
  friend class mozilla::dom::SVGAnimatedAngle;

public:
  void Init(uint8_t aAttrEnum = 0xff,
            float aValue = 0,
            uint8_t aUnitType = mozilla::SVG_ANGLETYPE_UNSPECIFIED) {
    mAnimVal = mBaseVal = aValue;
    mAnimValUnit = mBaseValUnit = aUnitType;
    mAttrEnum = aAttrEnum;
    mIsAnimated = false;
  }

  nsresult SetBaseValueString(const nsAString& aValue,
                              nsSVGElement *aSVGElement,
                              bool aDoSetAttr);
  void GetBaseValueString(nsAString& aValue) const;
  void GetAnimValueString(nsAString& aValue) const;

  float GetBaseValue() const
    { return mBaseVal * GetDegreesPerUnit(mBaseValUnit); }
  float GetAnimValue() const
    { return mAnimVal * GetDegreesPerUnit(mAnimValUnit); }

  void SetBaseValue(float aValue, nsSVGElement *aSVGElement, bool aDoSetAttr);
  void SetAnimValue(float aValue, uint8_t aUnit, nsSVGElement *aSVGElement);

  uint8_t GetBaseValueUnit() const { return mBaseValUnit; }
  uint8_t GetAnimValueUnit() const { return mAnimValUnit; }
  float GetBaseValInSpecifiedUnits() const { return mBaseVal; }
  float GetAnimValInSpecifiedUnits() const { return mAnimVal; }

  static nsresult ToDOMSVGAngle(nsISupports **aResult);
  nsresult ToDOMAnimatedAngle(nsISupports **aResult,
                              nsSVGElement* aSVGElement);
  // Returns a new nsISMILAttr object that the caller must delete
  nsISMILAttr* ToSMILAttr(nsSVGElement* aSVGElement);

  static float GetDegreesPerUnit(uint8_t aUnit);

private:

  float mAnimVal;
  float mBaseVal;
  uint8_t mAnimValUnit;
  uint8_t mBaseValUnit;
  uint8_t mAttrEnum; // element specified tracking for attribute
  bool mIsAnimated;

  void SetBaseValueInSpecifiedUnits(float aValue, nsSVGElement *aSVGElement);
  nsresult NewValueSpecifiedUnits(uint16_t aUnitType, float aValue,
                                  nsSVGElement *aSVGElement);
  nsresult ConvertToSpecifiedUnits(uint16_t aUnitType, nsSVGElement *aSVGElement);
  nsresult ToDOMBaseVal(mozilla::dom::SVGAngle **aResult, nsSVGElement* aSVGElement);
  nsresult ToDOMAnimVal(mozilla::dom::SVGAngle **aResult, nsSVGElement* aSVGElement);

public:
  // We do not currently implemente a SMILAngle struct because in SVG 1.1 the
  // only *animatable* attribute that takes an <angle> is 'orient', on the
  // 'marker' element, and 'orient' must be special cased since it can also
  // take the value 'auto', making it a more complex type.

  struct SMILOrient MOZ_FINAL : public nsISMILAttr
  {
  public:
    SMILOrient(nsSVGOrientType* aOrientType,
               nsSVGAngle* aAngle,
               nsSVGElement* aSVGElement)
      : mOrientType(aOrientType)
      , mAngle(aAngle)
      , mSVGElement(aSVGElement)
    {}

    // These will stay alive because a nsISMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    nsSVGOrientType* mOrientType;
    nsSVGAngle* mAngle;
    nsSVGElement* mSVGElement;

    // nsISMILAttr methods
    virtual nsresult ValueFromString(const nsAString& aStr,
                                     const nsISMILAnimationElement* aSrcElement,
                                     nsSMILValue& aValue,
                                     bool& aPreventCachingOfSandwich) const;
    virtual nsSMILValue GetBaseValue() const;
    virtual void ClearAnimValue();
    virtual nsresult SetAnimValue(const nsSMILValue& aValue);
  };
};

#endif //__NS_SVGANGLE_H__
