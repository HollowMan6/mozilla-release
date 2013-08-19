/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=8 sts=4 et sw=4 tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Per JSRuntime object */

#include "mozilla/MemoryReporting.h"
#include "mozilla/Util.h"

#include "xpcprivate.h"
#include "xpcpublic.h"
#include "XPCJSMemoryReporter.h"
#include "WrapperFactory.h"
#include "dom_quickstubs.h"

#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "amIAddonManager.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "prsystem.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Services.h"

#include "nsLayoutStatics.h"
#include "nsContentUtils.h"
#include "nsCxPusher.h"
#include "nsCCUncollectableMarker.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsScriptLoader.h"
#include "jsfriendapi.h"
#include "js/MemoryMetrics.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Element.h"
#include "mozilla/Attributes.h"
#include "AccessCheck.h"
#include "nsGlobalWindow.h"

#include "GeckoProfiler.h"
#include "nsJSPrincipals.h"
#include <algorithm>

#ifdef MOZ_CRASHREPORTER
#include "nsExceptionHandler.h"
#endif

using namespace mozilla;
using namespace xpc;
using namespace JS;

/***************************************************************************/

const char* XPCJSRuntime::mStrings[] = {
    "constructor",          // IDX_CONSTRUCTOR
    "toString",             // IDX_TO_STRING
    "toSource",             // IDX_TO_SOURCE
    "lastResult",           // IDX_LAST_RESULT
    "returnCode",           // IDX_RETURN_CODE
    "value",                // IDX_VALUE
    "QueryInterface",       // IDX_QUERY_INTERFACE
    "Components",           // IDX_COMPONENTS
    "wrappedJSObject",      // IDX_WRAPPED_JSOBJECT
    "Object",               // IDX_OBJECT
    "Function",             // IDX_FUNCTION
    "prototype",            // IDX_PROTOTYPE
    "createInstance",       // IDX_CREATE_INSTANCE
    "item",                 // IDX_ITEM
    "__proto__",            // IDX_PROTO
    "__iterator__",         // IDX_ITERATOR
    "__exposedProps__",     // IDX_EXPOSEDPROPS
};

/***************************************************************************/

struct CX_AND_XPCRT_Data
{
    JSContext* cx;
    XPCJSRuntime* rt;
};

static void * const UNMARK_ONLY = nullptr;
static void * const UNMARK_AND_SWEEP = (void *)1;

static PLDHashOperator
NativeInterfaceSweeper(PLDHashTable *table, PLDHashEntryHdr *hdr,
                       uint32_t number, void *arg)
{
    XPCNativeInterface* iface = ((IID2NativeInterfaceMap::Entry*)hdr)->value;
    if (iface->IsMarked()) {
        iface->Unmark();
        return PL_DHASH_NEXT;
    }

    if (arg == UNMARK_ONLY)
        return PL_DHASH_NEXT;

#ifdef XPC_REPORT_NATIVE_INTERFACE_AND_SET_FLUSHING
    fputs("- Destroying XPCNativeInterface for ", stdout);
    JS_PutString(JSVAL_TO_STRING(iface->GetName()), stdout);
    putc('\n', stdout);
#endif

    XPCNativeInterface::DestroyInstance(iface);
    return PL_DHASH_REMOVE;
}

// *Some* NativeSets are referenced from mClassInfo2NativeSetMap.
// *All* NativeSets are referenced from mNativeSetMap.
// So, in mClassInfo2NativeSetMap we just clear references to the unmarked.
// In mNativeSetMap we clear the references to the unmarked *and* delete them.

static PLDHashOperator
NativeUnMarkedSetRemover(PLDHashTable *table, PLDHashEntryHdr *hdr,
                         uint32_t number, void *arg)
{
    XPCNativeSet* set = ((ClassInfo2NativeSetMap::Entry*)hdr)->value;
    if (set->IsMarked())
        return PL_DHASH_NEXT;
    return PL_DHASH_REMOVE;
}

static PLDHashOperator
NativeSetSweeper(PLDHashTable *table, PLDHashEntryHdr *hdr,
                 uint32_t number, void *arg)
{
    XPCNativeSet* set = ((NativeSetMap::Entry*)hdr)->key_value;
    if (set->IsMarked()) {
        set->Unmark();
        return PL_DHASH_NEXT;
    }

    if (arg == UNMARK_ONLY)
        return PL_DHASH_NEXT;

#ifdef XPC_REPORT_NATIVE_INTERFACE_AND_SET_FLUSHING
    printf("- Destroying XPCNativeSet for:\n");
    uint16_t count = set->GetInterfaceCount();
    for (uint16_t k = 0; k < count; k++) {
        XPCNativeInterface* iface = set->GetInterfaceAt(k);
        fputs("    ", stdout);
        JS_PutString(JSVAL_TO_STRING(iface->GetName()), stdout);
        putc('\n', stdout);
    }
#endif

    XPCNativeSet::DestroyInstance(set);
    return PL_DHASH_REMOVE;
}

static PLDHashOperator
JSClassSweeper(PLDHashTable *table, PLDHashEntryHdr *hdr,
               uint32_t number, void *arg)
{
    XPCNativeScriptableShared* shared =
        ((XPCNativeScriptableSharedMap::Entry*) hdr)->key;
    if (shared->IsMarked()) {
#ifdef off_XPC_REPORT_JSCLASS_FLUSHING
        printf("+ Marked XPCNativeScriptableShared for: %s @ %x\n",
               shared->GetJSClass()->name,
               shared->GetJSClass());
#endif
        shared->Unmark();
        return PL_DHASH_NEXT;
    }

    if (arg == UNMARK_ONLY)
        return PL_DHASH_NEXT;

#ifdef XPC_REPORT_JSCLASS_FLUSHING
    printf("- Destroying XPCNativeScriptableShared for: %s @ %x\n",
           shared->GetJSClass()->name,
           shared->GetJSClass());
#endif

    delete shared;
    return PL_DHASH_REMOVE;
}

static PLDHashOperator
DyingProtoKiller(PLDHashTable *table, PLDHashEntryHdr *hdr,
                 uint32_t number, void *arg)
{
    XPCWrappedNativeProto* proto =
        (XPCWrappedNativeProto*)((PLDHashEntryStub*)hdr)->key;
    delete proto;
    return PL_DHASH_REMOVE;
}

static PLDHashOperator
DetachedWrappedNativeProtoMarker(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                 uint32_t number, void *arg)
{
    XPCWrappedNativeProto* proto =
        (XPCWrappedNativeProto*)((PLDHashEntryStub*)hdr)->key;

    proto->Mark();
    return PL_DHASH_NEXT;
}

bool
XPCJSRuntime::CustomContextCallback(JSContext *cx, unsigned operation)
{
    if (operation == JSCONTEXT_NEW) {
        if (!OnJSContextNew(cx)) {
            return false;
        }
    } else if (operation == JSCONTEXT_DESTROY) {
        delete XPCContext::GetXPCContext(cx);
    }

    nsTArray<xpcContextCallback> callbacks(extraContextCallbacks);
    for (uint32_t i = 0; i < callbacks.Length(); ++i) {
        if (!callbacks[i](cx, operation)) {
            return false;
        }
    }

    return true;
}

class AsyncFreeSnowWhite : public nsRunnable
{
public:
  NS_IMETHOD Run()
  {
      TimeStamp start = TimeStamp::Now();
      bool hadSnowWhiteObjects = nsCycleCollector_doDeferredDeletion();
      Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_ASYNC_SNOW_WHITE_FREEING,
                            uint32_t((TimeStamp::Now() - start).ToMilliseconds()));
      if (hadSnowWhiteObjects && !mContinuation) {
          mContinuation = true;
          if (NS_FAILED(NS_DispatchToCurrentThread(this))) {
              mActive = false;
          }
      } else {
          mActive = false;
      }
      return NS_OK;
  }

  void Dispatch(bool aContinuation = false)
  {
      if (mContinuation) {
          mContinuation = aContinuation;
      }
      if (!mActive && NS_SUCCEEDED(NS_DispatchToCurrentThread(this))) {
          mActive = true;
      }
  }

  AsyncFreeSnowWhite() : mContinuation(false), mActive(false) {}

public:
  bool mContinuation;
  bool mActive;
};

namespace xpc {

CompartmentPrivate::~CompartmentPrivate()
{
    MOZ_COUNT_DTOR(xpc::CompartmentPrivate);
}

bool CompartmentPrivate::TryParseLocationURI()
{
    // Already tried parsing the location before
    if (locationWasParsed)
      return false;
    locationWasParsed = true;

    // Need to parse the URI.
    if (location.IsEmpty())
        return false;

    // Handle Sandbox location strings.
    // A sandbox string looks like this:
    // <sandboxName> (from: <js-stack-frame-filename>:<lineno>)
    // where <sandboxName> is user-provided via Cu.Sandbox()
    // and <js-stack-frame-filename> and <lineno> is the stack frame location
    // from where Cu.Sandbox was called.
    // <js-stack-frame-filename> furthermore is "free form", often using a
    // "uri -> uri -> ..." chain. The following code will and must handle this
    // common case.
    // It should be noted that other parts of the code may already rely on the
    // "format" of these strings, such as the add-on SDK.

    static const nsDependentCString from("(from: ");
    static const nsDependentCString arrow(" -> ");
    static const size_t fromLength = from.Length();
    static const size_t arrowLength = arrow.Length();

    // See: XPCComponents.cpp#AssembleSandboxMemoryReporterName
    int32_t idx = location.Find(from);
    if (idx < 0)
        return TryParseLocationURICandidate(location);


    // When parsing we're looking for the right-most URI. This URI may be in
    // <sandboxName>, so we try this first.
    if (TryParseLocationURICandidate(Substring(location, 0, idx)))
        return true;

    // Not in <sandboxName> so we need to inspect <js-stack-frame-filename> and
    // the chain that is potentially contained within and grab the rightmost
    // item that is actually a URI.

    // First, hack off the :<lineno>) part as well
    int32_t ridx = location.RFind(NS_LITERAL_CSTRING(":"));
    nsAutoCString chain(Substring(location, idx + fromLength,
                                  ridx - idx - fromLength));

    // Loop over the "->" chain. This loop also works for non-chains, or more
    // correctly chains with only one item.
    for (;;) {
        idx = chain.RFind(arrow);
        if (idx < 0) {
            // This is the last chain item. Try to parse what is left.
            return TryParseLocationURICandidate(chain);
        }

        // Try to parse current chain item
        if (TryParseLocationURICandidate(Substring(chain, idx + arrowLength)))
            return true;

        // Current chain item couldn't be parsed.
        // Don't forget whitespace in " -> "
        idx -= 1;
        // Strip current item and continue
        chain = Substring(chain, 0, idx);
    }
    MOZ_ASSUME_UNREACHABLE("Chain parser loop does not terminate");
}

bool CompartmentPrivate::TryParseLocationURICandidate(const nsACString& uristr)
{
    nsCOMPtr<nsIURI> uri;
    if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), uristr)))
        return false;

    nsAutoCString scheme;
    if (NS_FAILED(uri->GetScheme(scheme)))
        return false;

    // Cannot really map data: and blob:.
    // Also, data: URIs are pretty memory hungry, which is kinda bad
    // for memory reporter use.
    if (scheme.EqualsLiteral("data") || scheme.EqualsLiteral("blob"))
        return false;

    locationURI = uri.forget();
    return true;
}

CompartmentPrivate*
EnsureCompartmentPrivate(JSObject *obj)
{
    return EnsureCompartmentPrivate(js::GetObjectCompartment(obj));
}

CompartmentPrivate*
EnsureCompartmentPrivate(JSCompartment *c)
{
    CompartmentPrivate *priv = GetCompartmentPrivate(c);
    if (priv)
        return priv;
    priv = new CompartmentPrivate();
    JS_SetCompartmentPrivate(c, priv);
    return priv;
}

bool
IsXBLScope(JSCompartment *compartment)
{
    // We always eagerly create compartment privates for XBL scopes.
    CompartmentPrivate *priv = GetCompartmentPrivate(compartment);
    if (!priv || !priv->scope)
        return false;
    return priv->scope->IsXBLScope();
}

bool
IsUniversalXPConnectEnabled(JSCompartment *compartment)
{
    CompartmentPrivate *priv = GetCompartmentPrivate(compartment);
    if (!priv)
        return false;
    return priv->universalXPConnectEnabled;
}

bool
IsUniversalXPConnectEnabled(JSContext *cx)
{
    JSCompartment *compartment = js::GetContextCompartment(cx);
    if (!compartment)
        return false;
    return IsUniversalXPConnectEnabled(compartment);
}

bool
EnableUniversalXPConnect(JSContext *cx)
{
    JSCompartment *compartment = js::GetContextCompartment(cx);
    if (!compartment)
        return true;
    // Never set universalXPConnectEnabled on a chrome compartment - it confuses
    // the security wrapping code.
    if (AccessCheck::isChrome(compartment))
        return true;
    CompartmentPrivate *priv = GetCompartmentPrivate(compartment);
    if (!priv)
        return true;
    priv->universalXPConnectEnabled = true;

    // Recompute all the cross-compartment wrappers leaving the newly-privileged
    // compartment.
    bool ok = js::RecomputeWrappers(cx, js::SingleCompartment(compartment),
                                    js::AllCompartments());
    NS_ENSURE_TRUE(ok, false);

    // The Components object normally isn't defined for unprivileged web content,
    // but we define it when UniversalXPConnect is enabled to support legacy
    // tests.
    XPCWrappedNativeScope *scope = priv->scope;
    if (!scope)
        return true;
    return nsXPCComponents::AttachComponentsObject(cx, scope);
}

JSObject *
GetJunkScope()
{
    XPCJSRuntime *self = nsXPConnect::GetRuntimeInstance();
    NS_ENSURE_TRUE(self, nullptr);
    return self->GetJunkScope();
}

nsIGlobalObject *
GetJunkScopeGlobal()
{
    JSObject *junkScope = GetJunkScope();
    // GetJunkScope would ideally never fail, currently it is not yet the case
    // unfortunately...(see Bug 874158)
    if (!junkScope)
        return nullptr;
    return GetNativeForGlobal(junkScope);
}

}

static void
CompartmentDestroyedCallback(JSFreeOp *fop, JSCompartment *compartment)
{
    XPCJSRuntime* self = nsXPConnect::GetRuntimeInstance();
    if (!self)
        return;

    // Get the current compartment private into an AutoPtr (which will do the
    // cleanup for us), and null out the private (which may already be null).
    nsAutoPtr<CompartmentPrivate> priv(GetCompartmentPrivate(compartment));
    JS_SetCompartmentPrivate(compartment, nullptr);
}

void XPCJSRuntime::TraceNativeBlackRoots(JSTracer* trc)
{
    // Skip this part if XPConnect is shutting down. We get into
    // bad locking problems with the thread iteration otherwise.
    if (!nsXPConnect::XPConnect()->IsShuttingDown()) {
        // Trace those AutoMarkingPtr lists!
        if (AutoMarkingPtr *roots = Get()->mAutoRoots)
            roots->TraceJSAll(trc);
    }

    {
        XPCAutoLock lock(mMapLock);

        // XPCJSObjectHolders don't participate in cycle collection, so always
        // trace them here.
        XPCRootSetElem *e;
        for (e = mObjectHolderRoots; e; e = e->GetNextRoot())
            static_cast<XPCJSObjectHolder*>(e)->TraceJS(trc);
    }

    dom::TraceBlackJS(trc, JS_GetGCParameter(Runtime(), JSGC_NUMBER),
                      nsXPConnect::XPConnect()->IsShuttingDown());
}

void XPCJSRuntime::TraceAdditionalNativeGrayRoots(JSTracer *trc)
{
    XPCAutoLock lock(mMapLock);

    XPCWrappedNativeScope::TraceWrappedNativesInAllScopes(trc, this);

    for (XPCRootSetElem *e = mVariantRoots; e ; e = e->GetNextRoot())
        static_cast<XPCTraceableVariant*>(e)->TraceJS(trc);

    for (XPCRootSetElem *e = mWrappedJSRoots; e ; e = e->GetNextRoot())
        static_cast<nsXPCWrappedJS*>(e)->TraceJS(trc);
}

// static
void
XPCJSRuntime::SuspectWrappedNative(XPCWrappedNative *wrapper,
                                   nsCycleCollectionNoteRootCallback &cb)
{
    if (!wrapper->IsValid() || wrapper->IsWrapperExpired())
        return;

    MOZ_ASSERT(NS_IsMainThread(),
               "Suspecting wrapped natives from non-main thread");

    // Only record objects that might be part of a cycle as roots, unless
    // the callback wants all traces (a debug feature).
    JSObject* obj = wrapper->GetFlatJSObjectPreserveColor();
    if (xpc_IsGrayGCThing(obj) || cb.WantAllTraces())
        cb.NoteJSRoot(obj);
}

bool
CanSkipWrappedJS(nsXPCWrappedJS *wrappedJS)
{
    JSObject *obj = wrappedJS->GetJSObjectPreserveColor();
    // If traversing wrappedJS wouldn't release it, nor
    // cause any other objects to be added to the graph, no
    // need to add it to the graph at all.
    bool isRootWrappedJS = wrappedJS->GetRootWrapper() == wrappedJS;
    if (nsCCUncollectableMarker::sGeneration &&
        (!obj || !xpc_IsGrayGCThing(obj)) &&
        !wrappedJS->IsSubjectToFinalization() &&
        (isRootWrappedJS || CanSkipWrappedJS(wrappedJS->GetRootWrapper()))) {
        if (!wrappedJS->IsAggregatedToNative() || !isRootWrappedJS) {
            return true;
        } else {
            nsISupports* agg = wrappedJS->GetAggregatedNativeObject();
            nsXPCOMCycleCollectionParticipant* cp = nullptr;
            CallQueryInterface(agg, &cp);
            nsISupports* canonical = nullptr;
            agg->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                                reinterpret_cast<void**>(&canonical));
            if (cp && canonical && cp->CanSkipInCC(canonical)) {
                return true;
            }
        }
    }
    return false;
}

void
XPCJSRuntime::TraverseAdditionalNativeRoots(nsCycleCollectionNoteRootCallback &cb)
{
    XPCAutoLock lock(mMapLock);

    XPCWrappedNativeScope::SuspectAllWrappers(this, cb);

    for (XPCRootSetElem *e = mVariantRoots; e ; e = e->GetNextRoot()) {
        XPCTraceableVariant* v = static_cast<XPCTraceableVariant*>(e);
        if (nsCCUncollectableMarker::InGeneration(cb,
                                                  v->CCGeneration())) {
           jsval val = v->GetJSValPreserveColor();
           if (val.isObject() && !xpc_IsGrayGCThing(&val.toObject()))
               continue;
        }
        cb.NoteXPCOMRoot(v);
    }

    for (XPCRootSetElem *e = mWrappedJSRoots; e ; e = e->GetNextRoot()) {
        nsXPCWrappedJS *wrappedJS = static_cast<nsXPCWrappedJS*>(e);
        if (!cb.WantAllTraces() &&
            CanSkipWrappedJS(wrappedJS)) {
            continue;
        }

        cb.NoteXPCOMRoot(static_cast<nsIXPConnectWrappedJS *>(wrappedJS));
    }
}

void
XPCJSRuntime::UnmarkSkippableJSHolders()
{
    XPCAutoLock lock(mMapLock);
    CycleCollectedJSRuntime::UnmarkSkippableJSHolders();
}

void
XPCJSRuntime::PrepareForForgetSkippable()
{
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
        obs->NotifyObservers(nullptr, "cycle-collector-forget-skippable", nullptr);
    }
}

void
XPCJSRuntime::PrepareForCollection()
{
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
        obs->NotifyObservers(nullptr, "cycle-collector-begin", nullptr);
    }
}

void
XPCJSRuntime::DispatchDeferredDeletion(bool aContinuation)
{
    mAsyncSnowWhiteFreer->Dispatch(aContinuation);
}

void
xpc_UnmarkSkippableJSHolders()
{
    if (nsXPConnect::XPConnect()->GetRuntime()) {
        nsXPConnect::XPConnect()->GetRuntime()->UnmarkSkippableJSHolders();
    }
}

template<class T> static void
DoDeferredRelease(nsTArray<T> &array)
{
    while (1) {
        uint32_t count = array.Length();
        if (!count) {
            array.Compact();
            break;
        }
        T wrapper = array[count-1];
        array.RemoveElementAt(count-1);
        NS_RELEASE(wrapper);
    }
}

/* static */ void
XPCJSRuntime::GCSliceCallback(JSRuntime *rt,
                              JS::GCProgress progress,
                              const JS::GCDescription &desc)
{
    XPCJSRuntime *self = nsXPConnect::GetRuntimeInstance();
    if (!self)
        return;

#ifdef MOZ_CRASHREPORTER
    CrashReporter::SetGarbageCollecting(progress == JS::GC_CYCLE_BEGIN ||
                                        progress == JS::GC_SLICE_BEGIN);
#endif

    if (self->mPrevGCSliceCallback)
        (*self->mPrevGCSliceCallback)(rt, progress, desc);
}

void
XPCJSRuntime::CustomGCCallback(JSGCStatus status)
{
    nsTArray<xpcGCCallback> callbacks(extraGCCallbacks);
    for (uint32_t i = 0; i < callbacks.Length(); ++i)
        callbacks[i](status);
}

/* static */ void
XPCJSRuntime::FinalizeCallback(JSFreeOp *fop, JSFinalizeStatus status, bool isCompartmentGC)
{
    XPCJSRuntime* self = nsXPConnect::GetRuntimeInstance();
    if (!self)
        return;

    switch (status) {
        case JSFINALIZE_GROUP_START:
        {
            MOZ_ASSERT(!self->mDoingFinalization, "bad state");

            // mThreadRunningGC indicates that GC is running
            { // scoped lock
                XPCAutoLock lock(self->GetMapLock());
                MOZ_ASSERT(!self->mThreadRunningGC, "bad state");
                self->mThreadRunningGC = PR_GetCurrentThread();
            }

            nsTArray<nsXPCWrappedJS*>* dyingWrappedJSArray =
                &self->mWrappedJSToReleaseArray;

            // Add any wrappers whose JSObjects are to be finalized to
            // this array. Note that we do not want to be changing the
            // refcount of these wrappers.
            // We add them to the array now and Release the array members
            // later to avoid the posibility of doing any JS GCThing
            // allocations during the gc cycle.
            self->mWrappedJSMap->FindDyingJSObjects(dyingWrappedJSArray);

            // Find dying scopes.
            XPCWrappedNativeScope::StartFinalizationPhaseOfGC(fop, self);

            XPCStringConvert::ClearCache();

            self->mDoingFinalization = true;
            break;
        }
        case JSFINALIZE_GROUP_END:
        {
            MOZ_ASSERT(self->mDoingFinalization, "bad state");
            self->mDoingFinalization = false;

            // Release all the members whose JSObjects are now known
            // to be dead.
            DoDeferredRelease(self->mWrappedJSToReleaseArray);

            // Sweep scopes needing cleanup
            XPCWrappedNativeScope::FinishedFinalizationPhaseOfGC();

            // mThreadRunningGC indicates that GC is running.
            // Clear it and notify waiters.
            { // scoped lock
                XPCAutoLock lock(self->GetMapLock());
                MOZ_ASSERT(self->mThreadRunningGC == PR_GetCurrentThread(), "bad state");
                self->mThreadRunningGC = nullptr;
                xpc_NotifyAll(self->GetMapLock());
            }

            break;
        }
        case JSFINALIZE_COLLECTION_END:
        {
            // mThreadRunningGC indicates that GC is running
            { // scoped lock
                XPCAutoLock lock(self->GetMapLock());
                MOZ_ASSERT(!self->mThreadRunningGC, "bad state");
                self->mThreadRunningGC = PR_GetCurrentThread();
            }

#ifdef XPC_REPORT_NATIVE_INTERFACE_AND_SET_FLUSHING
            printf("--------------------------------------------------------------\n");
            int setsBefore = (int) self->mNativeSetMap->Count();
            int ifacesBefore = (int) self->mIID2NativeInterfaceMap->Count();
#endif

            // We use this occasion to mark and sweep NativeInterfaces,
            // NativeSets, and the WrappedNativeJSClasses...

            // Do the marking...
            XPCWrappedNativeScope::MarkAllWrappedNativesAndProtos();

            self->mDetachedWrappedNativeProtoMap->
                Enumerate(DetachedWrappedNativeProtoMarker, nullptr);

            DOM_MarkInterfaces();

            // Mark the sets used in the call contexts. There is a small
            // chance that a wrapper's set will change *while* a call is
            // happening which uses that wrapper's old interfface set. So,
            // we need to do this marking to avoid collecting those sets
            // that might no longer be otherwise reachable from the wrappers
            // or the wrapperprotos.

            // Skip this part if XPConnect is shutting down. We get into
            // bad locking problems with the thread iteration otherwise.
            if (!nsXPConnect::XPConnect()->IsShuttingDown()) {

                // Mark those AutoMarkingPtr lists!
                if (AutoMarkingPtr *roots = Get()->mAutoRoots)
                    roots->MarkAfterJSFinalizeAll();

                XPCCallContext* ccxp = XPCJSRuntime::Get()->GetCallContext();
                while (ccxp) {
                    // Deal with the strictness of callcontext that
                    // complains if you ask for a set when
                    // it is in a state where the set could not
                    // possibly be valid.
                    if (ccxp->CanGetSet()) {
                        XPCNativeSet* set = ccxp->GetSet();
                        if (set)
                            set->Mark();
                    }
                    if (ccxp->CanGetInterface()) {
                        XPCNativeInterface* iface = ccxp->GetInterface();
                        if (iface)
                            iface->Mark();
                    }
                    ccxp = ccxp->GetPrevCallContext();
                }
            }

            // Do the sweeping. During a compartment GC, only
            // WrappedNativeProtos in collected compartments will be
            // marked. Therefore, some reachable NativeInterfaces will not be
            // marked, so it is not safe to sweep them. We still need to unmark
            // them, since the ones pointed to by WrappedNativeProtos in a
            // compartment being collected will be marked.
            //
            // Ideally, if NativeInterfaces from different compartments were
            // kept separate, we could sweep only the ones belonging to
            // compartments being collected. Currently, though, NativeInterfaces
            // are shared between compartments. This ought to be fixed.
            void *sweepArg = isCompartmentGC ? UNMARK_ONLY : UNMARK_AND_SWEEP;

            // We don't want to sweep the JSClasses at shutdown time.
            // At this point there may be JSObjects using them that have
            // been removed from the other maps.
            if (!nsXPConnect::XPConnect()->IsShuttingDown()) {
                self->mNativeScriptableSharedMap->
                    Enumerate(JSClassSweeper, sweepArg);
            }

            if (!isCompartmentGC) {
                self->mClassInfo2NativeSetMap->
                    Enumerate(NativeUnMarkedSetRemover, nullptr);
            }

            self->mNativeSetMap->
                Enumerate(NativeSetSweeper, sweepArg);

            self->mIID2NativeInterfaceMap->
                Enumerate(NativeInterfaceSweeper, sweepArg);

#ifdef DEBUG
            XPCWrappedNativeScope::ASSERT_NoInterfaceSetsAreMarked();
#endif

#ifdef XPC_REPORT_NATIVE_INTERFACE_AND_SET_FLUSHING
            int setsAfter = (int) self->mNativeSetMap->Count();
            int ifacesAfter = (int) self->mIID2NativeInterfaceMap->Count();

            printf("\n");
            printf("XPCNativeSets:        before: %d  collected: %d  remaining: %d\n",
                   setsBefore, setsBefore - setsAfter, setsAfter);
            printf("XPCNativeInterfaces:  before: %d  collected: %d  remaining: %d\n",
                   ifacesBefore, ifacesBefore - ifacesAfter, ifacesAfter);
            printf("--------------------------------------------------------------\n");
#endif

            // Now we are going to recycle any unused WrappedNativeTearoffs.
            // We do this by iterating all the live callcontexts
            // and marking the tearoffs in use. And then we
            // iterate over all the WrappedNative wrappers and sweep their
            // tearoffs.
            //
            // This allows us to perhaps minimize the growth of the
            // tearoffs. And also makes us not hold references to interfaces
            // on our wrapped natives that we are not actually using.
            //
            // XXX We may decide to not do this on *every* gc cycle.

            // Skip this part if XPConnect is shutting down. We get into
            // bad locking problems with the thread iteration otherwise.
            if (!nsXPConnect::XPConnect()->IsShuttingDown()) {
                // Do the marking...

                XPCCallContext* ccxp = XPCJSRuntime::Get()->GetCallContext();
                while (ccxp) {
                    // Deal with the strictness of callcontext that
                    // complains if you ask for a tearoff when
                    // it is in a state where the tearoff could not
                    // possibly be valid.
                    if (ccxp->CanGetTearOff()) {
                        XPCWrappedNativeTearOff* to =
                            ccxp->GetTearOff();
                        if (to)
                            to->Mark();
                    }
                    ccxp = ccxp->GetPrevCallContext();
                }

                // Do the sweeping...
                XPCWrappedNativeScope::SweepAllWrappedNativeTearOffs();
            }

            // Now we need to kill the 'Dying' XPCWrappedNativeProtos.
            // We transfered these native objects to this table when their
            // JSObject's were finalized. We did not destroy them immediately
            // at that point because the ordering of JS finalization is not
            // deterministic and we did not yet know if any wrappers that
            // might still be referencing the protos where still yet to be
            // finalized and destroyed. We *do* know that the protos'
            // JSObjects would not have been finalized if there were any
            // wrappers that referenced the proto but where not themselves
            // slated for finalization in this gc cycle. So... at this point
            // we know that any and all wrappers that might have been
            // referencing the protos in the dying list are themselves dead.
            // So, we can safely delete all the protos in the list.

            self->mDyingWrappedNativeProtoMap->
                Enumerate(DyingProtoKiller, nullptr);

            // mThreadRunningGC indicates that GC is running.
            // Clear it and notify waiters.
            { // scoped lock
                XPCAutoLock lock(self->GetMapLock());
                MOZ_ASSERT(self->mThreadRunningGC == PR_GetCurrentThread(), "bad state");
                self->mThreadRunningGC = nullptr;
                xpc_NotifyAll(self->GetMapLock());
            }

            break;
        }
    }
}

static void WatchdogMain(void *arg);
class Watchdog;
class WatchdogManager;
class AutoLockWatchdog {
    Watchdog* const mWatchdog;
  public:
    AutoLockWatchdog(Watchdog* aWatchdog);
    ~AutoLockWatchdog();
};

class Watchdog
{
  public:
    Watchdog(WatchdogManager *aManager)
      : mManager(aManager)
      , mLock(nullptr)
      , mWakeup(nullptr)
      , mThread(nullptr)
      , mHibernating(false)
      , mInitialized(false)
      , mShuttingDown(false)
    {}
    ~Watchdog() { MOZ_ASSERT(!Initialized()); }

    WatchdogManager* Manager() { return mManager; }
    bool Initialized() { return mInitialized; }
    bool ShuttingDown() { return mShuttingDown; }
    PRLock *GetLock() { return mLock; }
    bool Hibernating() { return mHibernating; }
    void WakeUp()
    {
        MOZ_ASSERT(Initialized());
        MOZ_ASSERT(Hibernating());
        mHibernating = false;
        PR_NotifyCondVar(mWakeup);
    }

    //
    // Invoked by the main thread only.
    //

    void Init()
    {
        MOZ_ASSERT(NS_IsMainThread());
        mLock = PR_NewLock();
        if (!mLock)
            NS_RUNTIMEABORT("PR_NewLock failed.");
        mWakeup = PR_NewCondVar(mLock);
        if (!mWakeup)
            NS_RUNTIMEABORT("PR_NewCondVar failed.");

        {
            AutoLockWatchdog lock(this);

            mThread = PR_CreateThread(PR_USER_THREAD, WatchdogMain, this,
                                      PR_PRIORITY_NORMAL, PR_LOCAL_THREAD,
                                      PR_UNJOINABLE_THREAD, 0);
            if (!mThread)
                NS_RUNTIMEABORT("PR_CreateThread failed!");

            // WatchdogMain acquires the lock and then asserts mInitialized. So
            // make sure to set mInitialized before releasing the lock here so
            // that it's atomic with the creation of the thread.
            mInitialized = true;
        }
    }

    void Shutdown()
    {
        MOZ_ASSERT(NS_IsMainThread());
        MOZ_ASSERT(Initialized());
        {   // Scoped lock.
            AutoLockWatchdog lock(this);

            // Signal to the watchdog thread that it's time to shut down.
            mShuttingDown = true;

            // Wake up the watchdog, and wait for it to call us back.
            PR_NotifyCondVar(mWakeup);
            PR_WaitCondVar(mWakeup, PR_INTERVAL_NO_TIMEOUT);
            MOZ_ASSERT(!mShuttingDown);
        }

        // Destroy state.
        mThread = nullptr;
        PR_DestroyCondVar(mWakeup);
        mWakeup = nullptr;
        PR_DestroyLock(mLock);
        mLock = nullptr;

        // All done.
        mInitialized = false;
    }

    //
    // Invoked by the watchdog thread only.
    //

    void Hibernate()
    {
        MOZ_ASSERT(!NS_IsMainThread());
        mHibernating = true;
        Sleep(PR_INTERVAL_NO_TIMEOUT);
    }
    void Sleep(PRIntervalTime timeout)
    {
        MOZ_ASSERT(!NS_IsMainThread());
        MOZ_ALWAYS_TRUE(PR_WaitCondVar(mWakeup, timeout) == PR_SUCCESS);
    }
    void Finished()
    {
        MOZ_ASSERT(!NS_IsMainThread());
        mShuttingDown = false;
        PR_NotifyCondVar(mWakeup);
    }

  private:
    WatchdogManager *mManager;

    PRLock *mLock;
    PRCondVar *mWakeup;
    PRThread *mThread;
    bool mHibernating;
    bool mInitialized;
    bool mShuttingDown;
};

class WatchdogManager : public nsIObserver
{
  public:

    NS_DECL_ISUPPORTS
    WatchdogManager(XPCJSRuntime *aRuntime) : mRuntime(aRuntime)
                                            , mRuntimeState(RUNTIME_INACTIVE)
    {
        // All the timestamps start at zero except for runtime state change.
        PodArrayZero(mTimestamps);
        mTimestamps[TimestampRuntimeStateChange] = PR_Now();

        // Enable the watchdog, if appropriate.
        RefreshWatchdog();

        // Register ourselves as an observer to get updates on the pref.
        mozilla::Preferences::AddStrongObserver(this, "dom.use_watchdog");
    }
    virtual ~WatchdogManager()
    {
        // Shutting down the watchdog requires context-switching to the watchdog
        // thread, which isn't great to do in a destructor. So we require
        // consumers to shut it down manually before releasing it.
        MOZ_ASSERT(!mWatchdog);
        mozilla::Preferences::RemoveObserver(this, "dom.use_watchdog");
    }

    NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                       const PRUnichar* aData)
    {
        RefreshWatchdog();
        return NS_OK;
    }

    // Runtime statistics. These live on the watchdog manager, are written
    // from the main thread, and are read from the watchdog thread (holding
    // the lock in each case).
    void
    RecordRuntimeActivity(bool active)
    {
        // The watchdog reads this state, so acquire the lock before writing it.
        MOZ_ASSERT(NS_IsMainThread());
        Maybe<AutoLockWatchdog> lock;
        if (mWatchdog)
            lock.construct(mWatchdog);

        // Write state.
        mTimestamps[TimestampRuntimeStateChange] = PR_Now();
        mRuntimeState = active ? RUNTIME_ACTIVE : RUNTIME_INACTIVE;

        // The watchdog may be hibernating, waiting for the runtime to go
        // active. Wake it up if necessary.
        if (active && mWatchdog && mWatchdog->Hibernating())
            mWatchdog->WakeUp();
    }
    bool IsRuntimeActive() { return mRuntimeState == RUNTIME_ACTIVE; }
    PRTime TimeSinceLastRuntimeStateChange()
    {
        return PR_Now() - GetTimestamp(TimestampRuntimeStateChange);
    }

    // Note - Because of the runtime activity timestamp, these are read and
    // written from both threads.
    void RecordTimestamp(WatchdogTimestampCategory aCategory)
    {
        // The watchdog thread always holds the lock when it runs.
        Maybe<AutoLockWatchdog> maybeLock;
        if (NS_IsMainThread() && mWatchdog)
            maybeLock.construct(mWatchdog);
        mTimestamps[aCategory] = PR_Now();
    }
    PRTime GetTimestamp(WatchdogTimestampCategory aCategory)
    {
        // The watchdog thread always holds the lock when it runs.
        Maybe<AutoLockWatchdog> maybeLock;
        if (NS_IsMainThread() && mWatchdog)
            maybeLock.construct(mWatchdog);
        return mTimestamps[aCategory];
    }

    XPCJSRuntime* Runtime() { return mRuntime; }
    Watchdog* GetWatchdog() { return mWatchdog; }

    void RefreshWatchdog()
    {
        bool wantWatchdog = Preferences::GetBool("dom.use_watchdog", true);
        if (wantWatchdog == !!mWatchdog)
            return;
        if (wantWatchdog)
            StartWatchdog();
        else
            StopWatchdog();
    }

    void StartWatchdog()
    {
        MOZ_ASSERT(!mWatchdog);
        mWatchdog = new Watchdog(this);
        mWatchdog->Init();
    }

    void StopWatchdog()
    {
        MOZ_ASSERT(mWatchdog);
        mWatchdog->Shutdown();
        mWatchdog = nullptr;
    }

  private:
    XPCJSRuntime *mRuntime;
    nsAutoPtr<Watchdog> mWatchdog;

    enum { RUNTIME_ACTIVE, RUNTIME_INACTIVE } mRuntimeState;
    PRTime mTimestamps[TimestampCount];
};

NS_IMPL_ISUPPORTS1(WatchdogManager, nsIObserver)

AutoLockWatchdog::AutoLockWatchdog(Watchdog *aWatchdog) : mWatchdog(aWatchdog)
{
    PR_Lock(mWatchdog->GetLock());
}

AutoLockWatchdog::~AutoLockWatchdog()
{
    PR_Unlock(mWatchdog->GetLock());
}

static void
WatchdogMain(void *arg)
{
    PR_SetCurrentThreadName("JS Watchdog");

    Watchdog* self = static_cast<Watchdog*>(arg);
    WatchdogManager* manager = self->Manager();

    // Lock lasts until we return
    AutoLockWatchdog lock(self);

    MOZ_ASSERT(self->Initialized());
    MOZ_ASSERT(!self->ShuttingDown());
    while (!self->ShuttingDown()) {
        // Sleep only 1 second if recently (or currently) active; otherwise, hibernate
        if (manager->IsRuntimeActive() ||
            manager->TimeSinceLastRuntimeStateChange() <= PRTime(2*PR_USEC_PER_SEC))
        {
            self->Sleep(PR_TicksPerSecond());
        } else {
            manager->RecordTimestamp(TimestampWatchdogHibernateStart);
            self->Hibernate();
            manager->RecordTimestamp(TimestampWatchdogHibernateStop);
        }

        // Rise and shine.
        manager->RecordTimestamp(TimestampWatchdogWakeup);

        // Don't trigger the operation callback if activity started less than one second ago.
        // The callback is only used for detecting long running scripts, and triggering the
        // callback from off the main thread can be expensive.
        if (manager->IsRuntimeActive() &&
            manager->TimeSinceLastRuntimeStateChange() >= PRTime(PR_USEC_PER_SEC))
        {
            JS_TriggerOperationCallback(manager->Runtime()->Runtime());
        }
    }

    // Tell the manager that we've shut down.
    self->Finished();
}

PRTime
XPCJSRuntime::GetWatchdogTimestamp(WatchdogTimestampCategory aCategory)
{
    return mWatchdogManager->GetTimestamp(aCategory);
}

NS_EXPORT_(void)
xpc::SimulateActivityCallback(bool aActive)
{
    XPCJSRuntime::ActivityCallback(XPCJSRuntime::Get(), aActive);
}

//static
void
XPCJSRuntime::ActivityCallback(void *arg, bool active)
{
    XPCJSRuntime* self = static_cast<XPCJSRuntime*>(arg);
    self->mWatchdogManager->RecordRuntimeActivity(active);
}

// static
//
// JS-CTypes creates and caches a JSContext that it uses when executing JS
// callbacks. When we're notified that ctypes is about to call into some JS,
// push the cx to maintain the integrity of the context stack.
void
XPCJSRuntime::CTypesActivityCallback(JSContext *cx, js::CTypesActivityType type)
{
  if (type == js::CTYPES_CALLBACK_BEGIN) {
    if (!xpc::PushJSContextNoScriptContext(cx))
      MOZ_CRASH();
  } else if (type == js::CTYPES_CALLBACK_END) {
    xpc::PopJSContextNoScriptContext();
  }
}

// static
bool
XPCJSRuntime::OperationCallback(JSContext *cx)
{
    XPCJSRuntime *self = XPCJSRuntime::Get();

    // If this is the first time the operation callback has fired since we last
    // returned to the event loop, mark the checkpoint.
    if (self->mSlowScriptCheckpoint.IsNull()) {
        self->mSlowScriptCheckpoint = TimeStamp::NowLoRes();
        return true;
    }

    // This is at least the second operation callback we've received since
    // returning to the event loop. See how long it's been, and what the limit
    // is.
    TimeDuration duration = TimeStamp::NowLoRes() - self->mSlowScriptCheckpoint;
    bool chrome =
      nsContentUtils::IsSystemPrincipal(nsContentUtils::GetSubjectPrincipal());
    const char *prefName = chrome ? "dom.max_chrome_script_run_time"
                                  : "dom.max_script_run_time";
    int32_t limit = Preferences::GetInt(prefName, chrome ? 20 : 10);

    // If there's no limit, or we're within the limit, let it go.
    if (limit == 0 || duration.ToSeconds() < limit)
        return true;

    //
    // This has gone on long enough! Time to take action. ;-)
    //

    // Get the DOM window associated with the running script. If the script is
    // running in a non-DOM scope, we have to just let it keep running.
    RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    nsCOMPtr<nsPIDOMWindow> win;
    if (IS_WN_REFLECTOR(global))
        win = do_QueryWrappedNative(XPCWrappedNative::Get(global));
    if (!win)
        return true;

    // Show the prompt to the user, and kill if requested.
    nsGlobalWindow::SlowScriptResponse response =
      static_cast<nsGlobalWindow*>(win.get())->ShowSlowScriptDialog();
    if (response == nsGlobalWindow::KillSlowScript)
        return false;

    // The user chose to continue the script. Reset the timer, and disable this
    // machinery with a pref of the user opted out of future slow-script dialogs.
    self->mSlowScriptCheckpoint = TimeStamp::NowLoRes();
    if (response == nsGlobalWindow::AlwaysContinueSlowScript)
        Preferences::SetInt(prefName, 0);

    return true;
}

size_t
XPCJSRuntime::SizeOfIncludingThis(MallocSizeOf mallocSizeOf)
{
    size_t n = 0;
    n += mallocSizeOf(this);
    n += mWrappedJSMap->SizeOfIncludingThis(mallocSizeOf);
    n += mIID2NativeInterfaceMap->SizeOfIncludingThis(mallocSizeOf);
    n += mClassInfo2NativeSetMap->ShallowSizeOfIncludingThis(mallocSizeOf);
    n += mNativeSetMap->SizeOfIncludingThis(mallocSizeOf);

    n += CycleCollectedJSRuntime::SizeOfExcludingThis(mallocSizeOf);

    // There are other XPCJSRuntime members that could be measured; the above
    // ones have been seen by DMD to be worth measuring.  More stuff may be
    // added later.

    return n;
}

XPCReadableJSStringWrapper *
XPCJSRuntime::NewStringWrapper(const PRUnichar *str, uint32_t len)
{
    for (uint32_t i = 0; i < XPCCCX_STRING_CACHE_SIZE; ++i) {
        StringWrapperEntry& ent = mScratchStrings[i];

        if (!ent.mInUse) {
            ent.mInUse = true;

            // Construct the string using placement new.

            return new (ent.mString.addr()) XPCReadableJSStringWrapper(str, len);
        }
    }

    // All our internal string wrappers are used, allocate a new string.

    return new XPCReadableJSStringWrapper(str, len);
}

void
XPCJSRuntime::DeleteString(nsAString *string)
{
    for (uint32_t i = 0; i < XPCCCX_STRING_CACHE_SIZE; ++i) {
        StringWrapperEntry& ent = mScratchStrings[i];
        if (string == ent.mString.addr()) {
            // One of our internal strings is no longer in use, mark
            // it as such and destroy the string.

            ent.mInUse = false;
            ent.mString.addr()->~XPCReadableJSStringWrapper();

            return;
        }
    }

    // We're done with a string that's not one of our internal
    // strings, delete it.
    delete string;
}

/***************************************************************************/

#ifdef XPC_CHECK_WRAPPERS_AT_SHUTDOWN
static PLDHashOperator
DEBUG_WrapperChecker(PLDHashTable *table, PLDHashEntryHdr *hdr,
                     uint32_t number, void *arg)
{
    XPCWrappedNative* wrapper = (XPCWrappedNative*)((PLDHashEntryStub*)hdr)->key;
    MOZ_ASSERT(!wrapper->IsValid(), "found a 'valid' wrapper!");
    ++ *((int*)arg);
    return PL_DHASH_NEXT;
}
#endif

static PLDHashOperator
DetachedWrappedNativeProtoShutdownMarker(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                         uint32_t number, void *arg)
{
    XPCWrappedNativeProto* proto =
        (XPCWrappedNativeProto*)((PLDHashEntryStub*)hdr)->key;

    proto->SystemIsBeingShutDown();
    return PL_DHASH_NEXT;
}

void XPCJSRuntime::DestroyJSContextStack()
{
    delete mJSContextStack;
    mJSContextStack = nullptr;
}

void XPCJSRuntime::SystemIsBeingShutDown()
{
    DOM_ClearInterfaces();

    if (mDetachedWrappedNativeProtoMap)
        mDetachedWrappedNativeProtoMap->
            Enumerate(DetachedWrappedNativeProtoShutdownMarker, nullptr);
}

XPCJSRuntime::~XPCJSRuntime()
{
    JS::SetGCSliceCallback(Runtime(), mPrevGCSliceCallback);

    xpc_DelocalizeRuntime(Runtime());

    if (mWatchdogManager->GetWatchdog())
        mWatchdogManager->StopWatchdog();

    if (mCallContext)
        mCallContext->SystemIsBeingShutDown();

#ifdef XPC_DUMP_AT_SHUTDOWN
    {
    // count the total JSContexts in use
    JSContext* iter = nullptr;
    int count = 0;
    while (JS_ContextIterator(Runtime(), &iter))
        count ++;
    if (count)
        printf("deleting XPCJSRuntime with %d live JSContexts\n", count);
    }
#endif

    // clean up and destroy maps...
    if (mWrappedJSMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mWrappedJSMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live wrapped JSObject\n", (int)count);
#endif
        mWrappedJSMap->ShutdownMarker(Runtime());
        delete mWrappedJSMap;
    }

    if (mWrappedJSClassMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mWrappedJSClassMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live nsXPCWrappedJSClass\n", (int)count);
#endif
        delete mWrappedJSClassMap;
    }

    if (mIID2NativeInterfaceMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mIID2NativeInterfaceMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live XPCNativeInterfaces\n", (int)count);
#endif
        delete mIID2NativeInterfaceMap;
    }

    if (mClassInfo2NativeSetMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mClassInfo2NativeSetMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live XPCNativeSets\n", (int)count);
#endif
        delete mClassInfo2NativeSetMap;
    }

    if (mNativeSetMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mNativeSetMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live XPCNativeSets\n", (int)count);
#endif
        delete mNativeSetMap;
    }

    if (mMapLock)
        XPCAutoLock::DestroyLock(mMapLock);

    if (mThisTranslatorMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mThisTranslatorMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live ThisTranslator\n", (int)count);
#endif
        delete mThisTranslatorMap;
    }

#ifdef XPC_CHECK_WRAPPERS_AT_SHUTDOWN
    if (DEBUG_WrappedNativeHashtable) {
        int LiveWrapperCount = 0;
        PL_DHashTableEnumerate(DEBUG_WrappedNativeHashtable,
                               DEBUG_WrapperChecker, &LiveWrapperCount);
        if (LiveWrapperCount)
            printf("deleting XPCJSRuntime with %d live XPCWrappedNative (found in wrapper check)\n", (int)LiveWrapperCount);
        PL_DHashTableDestroy(DEBUG_WrappedNativeHashtable);
    }
#endif

    if (mNativeScriptableSharedMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mNativeScriptableSharedMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live XPCNativeScriptableShared\n", (int)count);
#endif
        delete mNativeScriptableSharedMap;
    }

    if (mDyingWrappedNativeProtoMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mDyingWrappedNativeProtoMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live but dying XPCWrappedNativeProto\n", (int)count);
#endif
        delete mDyingWrappedNativeProtoMap;
    }

    if (mDetachedWrappedNativeProtoMap) {
#ifdef XPC_DUMP_AT_SHUTDOWN
        uint32_t count = mDetachedWrappedNativeProtoMap->Count();
        if (count)
            printf("deleting XPCJSRuntime with %d live detached XPCWrappedNativeProto\n", (int)count);
#endif
        delete mDetachedWrappedNativeProtoMap;
    }

#ifdef MOZ_ENABLE_PROFILER_SPS
    // Tell the profiler that the runtime is gone
    if (PseudoStack *stack = mozilla_get_pseudo_stack())
        stack->sampleRuntime(nullptr);
#endif

#ifdef DEBUG
    for (uint32_t i = 0; i < XPCCCX_STRING_CACHE_SIZE; ++i) {
        MOZ_ASSERT(!mScratchStrings[i].mInUse, "Uh, string wrapper still in use!");
    }
#endif
}

static void
GetCompartmentName(JSCompartment *c, nsCString &name, bool replaceSlashes)
{
    if (js::IsAtomsCompartment(c)) {
        name.AssignLiteral("atoms");
    } else if (JSPrincipals *principals = JS_GetCompartmentPrincipals(c)) {
        nsJSPrincipals::get(principals)->GetScriptLocation(name);

        // If the compartment's location (name) differs from the principal's
        // script location, append the compartment's location to allow
        // differentiation of multiple compartments owned by the same principal
        // (e.g. components owned by the system or null principal).
        CompartmentPrivate *compartmentPrivate = GetCompartmentPrivate(c);
        if (compartmentPrivate) {
            const nsACString& location = compartmentPrivate->GetLocation();
            if (!location.IsEmpty() && !location.Equals(name)) {
                name.AppendLiteral(", ");
                name.Append(location);
            }
        }

        // A hack: replace forward slashes with '\\' so they aren't
        // treated as path separators.  Users of the reporters
        // (such as about:memory) have to undo this change.
        if (replaceSlashes)
            name.ReplaceChar('/', '\\');
    } else {
        name.AssignLiteral("null-principal");
    }
}

static int64_t
GetGCChunkTotalBytes()
{
    JSRuntime *rt = nsXPConnect::GetRuntimeInstance()->Runtime();
    return int64_t(JS_GetGCParameter(rt, JSGC_TOTAL_CHUNKS)) * js::gc::ChunkSize;
}

// Telemetry relies on this memory reporter being a single-reporter (rather
// than part of the "js" multi-reporter, which is too slow to run during a
// telemetry ping).
NS_MEMORY_REPORTER_IMPLEMENT(XPConnectJSGCHeap,
                             "js-gc-heap",
                             KIND_OTHER,
                             nsIMemoryReporter::UNITS_BYTES,
                             GetGCChunkTotalBytes,
                             "Memory used by the garbage-collected JavaScript heap.")

static int64_t
GetJSSystemCompartmentCount()
{
    return JS::SystemCompartmentCount(nsXPConnect::GetRuntimeInstance()->Runtime());
}

static int64_t
GetJSUserCompartmentCount()
{
    return JS::UserCompartmentCount(nsXPConnect::GetRuntimeInstance()->Runtime());
}

// Nb: js-system-compartment-count + js-user-compartment-count could be
// different to the number of compartments reported by
// JSMemoryMultiReporter if a garbage collection occurred
// between them being consulted.  We could move these reporters into
// XPConnectJSCompartmentCount to avoid that problem, but then we couldn't
// easily report them via telemetry, so we live with the small risk of
// inconsistencies.
NS_MEMORY_REPORTER_IMPLEMENT(XPConnectJSSystemCompartmentCount,
    "js-compartments/system",
    KIND_OTHER,
    nsIMemoryReporter::UNITS_COUNT,
    GetJSSystemCompartmentCount,
    "The number of JavaScript compartments for system code.  The sum of this "
    "and 'js-compartments-user' might not match the number of compartments "
    "listed under 'js' if a garbage collection occurs at an inopportune time, "
    "but such cases should be rare.")

NS_MEMORY_REPORTER_IMPLEMENT(XPConnectJSUserCompartmentCount,
    "js-compartments/user",
    KIND_OTHER,
    nsIMemoryReporter::UNITS_COUNT,
    GetJSUserCompartmentCount,
    "The number of JavaScript compartments for user code.  The sum of this "
    "and 'js-compartments-system' might not match the number of compartments "
    "listed under 'js' if a garbage collection occurs at an inopportune time, "
    "but such cases should be rare.")

static int64_t
GetJSMainRuntimeTemporaryPeakSize()
{
    return JS::PeakSizeOfTemporary(nsXPConnect::GetRuntimeInstance()->Runtime());
}

// This is also a single reporter so it can be used by telemetry.
NS_MEMORY_REPORTER_IMPLEMENT(JSMainRuntimeTemporaryPeak,
    "js-main-runtime-temporary-peak",
    KIND_OTHER,
    nsIMemoryReporter::UNITS_BYTES,
    GetJSMainRuntimeTemporaryPeakSize,
    "The peak size of the transient storage in the main JSRuntime (the "
    "current size of which is reported as "
    "'explicit/js-non-window/runtime/temporary').");

// The REPORT* macros do an unconditional report.  The ZCREPORT* macros are for
// compartments and zones; they aggregate any entries smaller than
// SUNDRIES_THRESHOLD into the "sundries/gc-heap" and "sundries/malloc-heap"
// entries for the compartment.

#define SUNDRIES_THRESHOLD js::MemoryReportingSundriesThreshold()

#define REPORT(_path, _kind, _units, _amount, _desc)                          \
    do {                                                                      \
        nsresult rv;                                                          \
        rv = cb->Callback(EmptyCString(), _path,                              \
                          nsIMemoryReporter::_kind,                           \
                          nsIMemoryReporter::_units,                          \
                          _amount,                                            \
                          NS_LITERAL_CSTRING(_desc),                          \
                          closure);                                           \
        NS_ENSURE_SUCCESS(rv, rv);                                            \
    } while (0)

#define REPORT_BYTES(_path, _kind, _amount, _desc)                            \
    REPORT(_path, _kind, UNITS_BYTES, _amount, _desc);

// REPORT2 and REPORT_BYTES2 are just like REPORT and REPORT_BYTES, except the
// description is an nsCString, instead of a literal string.

#define REPORT2(_path, _kind, _units, _amount, _desc)                         \
    do {                                                                      \
        nsresult rv;                                                          \
        rv = cb->Callback(EmptyCString(), _path,                              \
                          nsIMemoryReporter::_kind,                           \
                          nsIMemoryReporter::_units,                          \
                          _amount,                                            \
                          _desc,                                              \
                          closure);                                           \
        NS_ENSURE_SUCCESS(rv, rv);                                            \
    } while (0)

#define REPORT_BYTES2(_path, _kind, _amount, _desc)                           \
    REPORT2(_path, _kind, UNITS_BYTES, _amount, _desc);

#define REPORT_GC_BYTES(_path, _amount, _desc)                                \
    do {                                                                      \
        size_t amount = _amount;  /* evaluate _amount only once */            \
        nsresult rv;                                                          \
        rv = cb->Callback(EmptyCString(), _path,                              \
                          nsIMemoryReporter::KIND_NONHEAP,                    \
                          nsIMemoryReporter::UNITS_BYTES, amount,             \
                          NS_LITERAL_CSTRING(_desc), closure);                \
        NS_ENSURE_SUCCESS(rv, rv);                                            \
        gcTotal += amount;                                                    \
    } while (0)

// Report compartment/zone bytes.  Note that _descLiteral must be a literal
// string.
//
// Nb: all non-GC compartment reports are currently KIND_HEAP, and this macro
// relies on that.
#define ZCREPORT_BYTES(_path, _amount, _descLiteral)                          \
    do {                                                                      \
        /* Assign _descLiteral plus "" into a char* to prove that it's */     \
        /* actually a literal. */                                             \
        const char* unusedDesc = _descLiteral "";                             \
        (void) unusedDesc;                                                    \
        ZCREPORT_BYTES2(_path, _amount, NS_LITERAL_CSTRING(_descLiteral));    \
    } while (0)

// ZCREPORT_BYTES2 is identical to ZCREPORT_BYTES, except the description is a
// nsCString instead of a literal string.
#define ZCREPORT_BYTES2(_path, _amount, _desc)                                \
    do {                                                                      \
        size_t amount = _amount;  /* evaluate _amount only once */            \
        if (amount >= SUNDRIES_THRESHOLD) {                                   \
            nsresult rv;                                                      \
            rv = cb->Callback(EmptyCString(), _path,                          \
                              nsIMemoryReporter::KIND_HEAP,                   \
                              nsIMemoryReporter::UNITS_BYTES, amount,         \
                              _desc, closure);                                \
            NS_ENSURE_SUCCESS(rv, rv);                                        \
        } else {                                                              \
            otherSundries += amount;                                          \
        }                                                                     \
    } while (0)

#define ZCREPORT_GC_BYTES(_path, _amount, _desc)                              \
    do {                                                                      \
        size_t amount = _amount;  /* evaluate _amount only once */            \
        if (amount >= SUNDRIES_THRESHOLD) {                                   \
            nsresult rv;                                                      \
            rv = cb->Callback(EmptyCString(), _path,                          \
                              nsIMemoryReporter::KIND_NONHEAP,                \
                              nsIMemoryReporter::UNITS_BYTES, amount,         \
                              NS_LITERAL_CSTRING(_desc), closure);            \
            NS_ENSURE_SUCCESS(rv, rv);                                        \
            gcTotal += amount;                                                \
        } else {                                                              \
            gcHeapSundries += amount;                                         \
        }                                                                     \
    } while (0)

#define RREPORT_BYTES(_path, _kind, _amount, _desc)                           \
    do {                                                                      \
        size_t amount = _amount;  /* evaluate _amount only once */            \
        nsresult rv;                                                          \
        rv = cb->Callback(EmptyCString(), _path,                              \
                          nsIMemoryReporter::_kind,                           \
                          nsIMemoryReporter::UNITS_BYTES, amount,             \
                          NS_LITERAL_CSTRING(_desc), closure);                \
        NS_ENSURE_SUCCESS(rv, rv);                                            \
        rtTotal += amount;                                                    \
    } while (0)

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(JsMallocSizeOf)

namespace xpc {

static nsresult
ReportZoneStats(const JS::ZoneStats &zStats,
                const xpc::ZoneStatsExtras &extras,
                nsIMemoryMultiReporterCallback *cb,
                nsISupports *closure, size_t *gcTotalOut = NULL)
{
    const nsAutoCString& pathPrefix = extras.pathPrefix;
    size_t gcTotal = 0, gcHeapSundries = 0, otherSundries = 0;

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("gc-heap-arena-admin"),
                      zStats.gcHeapArenaAdmin,
                      "Memory on the garbage-collected JavaScript "
                      "heap, within arenas, that is used (a) to hold internal "
                      "bookkeeping information, and (b) to provide padding to "
                      "align GC things.");

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("unused-gc-things"),
                      zStats.gcHeapUnusedGcThings,
                      "Memory on the garbage-collected JavaScript "
                      "heap taken by empty GC thing slots within non-empty "
                      "arenas.");

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("lazy-scripts/gc-heap"),
                      zStats.gcHeapLazyScripts,
                      "Memory on the garbage-collected JavaScript "
                      "heap that represents scripts which haven't executed yet.");

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("type-objects/gc-heap"),
                      zStats.gcHeapTypeObjects,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds type inference information.");

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("ion-codes/gc-heap"),
                      zStats.gcHeapIonCodes,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds references to executable code pools "
                      "used by the IonMonkey JIT.");

    ZCREPORT_BYTES(pathPrefix + NS_LITERAL_CSTRING("lazy-scripts/malloc-heap"),
                   zStats.lazyScripts,
                   "Memory holding miscellaneous additional information associated with lazy "
                   "scripts.  This memory is allocated on the malloc heap.");

    ZCREPORT_BYTES(pathPrefix + NS_LITERAL_CSTRING("type-objects"),
                   zStats.typeObjects,
                   "Memory holding miscellaneous additional information associated with type "
                   "objects.");

    ZCREPORT_BYTES(pathPrefix + NS_LITERAL_CSTRING("type-pool"),
                   zStats.typePool,
                   "Memory holding contents of type sets and related data.");

    size_t gcHeapStringsAboutMemory = 0;
    size_t stringCharsAboutMemory = 0;

    for (size_t i = 0; i < zStats.notableStrings.length(); i++) {
        const JS::NotableStringInfo& info = zStats.notableStrings[i];

        nsDependentCString notableString(info.buffer);

        // Viewing about:memory generates many notable strings which contain
        // "string(length=".  If we report these as notable, then we'll create
        // even more notable strings the next time we open about:memory (unless
        // there's a GC in the meantime), and so on ad infinitum.
        //
        // To avoid cluttering up about:memory like this, we stick notable
        // strings which contain "strings/notable/string(length=" into their own
        // bucket.
#       define STRING_LENGTH "string(length="
        if (FindInReadable(NS_LITERAL_CSTRING(STRING_LENGTH), notableString)) {
            gcHeapStringsAboutMemory += info.totalGCThingSizeOf();
            stringCharsAboutMemory += info.sizeOfAllStringChars;
            continue;
        }

        // Escape / to \ before we put notableString into the memory reporter
        // path, because we don't want any forward slashes in the string to
        // count as path separators.
        nsCString escapedString(notableString);
        escapedString.ReplaceSubstring("/", "\\");

        bool truncated = notableString.Length() < info.length;

        nsCString path = pathPrefix +
            nsPrintfCString("strings/notable/" STRING_LENGTH "%d, copies=%d, \"%s\"%s)/",
                            info.length, info.numCopies, escapedString.get(),
                            truncated ? " (truncated)" : "");

        REPORT_BYTES2(path + NS_LITERAL_CSTRING("gc-heap"),
            KIND_NONHEAP,
            info.totalGCThingSizeOf(),
            nsPrintfCString("Memory allocated to hold headers for copies of "
            "the given notable string.  A string is notable if all of its copies "
            "together use more than %d bytes total of JS GC heap and malloc heap "
            "memory.\n\n"
            "These headers may contain the string data itself, if the string "
            "is short enough.  If so, the string won't have any memory reported "
            "under 'string-chars'.",
            JS::NotableStringInfo::notableSize()));
        gcTotal += info.totalGCThingSizeOf();

        if (info.sizeOfAllStringChars > 0) {
            REPORT_BYTES2(path + NS_LITERAL_CSTRING("malloc-heap"),
                KIND_HEAP,
                info.sizeOfAllStringChars,
                nsPrintfCString("Memory allocated on the malloc heap to hold "
                "string data for copies of the given notable string.  A string is "
                "notable if all of its copies together use more than %d bytes "
                "total of JS GC heap and malloc heap memory.",
                JS::NotableStringInfo::notableSize()));
        }
    }

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("strings/short/gc-heap"),
                      zStats.gcHeapStringsShort,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds headers for strings which are short "
                      "enough to be stored completely within the header.  That "
                      "is, a 'short' string uses no string-chars.");

    ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("strings/normal/gc-heap"),
                      zStats.gcHeapStringsNormal,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds string headers for strings which are too "
                      "long to fit entirely within the header.  The character "
                      "data for such strings is counted under "
                      "strings/normal/malloc-heap.");

    ZCREPORT_BYTES(pathPrefix + NS_LITERAL_CSTRING("strings/normal/malloc-heap"),
                   zStats.stringCharsNonNotable,
                   "Memory allocated to hold characters for strings which are too long "
                   "to fit entirely within their string headers.\n\n"
                   "Sometimes more memory is allocated than necessary, to "
                   "simplify string concatenation.");

    if (gcHeapStringsAboutMemory > 0) {
        ZCREPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("strings/notable/about-memory/gc-heap"),
                          gcHeapStringsAboutMemory,
                          "Memory allocated on the garbage-collected JavaScript "
                          "heap that holds headers for notable strings which "
                          "contain the string '" STRING_LENGTH "'.  These "
                          "strings are likely from about:memory itself.  We "
                          "filter them out rather than display them, because "
                          "displaying them would create even more strings every "
                          "time you refresh about:memory.");
    }

    if (stringCharsAboutMemory > 0) {
        ZCREPORT_BYTES(pathPrefix +
                       NS_LITERAL_CSTRING("strings/notable/about-memory/malloc-heap"),
                       stringCharsAboutMemory,
                       "Memory allocated to hold characters of notable strings "
                       "which contain the string '" STRING_LENGTH "'.  These "
                       "strings are likely from about:memory itself.  We filter "
                       "them out rather than display them, because displaying "
                       "them would create even more strings every time you "
                       "refresh about:memory.");
    }

    if (gcHeapSundries > 0) {
        // We deliberately don't use ZCREPORT_GC_BYTES here.
        REPORT_GC_BYTES(pathPrefix + NS_LITERAL_CSTRING("sundries/gc-heap"),
                        gcHeapSundries,
                        "The sum of all the 'gc-heap' measurements that are too "
                        "small to be worth showing individually.");
    }

    if (otherSundries > 0) {
        // We deliberately don't use ZCREPORT_BYTES here.
        REPORT_BYTES(pathPrefix + NS_LITERAL_CSTRING("sundries/malloc-heap"),
                     KIND_HEAP, otherSundries,
                     "The sum of all the 'malloc-heap' measurements that are too "
                     "small to be worth showing individually.");
    }

    if (gcTotalOut)
        *gcTotalOut += gcTotal;

    return NS_OK;

#   undef STRING_LENGTH
}

static nsresult
ReportCompartmentStats(const JS::CompartmentStats &cStats,
                       const xpc::CompartmentStatsExtras &extras,
                       amIAddonManager *addonManager,
                       nsIMemoryMultiReporterCallback *cb,
                       nsISupports *closure, size_t *gcTotalOut = NULL)
{
    static const nsDependentCString addonPrefix("explicit/add-ons/");

    size_t gcTotal = 0, gcHeapSundries = 0, otherSundries = 0;
    nsAutoCString cJSPathPrefix = extras.jsPathPrefix;
    nsAutoCString cDOMPathPrefix = extras.domPathPrefix;

    // Only attempt to prefix if we got a location and the path wasn't already
    // prefixed.
    if (extras.location && addonManager &&
        cJSPathPrefix.Find(addonPrefix, false, 0, 0) != 0) {
        nsAutoCString addonId;
        bool ok;
        if (NS_SUCCEEDED(addonManager->MapURIToAddonID(extras.location,
                                                        addonId, &ok))
            && ok) {
            // Insert the add-on id as "add-ons/@id@/" after "explicit/" to
            // aggregate add-on compartments.
            static const size_t explicitLength = strlen("explicit/");
            addonId.Insert(NS_LITERAL_CSTRING("add-ons/"), 0);
            addonId += "/";
            cJSPathPrefix.Insert(addonId, explicitLength);
            cDOMPathPrefix.Insert(addonId, explicitLength);
        }
    }

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/gc-heap/ordinary"),
                      cStats.gcHeapObjectsOrdinary,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds ordinary (i.e. not otherwise distinguished "
                      "my memory reporters) objects.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/gc-heap/function"),
                      cStats.gcHeapObjectsFunction,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds function objects.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/gc-heap/dense-array"),
                      cStats.gcHeapObjectsDenseArray,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds dense array objects.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/gc-heap/slow-array"),
                      cStats.gcHeapObjectsSlowArray,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds slow array objects.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/gc-heap/cross-compartment-wrapper"),
                      cStats.gcHeapObjectsCrossCompartmentWrapper,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds cross-compartment wrapper objects.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("scripts/gc-heap"),
                      cStats.gcHeapScripts,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds JSScript instances. A JSScript is "
                      "created for each user-defined function in a script. One "
                      "is also created for the top-level code in a script.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/gc-heap/tree/global-parented"),
                      cStats.gcHeapShapesTreeGlobalParented,
                      "Memory on the garbage-collected JavaScript heap that "
                      "holds shapes that (a) are in a property tree, and (b) "
                      "represent an object whose parent is the global object.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/gc-heap/tree/non-global-parented"),
                      cStats.gcHeapShapesTreeNonGlobalParented,
                      "Memory on the garbage-collected JavaScript heap that "
                      "holds shapes that (a) are in a property tree, and (b) "
                      "represent an object whose parent is not the global object.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/gc-heap/dict"),
                      cStats.gcHeapShapesDict,
                      "Memory on the garbage-collected JavaScript "
                      "heap that holds shapes that are in dictionary mode.");

    ZCREPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/gc-heap/base"),
                      cStats.gcHeapShapesBase,
                      "Memory on the garbage-collected JavaScript "
                      "heap that collates data common to many shapes.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/slots"),
                   cStats.objectsExtra.slots,
                   "Memory allocated on the malloc heap for the non-fixed object "
                   "slot arrays, which are used to represent object properties. "
                   "Some objects also contain a fixed number of slots which are "
                   "stored on the JavaScript heap; those slots "
                   "are not counted here, but in 'objects/gc-heap/*' instead.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/elements/non-asm.js"),
                   cStats.objectsExtra.elementsNonAsmJS,
                   "Memory allocated on the malloc heap for non-asm.js object element arrays, "
                   "which are used to represent indexed object properties.");

    // asm.js arrays are heap-allocated on some platforms and
    // non-heap-allocated on others.  We never put them under sundries,
    // because (a) in practice they're almost always larger than the sundries
    // threshold, and (b) we'd need a third category of non-heap, non-GC
    // sundries, which would be a pain.
    size_t asmJSHeap    = cStats.objectsExtra.elementsAsmJSHeap;
    size_t asmJSNonHeap = cStats.objectsExtra.elementsAsmJSNonHeap;
    MOZ_ASSERT(asmJSHeap == 0 || asmJSNonHeap == 0);
    if (asmJSHeap > 0) {
        REPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/elements/asm.js"),
                     KIND_HEAP, asmJSHeap,
                     "Memory allocated on the malloc heap for object element arrays used as asm.js "
                     "array buffers.");
    }
    if (asmJSNonHeap > 0) {
        REPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/non-heap/elements/asm.js"),
                     KIND_NONHEAP, asmJSNonHeap,
                     "Memory allocated for object element arrays used as asm.js array buffers. "
                     "This memory lives outside both the malloc heap and the JS heap.");
    }

    REPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/non-heap/code/asm.js"),
                 KIND_NONHEAP, cStats.objectsExtra.asmJSModuleCode,
                 "Memory allocated for AOT-compiled asm.js code.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/asm.js-module-data"),
                   cStats.objectsExtra.asmJSModuleData,
                   "Memory allocated for asm.js module data.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/arguments-data"),
                   cStats.objectsExtra.argumentsData,
                   "Memory allocated on the malloc heap for data belonging to arguments objects.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/regexp-statics"),
                   cStats.objectsExtra.regExpStatics,
                   "Memory allocated for data belonging to the RegExpStatics object.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/property-iterator-data"),
                   cStats.objectsExtra.propertyIteratorData,
                   "Memory allocated on the malloc heap for data belonging to property iterator objects.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("objects/malloc-heap/ctypes-data"),
                   cStats.objectsExtra.ctypesData,
                   "Memory allocated on the malloc heap for data belonging to ctypes objects.");

    // Note that we use cDOMPathPrefix here.  This is because we measure orphan
    // DOM nodes in the JS multi-reporter, but we want to report them in a
    // "dom" sub-tree rather than a "js" sub-tree.
    ZCREPORT_BYTES(cDOMPathPrefix + NS_LITERAL_CSTRING("orphan-nodes"),
                   cStats.objectsExtra.private_,
                   "Memory used by orphan DOM nodes that are only reachable "
                   "from JavaScript objects.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/malloc-heap/tree-tables"),
                   cStats.shapesExtraTreeTables,
                   "Memory allocated on the malloc heap for the property tables "
                   "that belong to shapes that are in a property tree.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/malloc-heap/dict-tables"),
                   cStats.shapesExtraDictTables,
                   "Memory allocated on the malloc heap for the property tables "
                   "that belong to shapes that are in dictionary mode.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/malloc-heap/tree-shape-kids"),
                   cStats.shapesExtraTreeShapeKids,
                   "Memory allocated on the malloc heap for the kid hashes that "
                   "belong to shapes that are in a property tree.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("shapes/malloc-heap/compartment-tables"),
                   cStats.shapesCompartmentTables,
                   "Memory on the malloc heap used by compartment-wide tables storing shape "
                   "information for use during object construction.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("scripts/malloc-heap/data"),
                   cStats.scriptData,
                   "Memory on the malloc heap allocated for various variable-length tables in "
                   "JSScript.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("baseline/data"),
                   cStats.baselineData,
                   "Memory used by the Baseline JIT for compilation data: "
                   "BaselineScripts.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("baseline/stubs/fallback"),
                   cStats.baselineStubsFallback,
                   "Memory used by the Baseline JIT for fallback IC stubs "
                   "(excluding code).");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("baseline/stubs/optimized"),
                   cStats.baselineStubsOptimized,
                   "Memory used by the Baseline JIT for optimized IC stubs "
                   "(excluding code).");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("ion-data"),
                   cStats.ionData,
                   "Memory used by the IonMonkey JIT for compilation data: "
                   "IonScripts.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("compartment-object"),
                   cStats.compartmentObject,
                   "Memory used for the JSCompartment object itself.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("cross-compartment-wrapper-table"),
                   cStats.crossCompartmentWrappersTable,
                   "Memory used by the cross-compartment wrapper table.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("regexp-compartment"),
                   cStats.regexpCompartment,
                   "Memory used by the regexp compartment.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("debuggees-set"),
                   cStats.debuggeesSet,
                   "Memory used by the debuggees set.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/type-scripts"),
                   cStats.typeInference.typeScripts,
                   "Memory used by type sets associated with scripts.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/type-results"),
                   cStats.typeInference.typeResults,
                   "Memory used by dynamic type results produced by scripts.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/analysis-pool"),
                   cStats.typeInference.analysisPool,
                   "Memory holding transient analysis information used during type inference and "
                   "compilation.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/pending-arrays"),
                   cStats.typeInference.pendingArrays,
                   "Memory used for solving constraints during type inference.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/allocation-site-tables"),
                   cStats.typeInference.allocationSiteTables,
                   "Memory indexing type objects associated with allocation sites.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/array-type-tables"),
                   cStats.typeInference.arrayTypeTables,
                   "Memory indexing type objects associated with array literals.");

    ZCREPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("type-inference/object-type-tables"),
                   cStats.typeInference.objectTypeTables,
                   "Memory indexing type objects associated with object literals.");

    if (gcHeapSundries > 0) {
        // We deliberately don't use ZCREPORT_GC_BYTES here.
        REPORT_GC_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("sundries/gc-heap"),
                        gcHeapSundries,
                        "The sum of all the 'gc-heap' measurements that are too "
                        "small to be worth showing individually.");
    }

    if (otherSundries > 0) {
        // We deliberately don't use ZCREPORT_BYTES here.
        REPORT_BYTES(cJSPathPrefix + NS_LITERAL_CSTRING("sundries/malloc-heap"),
                     KIND_HEAP, otherSundries,
                     "The sum of all the 'malloc-heap' measurements that are too "
                     "small to be worth showing individually.");
    }

    if (gcTotalOut)
        *gcTotalOut += gcTotal;

    return NS_OK;
}

static nsresult
ReportJSRuntimeExplicitTreeStats(const JS::RuntimeStats &rtStats,
                                 const nsACString &rtPath,
                                 amIAddonManager* addonManager,
                                 nsIMemoryMultiReporterCallback *cb,
                                 nsISupports *closure, size_t *rtTotalOut)
{
    nsresult rv;

    size_t gcTotal = 0;

    for (size_t i = 0; i < rtStats.zoneStatsVector.length(); i++) {
        const JS::ZoneStats &zStats = rtStats.zoneStatsVector[i];
        const xpc::ZoneStatsExtras *extras =
          static_cast<const xpc::ZoneStatsExtras*>(zStats.extra);
        rv = ReportZoneStats(zStats, *extras, cb, closure, &gcTotal);
        NS_ENSURE_SUCCESS(rv, rv);
    }

    for (size_t i = 0; i < rtStats.compartmentStatsVector.length(); i++) {
        JS::CompartmentStats cStats = rtStats.compartmentStatsVector[i];
        const xpc::CompartmentStatsExtras *extras =
            static_cast<const xpc::CompartmentStatsExtras*>(cStats.extra);
        rv = ReportCompartmentStats(cStats, *extras, addonManager, cb, closure,
                                    &gcTotal);
        NS_ENSURE_SUCCESS(rv, rv);
    }

    // Report the rtStats.runtime numbers under "runtime/", and compute their
    // total for later.

    size_t rtTotal = 0;

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/runtime-object"),
                  KIND_HEAP, rtStats.runtime.object,
                  "Memory used by the JSRuntime object.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/atoms-table"),
                  KIND_HEAP, rtStats.runtime.atomsTable,
                  "Memory used by the atoms table.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/contexts"),
                  KIND_HEAP, rtStats.runtime.contexts,
                  "Memory used by JSContext objects and certain structures "
                  "hanging off them.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/dtoa"),
                  KIND_HEAP, rtStats.runtime.dtoa,
                  "Memory used by DtoaState, which is used for converting "
                  "strings to numbers and vice versa.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/temporary"),
                  KIND_HEAP, rtStats.runtime.temporary,
                  "Memory held transiently in JSRuntime and used during "
                  "compilation.  It mostly holds parse nodes.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/code/ion"),
                  KIND_NONHEAP, rtStats.runtime.code.ion,
                  "Memory used by the IonMonkey JIT to hold generated code.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/code/baseline"),
                  KIND_NONHEAP, rtStats.runtime.code.baseline,
                  "Memory used by the Baseline JIT to hold generated code.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/code/regexp"),
                  KIND_NONHEAP, rtStats.runtime.code.regexp,
                  "Memory used by the regexp JIT to hold generated code.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/code/other"),
                  KIND_NONHEAP, rtStats.runtime.code.other,
                  "Memory used by the JITs to hold generated code for "
                  "wrappers and trampolines.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/code/unused"),
                  KIND_NONHEAP, rtStats.runtime.code.unused,
                  "Memory allocated by one of the JITs to hold code, "
                  "but which is currently unused.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/regexp-data"),
                  KIND_NONHEAP, rtStats.runtime.regexpData,
                  "Memory used by the regexp JIT to hold data.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/interpreter-stack"),
                  KIND_HEAP, rtStats.runtime.interpreterStack,
                  "Memory used for JS interpreter frames.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/gc-marker"),
                  KIND_HEAP, rtStats.runtime.gcMarker,
                  "Memory used for the GC mark stack and gray roots.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/math-cache"),
                  KIND_HEAP, rtStats.runtime.mathCache,
                  "Memory used for the math cache.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/script-data"),
                  KIND_HEAP, rtStats.runtime.scriptData,
                  "Memory used for the table holding script data shared in "
                  "the runtime.");

    RREPORT_BYTES(rtPath + NS_LITERAL_CSTRING("runtime/script-sources"),
                  KIND_HEAP, rtStats.runtime.scriptSources,
                  "Memory use for storing JavaScript source code and filenames.");

    if (rtTotalOut)
        *rtTotalOut = rtTotal;

    // Report GC numbers that don't belong to a compartment.

    // We don't want to report decommitted memory in "explicit", so we just
    // change the leading "explicit/" to "decommitted/".
    nsCString rtPath2(rtPath);
    rtPath2.Replace(0, strlen("explicit"), NS_LITERAL_CSTRING("decommitted"));
    REPORT_GC_BYTES(rtPath2 + NS_LITERAL_CSTRING("gc-heap/decommitted-arenas"),
                    rtStats.gcHeapDecommittedArenas,
                    "Memory on the garbage-collected JavaScript heap, in "
                    "arenas in non-empty chunks, that is returned to the OS. "
                    "This means it takes up address space but no physical "
                    "memory or swap space.");

    REPORT_GC_BYTES(rtPath + NS_LITERAL_CSTRING("gc-heap/unused-chunks"),
                    rtStats.gcHeapUnusedChunks,
                    "Memory on the garbage-collected JavaScript heap taken by "
                    "empty chunks, which will soon be released unless claimed "
                    "for new allocations.");

    REPORT_GC_BYTES(rtPath + NS_LITERAL_CSTRING("gc-heap/unused-arenas"),
                    rtStats.gcHeapUnusedArenas,
                    "Memory on the garbage-collected JavaScript heap taken by "
                    "empty arenas within non-empty chunks.");

    REPORT_GC_BYTES(rtPath + NS_LITERAL_CSTRING("gc-heap/chunk-admin"),
                    rtStats.gcHeapChunkAdmin,
                    "Memory on the garbage-collected JavaScript heap, within "
                    "chunks, that is used to hold internal bookkeeping "
                    "information.");

    // gcTotal is the sum of everything we've reported for the GC heap.  It
    // should equal rtStats.gcHeapChunkTotal.
    MOZ_ASSERT(gcTotal == rtStats.gcHeapChunkTotal);

    return NS_OK;
}

nsresult
ReportJSRuntimeExplicitTreeStats(const JS::RuntimeStats &rtStats,
                                 const nsACString &rtPath,
                                 nsIMemoryMultiReporterCallback *cb,
                                 nsISupports *closure, size_t *rtTotalOut)
{
    nsCOMPtr<amIAddonManager> am =
      do_GetService("@mozilla.org/addons/integration;1");
    return ReportJSRuntimeExplicitTreeStats(rtStats, rtPath, am.get(), cb,
                                            closure, rtTotalOut);
}


} // namespace xpc

class JSCompartmentsMultiReporter MOZ_FINAL : public nsIMemoryMultiReporter
{
  public:
    NS_DECL_THREADSAFE_ISUPPORTS

    NS_IMETHOD GetName(nsACString &name) {
        name.AssignLiteral("compartments");
        return NS_OK;
    }

    typedef js::Vector<nsCString, 0, js::SystemAllocPolicy> Paths;

    static void CompartmentCallback(JSRuntime *rt, void* data, JSCompartment *c) {
        // silently ignore OOM errors
        Paths *paths = static_cast<Paths *>(data);
        nsCString path;
        GetCompartmentName(c, path, true);
        path.Insert(js::IsSystemCompartment(c)
                    ? NS_LITERAL_CSTRING("compartments/system/")
                    : NS_LITERAL_CSTRING("compartments/user/"),
                    0);
        paths->append(path);
    }

    NS_IMETHOD CollectReports(nsIMemoryMultiReporterCallback *cb,
                              nsISupports *closure)
    {
        // First we collect the compartment paths.  Then we report them.  Doing
        // the two steps interleaved is a bad idea, because calling |cb|
        // from within CompartmentCallback() leads to all manner of assertions.

        // Collect.

        Paths paths;
        JS_IterateCompartments(nsXPConnect::GetRuntimeInstance()->Runtime(),
                               &paths, CompartmentCallback);

        // Report.
        for (size_t i = 0; i < paths.length(); i++)
            // These ones don't need a description, hence the "".
            REPORT(nsCString(paths[i]), KIND_OTHER, UNITS_COUNT, 1, "");

        return NS_OK;
    }
};

NS_IMPL_ISUPPORTS1(JSCompartmentsMultiReporter
                              , nsIMemoryMultiReporter
                              )

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(OrphanMallocSizeOf)

namespace xpc {

static size_t
SizeOfTreeIncludingThis(nsINode *tree)
{
    size_t n = tree->SizeOfIncludingThis(OrphanMallocSizeOf);
    for (nsIContent* child = tree->GetFirstChild(); child; child = child->GetNextNode(tree))
        n += child->SizeOfIncludingThis(OrphanMallocSizeOf);

    return n;
}

class OrphanReporter : public JS::ObjectPrivateVisitor
{
  public:
    OrphanReporter(GetISupportsFun aGetISupports)
      : JS::ObjectPrivateVisitor(aGetISupports)
    {
        mAlreadyMeasuredOrphanTrees.Init();
    }

    virtual size_t sizeOfIncludingThis(nsISupports *aSupports) {
        size_t n = 0;
        nsCOMPtr<nsINode> node = do_QueryInterface(aSupports);
        // https://bugzilla.mozilla.org/show_bug.cgi?id=773533#c11 explains
        // that we have to skip XBL elements because they violate certain
        // assumptions.  Yuk.
        if (node && !node->IsInDoc() &&
            !(node->IsElement() && node->AsElement()->IsInNamespace(kNameSpaceID_XBL)))
        {
            // This is an orphan node.  If we haven't already handled the
            // sub-tree that this node belongs to, measure the sub-tree's size
            // and then record its root so we don't measure it again.
            nsCOMPtr<nsINode> orphanTree = node->SubtreeRoot();
            if (!mAlreadyMeasuredOrphanTrees.Contains(orphanTree)) {
                n += SizeOfTreeIncludingThis(orphanTree);
                mAlreadyMeasuredOrphanTrees.PutEntry(orphanTree);
            }
        }
        return n;
    }

  private:
    nsTHashtable <nsISupportsHashKey> mAlreadyMeasuredOrphanTrees;
};

class XPCJSRuntimeStats : public JS::RuntimeStats
{
    WindowPaths *mWindowPaths;
    WindowPaths *mTopWindowPaths;
    bool mGetLocations;

  public:
    XPCJSRuntimeStats(WindowPaths *windowPaths, WindowPaths *topWindowPaths,
                      bool getLocations)
      : JS::RuntimeStats(JsMallocSizeOf),
        mWindowPaths(windowPaths),
        mTopWindowPaths(topWindowPaths),
        mGetLocations(getLocations)
    {}

    ~XPCJSRuntimeStats() {
        for (size_t i = 0; i != compartmentStatsVector.length(); ++i)
            delete static_cast<xpc::CompartmentStatsExtras*>(compartmentStatsVector[i].extra);


        for (size_t i = 0; i != zoneStatsVector.length(); ++i)
            delete static_cast<xpc::ZoneStatsExtras*>(zoneStatsVector[i].extra);
    }

    virtual void initExtraZoneStats(JS::Zone *zone, JS::ZoneStats *zStats) MOZ_OVERRIDE {
        // Get the compartment's global.
        nsXPConnect *xpc = nsXPConnect::XPConnect();
        AutoSafeJSContext cx;
        JSCompartment *comp = js::GetAnyCompartmentInZone(zone);
        xpc::ZoneStatsExtras *extras = new xpc::ZoneStatsExtras;
        extras->pathPrefix.AssignLiteral("explicit/js-non-window/zones/");
        RootedObject global(cx, JS_GetGlobalForCompartmentOrNull(cx, comp));
        if (global) {
            // Need to enter the compartment, otherwise GetNativeOfWrapper()
            // might crash.
            JSAutoCompartment ac(cx, global);
            nsISupports *native = xpc->GetNativeOfWrapper(cx, global);
            if (nsCOMPtr<nsPIDOMWindow> piwindow = do_QueryInterface(native)) {
                // The global is a |window| object.  Use the path prefix that
                // we should have already created for it.
                if (mTopWindowPaths->Get(piwindow->WindowID(),
                                         &extras->pathPrefix))
                    extras->pathPrefix.AppendLiteral("/js-");
            }
        }

        extras->pathPrefix += nsPrintfCString("zone(0x%p)/", (void *)zone);

        zStats->extra = extras;
    }

    virtual void initExtraCompartmentStats(JSCompartment *c,
                                           JS::CompartmentStats *cstats) MOZ_OVERRIDE
    {
        xpc::CompartmentStatsExtras *extras = new xpc::CompartmentStatsExtras;
        nsCString cName;
        GetCompartmentName(c, cName, true);
        if (mGetLocations) {
            CompartmentPrivate *cp = GetCompartmentPrivate(c);
            if (cp)
              cp->GetLocationURI(getter_AddRefs(extras->location));
            // Note: cannot use amIAddonManager implementation at this point,
            // as it is a JS service and the JS heap is currently not idle.
            // Otherwise, we could have computed the add-on id at this point.
        }

        // Get the compartment's global.
        nsXPConnect *xpc = nsXPConnect::XPConnect();
        AutoSafeJSContext cx;
        bool needZone = true;
        RootedObject global(cx, JS_GetGlobalForCompartmentOrNull(cx, c));
        if (global) {
            // Need to enter the compartment, otherwise GetNativeOfWrapper()
            // might crash.
            JSAutoCompartment ac(cx, global);
            nsISupports *native = xpc->GetNativeOfWrapper(cx, global);
            if (nsCOMPtr<nsPIDOMWindow> piwindow = do_QueryInterface(native)) {
                // The global is a |window| object.  Use the path prefix that
                // we should have already created for it.
                if (mWindowPaths->Get(piwindow->WindowID(),
                                      &extras->jsPathPrefix)) {
                    extras->domPathPrefix.Assign(extras->jsPathPrefix);
                    extras->domPathPrefix.AppendLiteral("/dom/");
                    extras->jsPathPrefix.AppendLiteral("/js-");
                    needZone = false;
                } else {
                    extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
                    extras->domPathPrefix.AssignLiteral("explicit/dom/unknown-window-global?!/");
                }
            } else {
                extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
                extras->domPathPrefix.AssignLiteral("explicit/dom/non-window-global?!/");
            }
        } else {
            extras->jsPathPrefix.AssignLiteral("explicit/js-non-window/zones/");
            extras->domPathPrefix.AssignLiteral("explicit/dom/no-global?!/");
        }

        if (needZone)
            extras->jsPathPrefix += nsPrintfCString("zone(0x%p)/", (void *)js::GetCompartmentZone(c));

        extras->jsPathPrefix += NS_LITERAL_CSTRING("compartment(") + cName + NS_LITERAL_CSTRING(")/");

        // extras->jsPathPrefix is used for almost all the compartment-specific
        // reports. At this point it has the form
        // "<something>compartment(<cname>)/".
        //
        // extras->domPathPrefix is used for DOM orphan nodes, which are
        // counted by the JS multi-reporter but reported as part of the
        // DOM measurements. At this point it has the form "<something>/dom/"
        // if this compartment belongs to an nsGlobalWindow, and
        // "explicit/dom/<something>?!/" otherwise (in which case it shouldn't
        // be used, because non-nsGlobalWindow compartments shouldn't have
        // orphan DOM nodes).

        cstats->extra = extras;
    }
};

nsresult
JSMemoryMultiReporter::CollectReports(WindowPaths *windowPaths,
                                      WindowPaths *topWindowPaths,
                                      nsIMemoryMultiReporterCallback *cb,
                                      nsISupports *closure)
{
    XPCJSRuntime *xpcrt = nsXPConnect::GetRuntimeInstance();

    // In the first step we get all the stats and stash them in a local
    // data structure.  In the second step we pass all the stashed stats to
    // the callback.  Separating these steps is important because the
    // callback may be a JS function, and executing JS while getting these
    // stats seems like a bad idea.

    nsCOMPtr<amIAddonManager> addonManager =
      do_GetService("@mozilla.org/addons/integration;1");
    bool getLocations = !!addonManager;
    XPCJSRuntimeStats rtStats(windowPaths, topWindowPaths, getLocations);
    OrphanReporter orphanReporter(XPCConvert::GetISupportsFromJSObject);
    if (!JS::CollectRuntimeStats(xpcrt->Runtime(), &rtStats, &orphanReporter))
        return NS_ERROR_FAILURE;

    size_t xpconnect =
        xpcrt->SizeOfIncludingThis(JsMallocSizeOf) +
        XPCWrappedNativeScope::SizeOfAllScopesIncludingThis(JsMallocSizeOf);

    // This is the second step (see above).  First we report stuff in the
    // "explicit" tree, then we report other stuff.

    nsresult rv;
    size_t rtTotal = 0;
    rv = xpc::ReportJSRuntimeExplicitTreeStats(rtStats,
                                               NS_LITERAL_CSTRING("explicit/js-non-window/"),
                                               addonManager, cb, closure,
                                               &rtTotal);
    NS_ENSURE_SUCCESS(rv, rv);

    // Report the sums of the compartment numbers.
    xpc::CompartmentStatsExtras cExtrasTotal;
    cExtrasTotal.jsPathPrefix.AssignLiteral("js-main-runtime/compartments/");
    cExtrasTotal.domPathPrefix.AssignLiteral("window-objects/dom/");
    rv = ReportCompartmentStats(rtStats.cTotals, cExtrasTotal, addonManager,
                                cb, closure);
    NS_ENSURE_SUCCESS(rv, rv);

    xpc::ZoneStatsExtras zExtrasTotal;
    zExtrasTotal.pathPrefix.AssignLiteral("js-main-runtime/zones/");
    rv = ReportZoneStats(rtStats.zTotals, zExtrasTotal, cb, closure);
    NS_ENSURE_SUCCESS(rv, rv);

    // Report the sum of the runtime/ numbers.
    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime/runtime"),
                 KIND_OTHER, rtTotal,
                 "The sum of all measurements under 'explicit/js-non-window/runtime/'.");

    // Report the numbers for memory outside of compartments.

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime/gc-heap/unused-chunks"),
                 KIND_OTHER,
                 rtStats.gcHeapUnusedChunks,
                 "The same as 'explicit/js-non-window/gc-heap/unused-chunks'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime/gc-heap/unused-arenas"),
                 KIND_OTHER,
                 rtStats.gcHeapUnusedArenas,
                 "The same as 'explicit/js-non-window/gc-heap/unused-arenas'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime/gc-heap/chunk-admin"),
                 KIND_OTHER,
                 rtStats.gcHeapChunkAdmin,
                 "The same as 'explicit/js-non-window/gc-heap/chunk-admin'.");

    // Report a breakdown of the committed GC space.

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/unused/chunks"),
                 KIND_OTHER,
                 rtStats.gcHeapUnusedChunks,
                 "The same as 'explicit/js-non-window/gc-heap/unused-chunks'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/unused/arenas"),
                 KIND_OTHER,
                 rtStats.gcHeapUnusedArenas,
                 "The same as 'explicit/js-non-window/gc-heap/unused-arenas'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/unused/gc-things"),
                 KIND_OTHER,
                 rtStats.zTotals.gcHeapUnusedGcThings,
                 "The same as 'js-main-runtime/compartments/gc-heap/unused-gc-things'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/used/chunk-admin"),
                 KIND_OTHER,
                 rtStats.gcHeapChunkAdmin,
                 "The same as 'explicit/js-non-window/gc-heap/chunk-admin'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/used/arena-admin"),
                 KIND_OTHER,
                 rtStats.zTotals.gcHeapArenaAdmin,
                 "The same as 'js-main-runtime/compartments/gc-heap/arena-admin'.");

    REPORT_BYTES(NS_LITERAL_CSTRING("js-main-runtime-gc-heap-committed/used/gc-things"),
                 KIND_OTHER,
                 rtStats.gcHeapGcThings,
                 "Memory on the garbage-collected JavaScript heap that holds GC things such "
                 "as objects, strings, scripts, etc.")

    // Report xpconnect.

    REPORT_BYTES(NS_LITERAL_CSTRING("explicit/xpconnect"),
                 KIND_HEAP, xpconnect,
                 "Memory used by XPConnect.");

    return NS_OK;
}

} // namespace xpc

#ifdef MOZ_CRASHREPORTER
static bool
DiagnosticMemoryCallback(void *ptr, size_t size)
{
    return CrashReporter::RegisterAppMemory(ptr, size) == NS_OK;
}
#endif

static void
AccumulateTelemetryCallback(int id, uint32_t sample)
{
    switch (id) {
      case JS_TELEMETRY_GC_REASON:
        Telemetry::Accumulate(Telemetry::GC_REASON_2, sample);
        break;
      case JS_TELEMETRY_GC_IS_COMPARTMENTAL:
        Telemetry::Accumulate(Telemetry::GC_IS_COMPARTMENTAL, sample);
        break;
      case JS_TELEMETRY_GC_MS:
        Telemetry::Accumulate(Telemetry::GC_MS, sample);
        break;
      case JS_TELEMETRY_GC_MAX_PAUSE_MS:
        Telemetry::Accumulate(Telemetry::GC_MAX_PAUSE_MS, sample);
        break;
      case JS_TELEMETRY_GC_MARK_MS:
        Telemetry::Accumulate(Telemetry::GC_MARK_MS, sample);
        break;
      case JS_TELEMETRY_GC_SWEEP_MS:
        Telemetry::Accumulate(Telemetry::GC_SWEEP_MS, sample);
        break;
      case JS_TELEMETRY_GC_MARK_ROOTS_MS:
        Telemetry::Accumulate(Telemetry::GC_MARK_ROOTS_MS, sample);
        break;
      case JS_TELEMETRY_GC_MARK_GRAY_MS:
        Telemetry::Accumulate(Telemetry::GC_MARK_GRAY_MS, sample);
        break;
      case JS_TELEMETRY_GC_SLICE_MS:
        Telemetry::Accumulate(Telemetry::GC_SLICE_MS, sample);
        break;
      case JS_TELEMETRY_GC_MMU_50:
        Telemetry::Accumulate(Telemetry::GC_MMU_50, sample);
        break;
      case JS_TELEMETRY_GC_RESET:
        Telemetry::Accumulate(Telemetry::GC_RESET, sample);
        break;
      case JS_TELEMETRY_GC_INCREMENTAL_DISABLED:
        Telemetry::Accumulate(Telemetry::GC_INCREMENTAL_DISABLED, sample);
        break;
      case JS_TELEMETRY_GC_NON_INCREMENTAL:
        Telemetry::Accumulate(Telemetry::GC_NON_INCREMENTAL, sample);
        break;
      case JS_TELEMETRY_GC_SCC_SWEEP_TOTAL_MS:
        Telemetry::Accumulate(Telemetry::GC_SCC_SWEEP_TOTAL_MS, sample);
        break;
      case JS_TELEMETRY_GC_SCC_SWEEP_MAX_PAUSE_MS:
        Telemetry::Accumulate(Telemetry::GC_SCC_SWEEP_MAX_PAUSE_MS, sample);
        break;
    }
}

static void
CompartmentNameCallback(JSRuntime *rt, JSCompartment *comp,
                        char *buf, size_t bufsize)
{
    nsCString name;
    GetCompartmentName(comp, name, false);
    if (name.Length() >= bufsize)
        name.Truncate(bufsize - 1);
    memcpy(buf, name.get(), name.Length() + 1);
}

static bool
PreserveWrapper(JSContext *cx, JSObject *obj)
{
    MOZ_ASSERT(cx);
    MOZ_ASSERT(obj);
    MOZ_ASSERT(IS_WN_REFLECTOR(obj) || mozilla::dom::IsDOMObject(obj));

    return mozilla::dom::IsDOMObject(obj) && mozilla::dom::TryPreserveWrapper(obj);
}

static nsresult
ReadSourceFromFilename(JSContext *cx, const char *filename, jschar **src, uint32_t *len)
{
  nsresult rv;

  // mozJSSubScriptLoader prefixes the filenames of the scripts it loads with
  // the filename of its caller. Axe that if present.
  const char *arrow;
  while ((arrow = strstr(filename, " -> ")))
    filename = arrow + strlen(" -> ");

  // Get the URI.
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), filename);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> scriptChannel;
  rv = NS_NewChannel(getter_AddRefs(scriptChannel), uri);
  NS_ENSURE_SUCCESS(rv, rv);

  // Only allow local reading.
  nsCOMPtr<nsIURI> actualUri;
  rv = scriptChannel->GetURI(getter_AddRefs(actualUri));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString scheme;
  rv = actualUri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!scheme.EqualsLiteral("file") && !scheme.EqualsLiteral("jar"))
    return NS_OK;

  nsCOMPtr<nsIInputStream> scriptStream;
  rv = scriptChannel->Open(getter_AddRefs(scriptStream));
  NS_ENSURE_SUCCESS(rv, rv);

  uint64_t rawLen;
  rv = scriptStream->Available(&rawLen);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!rawLen)
    return NS_ERROR_FAILURE;
  if (rawLen > UINT32_MAX)
    return NS_ERROR_FILE_TOO_BIG;

  // Allocate an internal buf the size of the file.
  nsAutoArrayPtr<unsigned char> buf(new unsigned char[rawLen]);
  if (!buf)
    return NS_ERROR_OUT_OF_MEMORY;

  unsigned char *ptr = buf, *end = ptr + rawLen;
  while (ptr < end) {
    uint32_t bytesRead;
    rv = scriptStream->Read(reinterpret_cast<char *>(ptr), end - ptr, &bytesRead);
    if (NS_FAILED(rv))
      return rv;
    MOZ_ASSERT(bytesRead > 0, "stream promised more bytes before EOF");
    ptr += bytesRead;
  }

  nsString decoded;
  rv = nsScriptLoader::ConvertToUTF16(scriptChannel, buf, rawLen, EmptyString(), NULL, decoded);
  NS_ENSURE_SUCCESS(rv, rv);

  // Copy to JS engine.
  *len = decoded.Length();
  *src = static_cast<jschar *>(JS_malloc(cx, decoded.Length()*sizeof(jschar)));
  if (!*src)
    return NS_ERROR_FAILURE;
  memcpy(*src, decoded.get(), decoded.Length()*sizeof(jschar));

  return NS_OK;
}

/*
  The JS engine calls this function when it needs the source for a chrome JS
  function. See the comment in the XPCJSRuntime constructor.
*/
static bool
SourceHook(JSContext *cx, JS::Handle<JSScript*> script, jschar **src,
           uint32_t *length)
{
  *src = NULL;
  *length = 0;

  if (!nsContentUtils::IsCallerChrome())
    return true;

  const char *filename = JS_GetScriptFilename(cx, script);
  if (!filename)
    return true;

  nsresult rv = ReadSourceFromFilename(cx, filename, src, length);
  if (NS_FAILED(rv)) {
    xpc::Throw(cx, rv);
    return false;
  }

  return true;
}

XPCJSRuntime::XPCJSRuntime(nsXPConnect* aXPConnect)
   : CycleCollectedJSRuntime(32L * 1024L * 1024L, JS_USE_HELPER_THREADS, true),
   mJSContextStack(new XPCJSContextStack()),
   mCallContext(nullptr),
   mAutoRoots(nullptr),
   mResolveName(JSID_VOID),
   mResolvingWrapper(nullptr),
   mWrappedJSMap(JSObject2WrappedJSMap::newMap(XPC_JS_MAP_SIZE)),
   mWrappedJSClassMap(IID2WrappedJSClassMap::newMap(XPC_JS_CLASS_MAP_SIZE)),
   mIID2NativeInterfaceMap(IID2NativeInterfaceMap::newMap(XPC_NATIVE_INTERFACE_MAP_SIZE)),
   mClassInfo2NativeSetMap(ClassInfo2NativeSetMap::newMap(XPC_NATIVE_SET_MAP_SIZE)),
   mNativeSetMap(NativeSetMap::newMap(XPC_NATIVE_SET_MAP_SIZE)),
   mThisTranslatorMap(IID2ThisTranslatorMap::newMap(XPC_THIS_TRANSLATOR_MAP_SIZE)),
   mNativeScriptableSharedMap(XPCNativeScriptableSharedMap::newMap(XPC_NATIVE_JSCLASS_MAP_SIZE)),
   mDyingWrappedNativeProtoMap(XPCWrappedNativeProtoMap::newMap(XPC_DYING_NATIVE_PROTO_MAP_SIZE)),
   mDetachedWrappedNativeProtoMap(XPCWrappedNativeProtoMap::newMap(XPC_DETACHED_NATIVE_PROTO_MAP_SIZE)),
   mMapLock(XPCAutoLock::NewLock("XPCJSRuntime::mMapLock")),
   mThreadRunningGC(nullptr),
   mWrappedJSToReleaseArray(),
   mNativesToReleaseArray(),
   mDoingFinalization(false),
   mVariantRoots(nullptr),
   mWrappedJSRoots(nullptr),
   mObjectHolderRoots(nullptr),
   mWatchdogManager(new WatchdogManager(this)),
   mJunkScope(nullptr),
   mAsyncSnowWhiteFreer(new AsyncFreeSnowWhite()),
   mExceptionManagerNotAvailable(false)
{
#ifdef XPC_CHECK_WRAPPERS_AT_SHUTDOWN
    DEBUG_WrappedNativeHashtable =
        PL_NewDHashTable(PL_DHashGetStubOps(), nullptr,
                         sizeof(PLDHashEntryStub), 128);
#endif

    DOM_InitInterfaces();

    // these jsids filled in later when we have a JSContext to work with.
    mStrIDs[0] = JSID_VOID;

    MOZ_ASSERT(Runtime());
    JSRuntime* runtime = Runtime();

    // Unconstrain the runtime's threshold on nominal heap size, to avoid
    // triggering GC too often if operating continuously near an arbitrary
    // finite threshold (0xffffffff is infinity for uint32_t parameters).
    // This leaves the maximum-JS_malloc-bytes threshold still in effect
    // to cause period, and we hope hygienic, last-ditch GCs from within
    // the GC's allocator.
    JS_SetGCParameter(runtime, JSGC_MAX_BYTES, 0xffffffff);
#if defined(MOZ_ASAN) || (defined(DEBUG) && !defined(XP_WIN))
    // Bug 803182: account for the 4x difference in the size of js::Interpret
    // between optimized and debug builds. Also, ASan requires more stack space
    // due to redzones
    JS_SetNativeStackQuota(runtime, 2 * 128 * sizeof(size_t) * 1024);
#elif defined(XP_WIN)
    // 1MB is the default stack size on Windows
    JS_SetNativeStackQuota(runtime, 900 * 1024);
#elif defined(XP_MACOSX) || defined(DARWIN)
    // 8MB is the default stack size on MacOS
    JS_SetNativeStackQuota(runtime, 7 * 1024 * 1024);
#else
    JS_SetNativeStackQuota(runtime, 128 * sizeof(size_t) * 1024);
#endif
    JS_SetDestroyCompartmentCallback(runtime, CompartmentDestroyedCallback);
    JS_SetCompartmentNameCallback(runtime, CompartmentNameCallback);
    mPrevGCSliceCallback = JS::SetGCSliceCallback(runtime, GCSliceCallback);
    JS_SetFinalizeCallback(runtime, FinalizeCallback);
    JS_SetWrapObjectCallbacks(runtime,
                              xpc::WrapperFactory::Rewrap,
                              xpc::WrapperFactory::WrapForSameCompartment,
                              xpc::WrapperFactory::PrepareForWrapping);
    js::SetPreserveWrapperCallback(runtime, PreserveWrapper);
#ifdef MOZ_CRASHREPORTER
    JS_EnumerateDiagnosticMemoryRegions(DiagnosticMemoryCallback);
#endif
#ifdef MOZ_ENABLE_PROFILER_SPS
    if (PseudoStack *stack = mozilla_get_pseudo_stack())
        stack->sampleRuntime(runtime);
#endif
    JS_SetAccumulateTelemetryCallback(runtime, AccumulateTelemetryCallback);
    js::SetActivityCallback(runtime, ActivityCallback, this);
    js::SetCTypesActivityCallback(runtime, CTypesActivityCallback);
    JS_SetOperationCallback(runtime, OperationCallback);

    // The JS engine needs to keep the source code around in order to implement
    // Function.prototype.toSource(). It'd be nice to not have to do this for
    // chrome code and simply stub out requests for source on it. Life is not so
    // easy, unfortunately. Nobody relies on chrome toSource() working in core
    // browser code, but chrome tests use it. The worst offenders are addons,
    // which like to monkeypatch chrome functions by calling toSource() on them
    // and using regular expressions to modify them. We avoid keeping most browser
    // JS source code in memory by setting LAZY_SOURCE on JS::CompileOptions when
    // compiling some chrome code. This causes the JS engine not save the source
    // code in memory. When the JS engine is asked to provide the source for a
    // function compiled with LAZY_SOURCE, it calls SourceHook to load it.
    ///
    // Note we do have to retain the source code in memory for scripts compiled in
    // compileAndGo mode and compiled function bodies (from
    // JS_CompileFunction*). In practice, this means content scripts and event
    // handlers.
    JS_SetSourceHook(runtime, SourceHook);

    // Set up locale information and callbacks for the newly-created runtime so
    // that the various toLocaleString() methods, localeCompare(), and other
    // internationalization APIs work as desired.
    if (!xpc_LocalizeRuntime(runtime))
        NS_RUNTIMEABORT("xpc_LocalizeRuntime failed.");

    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(XPConnectJSGCHeap));
    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(XPConnectJSSystemCompartmentCount));
    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(XPConnectJSUserCompartmentCount));
    NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(JSMainRuntimeTemporaryPeak));
    NS_RegisterMemoryMultiReporter(new JSCompartmentsMultiReporter);

    // Install a JavaScript 'debugger' keyword handler in debug builds only
#ifdef DEBUG
    if (!JS_GetGlobalDebugHooks(runtime)->debuggerHandler)
        xpc_InstallJSDebuggerKeywordHandler(runtime);
#endif
}

// static
XPCJSRuntime*
XPCJSRuntime::newXPCJSRuntime(nsXPConnect* aXPConnect)
{
    NS_PRECONDITION(aXPConnect,"bad param");

    XPCJSRuntime* self = new XPCJSRuntime(aXPConnect);

    if (self                                    &&
        self->Runtime()                    &&
        self->GetWrappedJSMap()                 &&
        self->GetWrappedJSClassMap()            &&
        self->GetIID2NativeInterfaceMap()       &&
        self->GetClassInfo2NativeSetMap()       &&
        self->GetNativeSetMap()                 &&
        self->GetThisTranslatorMap()            &&
        self->GetNativeScriptableSharedMap()    &&
        self->GetDyingWrappedNativeProtoMap()   &&
        self->GetMapLock()                      &&
        self->mWatchdogManager) {
        return self;
    }

    NS_RUNTIMEABORT("new XPCJSRuntime failed to initialize.");

    delete self;
    return nullptr;
}

// InternStaticDictionaryJSVals is automatically generated.
bool InternStaticDictionaryJSVals(JSContext* aCx);

bool
XPCJSRuntime::OnJSContextNew(JSContext *cx)
{
    // If we were the first cx ever created (like the SafeJSContext), the caller
    // would have had no way to enter a request. Enter one now before doing the
    // rest of the cx setup.
    JSAutoRequest ar(cx);

    // if it is our first context then we need to generate our string ids
    if (JSID_IS_VOID(mStrIDs[0])) {
        RootedString str(cx);
        for (unsigned i = 0; i < IDX_TOTAL_COUNT; i++) {
            str = JS_InternString(cx, mStrings[i]);
            if (!str) {
                mStrIDs[0] = JSID_VOID;
                return false;
            }
            mStrIDs[i] = INTERNED_STRING_TO_JSID(cx, str);
            mStrJSVals[i] = STRING_TO_JSVAL(str);
        }

        if (!mozilla::dom::DefineStaticJSVals(cx) ||
            !InternStaticDictionaryJSVals(cx)) {
            return false;
        }
    }

    XPCContext* xpc = new XPCContext(this, cx);
    if (!xpc)
        return false;

    return true;
}

bool
XPCJSRuntime::DescribeCustomObjects(JSObject* obj, js::Class* clasp,
                                    char (&name)[72]) const
{
    XPCNativeScriptableInfo *si = nullptr;

    if (!IS_PROTO_CLASS(clasp)) {
        return false;
    }

    XPCWrappedNativeProto *p =
        static_cast<XPCWrappedNativeProto*>(xpc_GetJSPrivate(obj));
    si = p->GetScriptableInfo();
    
    if (!si) {
        return false;
    }

    JS_snprintf(name, sizeof(name), "JS Object (%s - %s)",
                clasp->name, si->GetJSClass()->name);
    return true;
}

bool
XPCJSRuntime::NoteCustomGCThingXPCOMChildren(js::Class* clasp, JSObject* obj,
                                             nsCycleCollectionTraversalCallback& cb) const
{
    if (clasp != &XPC_WN_Tearoff_JSClass) {
        return false;
    }

    // A tearoff holds a strong reference to its native object
    // (see XPCWrappedNative::FlatJSObjectFinalized). Its XPCWrappedNative
    // will be held alive through the parent of the JSObject of the tearoff.
    XPCWrappedNativeTearOff *to =
        static_cast<XPCWrappedNativeTearOff*>(xpc_GetJSPrivate(obj));
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "xpc_GetJSPrivate(obj)->mNative");
    cb.NoteXPCOMChild(to->GetNative());
    return true;
}

/***************************************************************************/

#ifdef DEBUG
static PLDHashOperator
WrappedJSClassMapDumpEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                uint32_t number, void *arg)
{
    ((IID2WrappedJSClassMap::Entry*)hdr)->value->DebugDump(*(int16_t*)arg);
    return PL_DHASH_NEXT;
}
static PLDHashOperator
NativeSetDumpEnumerator(PLDHashTable *table, PLDHashEntryHdr *hdr,
                        uint32_t number, void *arg)
{
    ((NativeSetMap::Entry*)hdr)->key_value->DebugDump(*(int16_t*)arg);
    return PL_DHASH_NEXT;
}
#endif

void
XPCJSRuntime::DebugDump(int16_t depth)
{
#ifdef DEBUG
    depth--;
    XPC_LOG_ALWAYS(("XPCJSRuntime @ %x", this));
        XPC_LOG_INDENT();
        XPC_LOG_ALWAYS(("mJSRuntime @ %x", Runtime()));
        XPC_LOG_ALWAYS(("mMapLock @ %x", mMapLock));

        XPC_LOG_ALWAYS(("mWrappedJSToReleaseArray @ %x with %d wrappers(s)", \
                        &mWrappedJSToReleaseArray,
                        mWrappedJSToReleaseArray.Length()));

        int cxCount = 0;
        JSContext* iter = nullptr;
        while (JS_ContextIterator(Runtime(), &iter))
            ++cxCount;
        XPC_LOG_ALWAYS(("%d JS context(s)", cxCount));

        iter = nullptr;
        while (JS_ContextIterator(Runtime(), &iter)) {
            XPCContext *xpc = XPCContext::GetXPCContext(iter);
            XPC_LOG_INDENT();
            xpc->DebugDump(depth);
            XPC_LOG_OUTDENT();
        }

        XPC_LOG_ALWAYS(("mWrappedJSClassMap @ %x with %d wrapperclasses(s)",  \
                        mWrappedJSClassMap, mWrappedJSClassMap ?              \
                        mWrappedJSClassMap->Count() : 0));
        // iterate wrappersclasses...
        if (depth && mWrappedJSClassMap && mWrappedJSClassMap->Count()) {
            XPC_LOG_INDENT();
            mWrappedJSClassMap->Enumerate(WrappedJSClassMapDumpEnumerator, &depth);
            XPC_LOG_OUTDENT();
        }
        XPC_LOG_ALWAYS(("mWrappedJSMap @ %x with %d wrappers(s)",             \
                        mWrappedJSMap, mWrappedJSMap ?                        \
                        mWrappedJSMap->Count() : 0));
        // iterate wrappers...
        if (depth && mWrappedJSMap && mWrappedJSMap->Count()) {
            XPC_LOG_INDENT();
            mWrappedJSMap->Dump(depth);
            XPC_LOG_OUTDENT();
        }

        XPC_LOG_ALWAYS(("mIID2NativeInterfaceMap @ %x with %d interface(s)",  \
                        mIID2NativeInterfaceMap, mIID2NativeInterfaceMap ?    \
                        mIID2NativeInterfaceMap->Count() : 0));

        XPC_LOG_ALWAYS(("mClassInfo2NativeSetMap @ %x with %d sets(s)",       \
                        mClassInfo2NativeSetMap, mClassInfo2NativeSetMap ?    \
                        mClassInfo2NativeSetMap->Count() : 0));

        XPC_LOG_ALWAYS(("mThisTranslatorMap @ %x with %d translator(s)",      \
                        mThisTranslatorMap, mThisTranslatorMap ?              \
                        mThisTranslatorMap->Count() : 0));

        XPC_LOG_ALWAYS(("mNativeSetMap @ %x with %d sets(s)",                 \
                        mNativeSetMap, mNativeSetMap ?                        \
                        mNativeSetMap->Count() : 0));

        // iterate sets...
        if (depth && mNativeSetMap && mNativeSetMap->Count()) {
            XPC_LOG_INDENT();
            mNativeSetMap->Enumerate(NativeSetDumpEnumerator, &depth);
            XPC_LOG_OUTDENT();
        }

        XPC_LOG_OUTDENT();
#endif
}

/***************************************************************************/

void
XPCRootSetElem::AddToRootSet(XPCLock *lock, XPCRootSetElem **listHead)
{
    MOZ_ASSERT(!mSelfp, "Must be not linked");

    XPCAutoLock autoLock(lock);

    mSelfp = listHead;
    mNext = *listHead;
    if (mNext) {
        MOZ_ASSERT(mNext->mSelfp == listHead, "Must be list start");
        mNext->mSelfp = &mNext;
    }
    *listHead = this;
}

void
XPCRootSetElem::RemoveFromRootSet(XPCLock *lock)
{
    nsXPConnect *xpc = nsXPConnect::XPConnect();
    JS::PokeGC(xpc->GetRuntime()->Runtime());

    MOZ_ASSERT(mSelfp, "Must be linked");

    XPCAutoLock autoLock(lock);

    MOZ_ASSERT(*mSelfp == this, "Link invariant");
    *mSelfp = mNext;
    if (mNext)
        mNext->mSelfp = mSelfp;
#ifdef DEBUG
    mSelfp = nullptr;
    mNext = nullptr;
#endif
}

void
XPCJSRuntime::AddGCCallback(xpcGCCallback cb)
{
    MOZ_ASSERT(cb, "null callback");
    extraGCCallbacks.AppendElement(cb);
}

void
XPCJSRuntime::RemoveGCCallback(xpcGCCallback cb)
{
    MOZ_ASSERT(cb, "null callback");
    bool found = extraGCCallbacks.RemoveElement(cb);
    if (!found) {
        NS_ERROR("Removing a callback which was never added.");
    }
}

void
XPCJSRuntime::AddContextCallback(xpcContextCallback cb)
{
    MOZ_ASSERT(cb, "null callback");
    extraContextCallbacks.AppendElement(cb);
}

void
XPCJSRuntime::RemoveContextCallback(xpcContextCallback cb)
{
    MOZ_ASSERT(cb, "null callback");
    bool found = extraContextCallbacks.RemoveElement(cb);
    if (!found) {
        NS_ERROR("Removing a callback which was never added.");
    }
}

JSObject *
XPCJSRuntime::GetJunkScope()
{
    if (!mJunkScope) {
        AutoSafeJSContext cx;
        SandboxOptions options(cx);
        options.sandboxName.AssignASCII("XPConnect Junk Compartment");
        RootedValue v(cx);
        nsresult rv = xpc_CreateSandboxObject(cx, v.address(),
                                              nsContentUtils::GetSystemPrincipal(),
                                              options);

        NS_ENSURE_SUCCESS(rv, nullptr);

        mJunkScope = js::UncheckedUnwrap(&v.toObject());
        JS_AddNamedObjectRoot(cx, &mJunkScope, "XPConnect Junk Compartment");
    }
    return mJunkScope;
}

void
XPCJSRuntime::DeleteJunkScope()
{
    if(!mJunkScope)
        return;

    AutoSafeJSContext cx;
    JS_RemoveObjectRoot(cx, &mJunkScope);
    mJunkScope = nullptr;
}
