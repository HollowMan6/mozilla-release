/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/PerfSpewer.h"

#if defined(__linux__)
# include <unistd.h>
#endif

#ifdef JS_ION_PERF
# include "jit/IonSpewer.h"
# include "jit/LinearScan.h"
# include "jit/LIR.h"
# include "jit/MIR.h"
# include "jit/MIRGraph.h"
#endif

// perf expects its data to be in a file /tmp/perf-PID.map, but for Android
// and B2G the map files are written to /data/local/tmp/perf-PID.map
//
// Except that Android 4.3 no longer allows the browser to write to /data/local/tmp/
// so also try /sdcard/.

#ifndef PERF_SPEW_DIR
# if defined(__ANDROID__)
#  define PERF_SPEW_DIR "/data/local/tmp/"
#  define PERF_SPEW_DIR_2 "/sdcard/"
# else
#  define PERF_SPEW_DIR "/tmp/"
# endif
#endif

using namespace js;
using namespace js::jit;

#define PERF_MODE_NONE  1
#define PERF_MODE_FUNC  2
#define PERF_MODE_BLOCK 3

#ifdef JS_ION_PERF

static uint32_t PerfMode = 0;

static bool PerfChecked = false;

static FILE *PerfFilePtr = nullptr;

#ifdef JS_THREADSAFE
# include "jslock.h"
static PRLock *PerfMutex;
#endif

static bool
openPerfMap(const char *dir)
{
    const ssize_t bufferSize = 256;
    char filenameBuffer[bufferSize];

    if (snprintf(filenameBuffer, bufferSize, "%sperf-%d.map", dir, getpid()) >= bufferSize)
        return false;

    JS_ASSERT(!PerfFilePtr);
    PerfFilePtr = fopen(filenameBuffer, "a");

    if (!PerfFilePtr)
        return false;

    return true;
}

void
js::jit::CheckPerf() {
    if (!PerfChecked) {
        const char *env = getenv("IONPERF");
        if (env == nullptr) {
            PerfMode = PERF_MODE_NONE;
            fprintf(stderr, "Warning: JIT perf reporting requires IONPERF set to \"block\" or \"func\". ");
            fprintf(stderr, "Perf mapping will be deactivated.\n");
        } else if (!strcmp(env, "none")) {
            PerfMode = PERF_MODE_NONE;
        } else if (!strcmp(env, "block")) {
            PerfMode = PERF_MODE_BLOCK;
        } else if (!strcmp(env, "func")) {
            PerfMode = PERF_MODE_FUNC;
        } else {
            fprintf(stderr, "Use IONPERF=func to record at function granularity\n");
            fprintf(stderr, "Use IONPERF=block to record at basic block granularity\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "Be advised that using IONPERF will cause all scripts\n");
            fprintf(stderr, "to be leaked.\n");
            exit(0);
        }

        if (PerfMode != PERF_MODE_NONE) {
#ifdef JS_THREADSAFE
            PerfMutex = PR_NewLock();
            if (!PerfMutex)
                MOZ_CRASH();
#endif

            if (openPerfMap(PERF_SPEW_DIR)) {
                PerfChecked = true;
                return;
            }

#if defined(__ANDROID__)
            if (openPerfMap(PERF_SPEW_DIR_2)) {
                PerfChecked = true;
                return;
            }
#endif
            fprintf(stderr, "Failed to open perf map file.  Disabling IONPERF.\n");
            PerfMode = PERF_MODE_NONE;
        }
        PerfChecked = true;
    }
}

bool
js::jit::PerfBlockEnabled() {
    JS_ASSERT(PerfMode);
    return PerfMode == PERF_MODE_BLOCK;
}

bool
js::jit::PerfFuncEnabled() {
    JS_ASSERT(PerfMode);
    return PerfMode == PERF_MODE_FUNC;
}

static bool
lockPerfMap(void)
{
    if (!PerfEnabled())
        return false;

#ifdef JS_THREADSAFE
    PR_Lock(PerfMutex);
#endif

    JS_ASSERT(PerfFilePtr);
    return true;
}

static void
unlockPerfMap()
{
    JS_ASSERT(PerfFilePtr);
    fflush(PerfFilePtr);
#ifdef JS_THREADSAFE
    PR_Unlock(PerfMutex);
#endif
}

uint32_t PerfSpewer::nextFunctionIndex = 0;

bool
PerfSpewer::startBasicBlock(MBasicBlock *blk,
                            MacroAssembler &masm)
{
    if (!PerfBlockEnabled())
        return true;

    const char *filename = blk->info().script()->filename();
    unsigned lineNumber, columnNumber;
    if (blk->pc()) {
        lineNumber = PCToLineNumber(blk->info().script(),
                                    blk->pc(),
                                    &columnNumber);
    } else {
        lineNumber = 0;
        columnNumber = 0;
    }
    Record r(filename, lineNumber, columnNumber, blk->id());
    masm.bind(&r.start);
    return basicBlocks_.append(r);
}

bool
PerfSpewer::endBasicBlock(MacroAssembler &masm)
{
    if (!PerfBlockEnabled())
        return true;

    masm.bind(&basicBlocks_.back().end);
    return true;
}

bool
PerfSpewer::noteEndInlineCode(MacroAssembler &masm)
{
    if (!PerfBlockEnabled())
        return true;

    masm.bind(&endInlineCode);
    return true;
}

void
PerfSpewer::writeProfile(JSScript *script,
                         JitCode *code,
                         MacroAssembler &masm)
{
    if (PerfFuncEnabled()) {
        if (!lockPerfMap())
            return;

        uint32_t thisFunctionIndex = nextFunctionIndex++;

        size_t size = code->instructionsSize();
        if (size > 0) {
            fprintf(PerfFilePtr, "%zx %zx %s:%d: Func%02d\n",
                    reinterpret_cast<uintptr_t>(code->raw()),
                    size,
                    script->filename(),
                    script->lineno(),
                    thisFunctionIndex);
        }
        unlockPerfMap();
        return;
    }

    if (PerfBlockEnabled() && basicBlocks_.length() > 0) {
        if (!lockPerfMap())
            return;

        uint32_t thisFunctionIndex = nextFunctionIndex++;
        uintptr_t funcStart = uintptr_t(code->raw());
        uintptr_t funcEndInlineCode = funcStart + masm.actualOffset(endInlineCode.offset());
        uintptr_t funcEnd = funcStart + code->instructionsSize();

        // function begins with the prologue, which is located before the first basic block
        size_t prologueSize = masm.actualOffset(basicBlocks_[0].start.offset());

        if (prologueSize > 0) {
            fprintf(PerfFilePtr, "%zx %zx %s:%d: Func%02d-Prologue\n",
                    funcStart, prologueSize, script->filename(), script->lineno(), thisFunctionIndex);
        }

        uintptr_t cur = funcStart + prologueSize;
        for (uint32_t i = 0; i < basicBlocks_.length(); i++) {
            Record &r = basicBlocks_[i];

            uintptr_t blockStart = funcStart + masm.actualOffset(r.start.offset());
            uintptr_t blockEnd = funcStart + masm.actualOffset(r.end.offset());

            JS_ASSERT(cur <= blockStart);
            if (cur < blockStart) {
                fprintf(PerfFilePtr, "%zx %zx %s:%d: Func%02d-Block?\n",
                        static_cast<uintptr_t>(cur),
                        static_cast<uintptr_t>(blockStart - cur),
                        script->filename(), script->lineno(),
                        thisFunctionIndex);
            }
            cur = blockEnd;

            size_t size = blockEnd - blockStart;

            if (size > 0) {
                fprintf(PerfFilePtr, "%zx %zx %s:%d:%d: Func%02d-Block%d\n",
                        static_cast<uintptr_t>(blockStart), size,
                        r.filename, r.lineNumber, r.columnNumber,
                        thisFunctionIndex, r.id);
            }
        }

        JS_ASSERT(cur <= funcEndInlineCode);
        if (cur < funcEndInlineCode) {
            fprintf(PerfFilePtr, "%zx %zx %s:%d: Func%02d-Epilogue\n",
                    cur, funcEndInlineCode - cur,
                    script->filename(), script->lineno(),
                    thisFunctionIndex);
        }

        JS_ASSERT(funcEndInlineCode <= funcEnd);
        if (funcEndInlineCode < funcEnd) {
            fprintf(PerfFilePtr, "%zx %zx %s:%d: Func%02d-OOL\n",
                    funcEndInlineCode, funcEnd - funcEndInlineCode,
                    script->filename(), script->lineno(),
                    thisFunctionIndex);
        }

        unlockPerfMap();
        return;
    }
}

void
js::jit::writePerfSpewerBaselineProfile(JSScript *script, JitCode *code)
{
    if (!PerfEnabled())
        return;

    if (!lockPerfMap())
        return;

    size_t size = code->instructionsSize();
    if (size > 0) {
        fprintf(PerfFilePtr, "%zx %zx %s:%d: Baseline\n",
                reinterpret_cast<uintptr_t>(code->raw()),
                size, script->filename(), script->lineno());
    }

    unlockPerfMap();
}

void
js::jit::writePerfSpewerJitCodeProfile(JitCode *code, const char *msg)
{
    if (!code || !PerfEnabled())
        return;

    if (!lockPerfMap())
        return;

    size_t size = code->instructionsSize();
    if (size > 0) {
        fprintf(PerfFilePtr, "%zx %zx %s (%p 0x%zx)\n",
                reinterpret_cast<uintptr_t>(code->raw()),
                size, msg, code->raw(), size);
    }

    unlockPerfMap();
}

void
js::jit::writePerfSpewerAsmJSFunctionMap(uintptr_t base, uintptr_t size,
                                         const char *filename, unsigned lineno, unsigned colIndex,
                                         const char *funcName)
{
    if (!PerfFuncEnabled() || size == 0U)
        return;

    if (!lockPerfMap())
        return;

    fprintf(PerfFilePtr, "%zx %zx %s:%d:%d: Function %s\n", base, size, filename, lineno, colIndex, funcName);

    unlockPerfMap();
}

bool
AsmJSPerfSpewer::startBasicBlock(MBasicBlock *blk, MacroAssembler &masm)
{
    if (!PerfBlockEnabled())
        return true;

    Record r("", blk->lineno(), blk->columnIndex(), blk->id()); // filename is retrieved later
    masm.bind(&r.start);
    return basicBlocks_.append(r);
}

void
AsmJSPerfSpewer::noteBlocksOffsets()
{
    if (!PerfBlockEnabled())
        return;

    for (uint32_t i = 0; i < basicBlocks_.length(); i++) {
        Record &r = basicBlocks_[i];
        r.startOffset = r.start.offset();
        r.endOffset = r.end.offset();
    }
}

void
js::jit::writePerfSpewerAsmJSBlocksMap(uintptr_t baseAddress, size_t funcStartOffset,
                                       size_t funcEndInlineOffset, size_t funcSize,
                                       const char *filename, const char *funcName,
                                       const js::jit::BasicBlocksVector &basicBlocks)
{
    if (!PerfBlockEnabled() || basicBlocks.empty())
        return;

    if (!lockPerfMap())
        return;

    // function begins with the prologue, which is located before the first basic block
    size_t prologueSize = basicBlocks[0].startOffset - funcStartOffset;
    size_t cur = baseAddress + funcStartOffset + prologueSize;
    size_t funcEndInlineCode = baseAddress + funcEndInlineOffset;
    size_t funcEnd = baseAddress + funcStartOffset + funcSize;

    if (prologueSize > 0) {
        fprintf(PerfFilePtr, "%zx %zx %s: Function %s - Prologue\n",
                baseAddress + funcStartOffset, prologueSize, filename, funcName);
    }

    for (uint32_t i = 0; i < basicBlocks.length(); i++) {
        const Record &r = basicBlocks[i];

        size_t blockStart = baseAddress + r.startOffset;
        size_t blockEnd = baseAddress + r.endOffset;

        JS_ASSERT(cur <= blockStart);
        if (cur < blockStart) {
            fprintf(PerfFilePtr, "%zx %zx %s: Function %s - unknown block\n",
                    cur, blockStart - cur,
                    filename,
                    funcName);
        }
        cur = blockEnd;

        size_t size = blockEnd - blockStart;
        if (size > 0) {
            fprintf(PerfFilePtr, "%zx %zx %s:%d:%d: Function %s - Block %d\n",
                    blockStart, size,
                    filename, r.lineNumber, r.columnNumber,
                    funcName, r.id);
        }
    }

    JS_ASSERT(cur <= funcEndInlineCode);
    if (cur < funcEndInlineCode)
        fprintf(PerfFilePtr, "%zx %zx %s: Function %s - Epilogue\n",
                cur, funcEndInlineCode - cur, filename, funcName);

    JS_ASSERT(funcEndInlineCode <= funcEnd);
    if (funcEndInlineCode < funcEnd) {
        fprintf(PerfFilePtr, "%zx %zx %s: Function %s - OOL\n",
                funcEndInlineCode, funcEnd - funcEndInlineCode, filename, funcName);
    }

    unlockPerfMap();
}

void
js::jit::writePerfSpewerAsmJSEntriesAndExits(uintptr_t base, size_t size)
{
    if (size == 0)
        return;

    if (!lockPerfMap())
        return;

    fprintf(PerfFilePtr, "%zx %zx AsmJS Entries and Exits (0x%zx 0x%zx)\n", base, size, base, size);

    unlockPerfMap();
}

#endif // defined (JS_ION_PERF)
