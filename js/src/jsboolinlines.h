/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsboolinlines_h
#define jsboolinlines_h

#include "jsbool.h"

#include "vm/BooleanObject.h"
#include "vm/WrapperObject.h"

namespace js {

bool
BooleanGetPrimitiveValueSlow(HandleObject, JSContext *);

inline bool
BooleanGetPrimitiveValue(HandleObject obj, JSContext *cx)
{
    if (obj->is<BooleanObject>())
        return obj->as<BooleanObject>().unbox();

    return BooleanGetPrimitiveValueSlow(obj, cx);
}

inline bool
EmulatesUndefined(JSObject *obj)
{
    AutoThreadSafeAccess ts0(obj);
    AutoThreadSafeAccess ts1(obj->typeRaw());
    JSObject *actual = MOZ_LIKELY(!obj->is<WrapperObject>()) ? obj : UncheckedUnwrap(obj);
    return actual->getClass()->emulatesUndefined();
}

} /* namespace js */

#endif /* jsboolinlines_h */
