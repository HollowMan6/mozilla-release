/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef COMPILER_ARRAY_BOUNDS_CLAMPER_H_
#define COMPILER_ARRAY_BOUNDS_CLAMPER_H_

#include "GLSLANG/ShaderLang.h"

#include "compiler/InfoSink.h"
#include "compiler/intermediate.h"

class ArrayBoundsClamper {
public:
    ArrayBoundsClamper();

    // Output array clamp function source into the shader source.
    void OutputClampingFunctionDefinition(TInfoSinkBase& out) const;

    // Marks nodes in the tree that index arrays indirectly as
    // requiring clamping.
    void MarkIndirectArrayBoundsForClamping(TIntermNode* root);

    void Cleanup()
    {
        mArrayBoundsClampDefinitionNeeded = false;
    }

private:
    bool GetArrayBoundsClampDefinitionNeeded() const { return mArrayBoundsClampDefinitionNeeded; }
    void SetArrayBoundsClampDefinitionNeeded() { mArrayBoundsClampDefinitionNeeded = true; }
    
    bool mArrayBoundsClampDefinitionNeeded;
};

#endif // COMPILER_ARRAY_BOUNDS_CLAMPER_H_
