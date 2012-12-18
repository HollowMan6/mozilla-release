/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsOfflineCacheUpdate_h__
#define nsOfflineCacheUpdate_h__

#include "nsIOfflineCacheUpdate.h"

#include "nsAutoPtr.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsICacheService.h"
#include "nsIChannelEventSink.h"
#include "nsIDOMDocument.h"
#include "nsIDOMNode.h"
#include "nsIDOMLoadStatus.h"
#include "nsIInterfaceRequestor.h"
#include "nsIMutableArray.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIApplicationCache.h"
#include "nsIRequestObserver.h"
#include "nsIRunnable.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIWebProgressListener.h"
#include "nsClassHashtable.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWeakReference.h"
#include "nsICryptoHash.h"
#include "mozilla/Attributes.h"

class nsOfflineCacheUpdate;

class nsICacheEntryDescriptor;
class nsIUTF8StringEnumerator;
class nsILoadContext;

class nsOfflineCacheUpdateItem : public nsIDOMLoadStatus
                               , public nsIStreamListener
                               , public nsIRunnable
                               , public nsIInterfaceRequestor
                               , public nsIChannelEventSink
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIDOMLOADSTATUS
    NS_DECL_NSIREQUESTOBSERVER
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIRUNNABLE
    NS_DECL_NSIINTERFACEREQUESTOR
    NS_DECL_NSICHANNELEVENTSINK

    nsOfflineCacheUpdateItem(nsIURI *aURI,
                             nsIURI *aReferrerURI,
                             nsIApplicationCache *aApplicationCache,
                             nsIApplicationCache *aPreviousApplicationCache,
                             uint32_t aType);
    virtual ~nsOfflineCacheUpdateItem();

    nsCOMPtr<nsIURI>           mURI;
    nsCOMPtr<nsIURI>           mReferrerURI;
    nsCOMPtr<nsIApplicationCache> mApplicationCache;
    nsCOMPtr<nsIApplicationCache> mPreviousApplicationCache;
    nsCString                  mCacheKey;
    uint32_t                   mItemType;

    nsresult OpenChannel(nsOfflineCacheUpdate *aUpdate);
    nsresult Cancel();
    nsresult GetRequestSucceeded(bool * succeeded);

    bool IsInProgress();
    bool IsScheduled();
    bool IsCompleted();

private:
    nsRefPtr<nsOfflineCacheUpdate> mUpdate;
    nsCOMPtr<nsIChannel>           mChannel;
    uint16_t                       mState;

protected:
    int64_t                        mBytesRead;
};


class nsOfflineManifestItem : public nsOfflineCacheUpdateItem
{
public:
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIREQUESTOBSERVER

    nsOfflineManifestItem(nsIURI *aURI,
                          nsIURI *aReferrerURI,
                          nsIApplicationCache *aApplicationCache,
                          nsIApplicationCache *aPreviousApplicationCache);
    virtual ~nsOfflineManifestItem();

    nsCOMArray<nsIURI> &GetExplicitURIs() { return mExplicitURIs; }
    nsCOMArray<nsIURI> &GetFallbackURIs() { return mFallbackURIs; }

    nsTArray<nsCString> &GetOpportunisticNamespaces()
        { return mOpportunisticNamespaces; }
    nsIArray *GetNamespaces()
        { return mNamespaces.get(); }

    bool ParseSucceeded()
        { return (mParserState != PARSE_INIT && mParserState != PARSE_ERROR); }
    bool NeedsUpdate() { return mParserState != PARSE_INIT && mNeedsUpdate; }

    void GetManifestHash(nsCString &aManifestHash)
        { aManifestHash = mManifestHashValue; }

private:
    static NS_METHOD ReadManifest(nsIInputStream *aInputStream,
                                  void *aClosure,
                                  const char *aFromSegment,
                                  uint32_t aOffset,
                                  uint32_t aCount,
                                  uint32_t *aBytesConsumed);

    nsresult AddNamespace(uint32_t namespaceType,
                          const nsCString &namespaceSpec,
                          const nsCString &data);

    nsresult HandleManifestLine(const nsCString::const_iterator &aBegin,
                                const nsCString::const_iterator &aEnd);

    /**
     * Saves "offline-manifest-hash" meta data from the old offline cache
     * token to mOldManifestHashValue member to be compared on
     * successfull load.
     */
    nsresult GetOldManifestContentHash(nsIRequest *aRequest);
    /**
     * This method setups the mNeedsUpdate to false when hash value
     * of the just downloaded manifest file is the same as stored in cache's 
     * "offline-manifest-hash" meta data. Otherwise stores the new value
     * to this meta data.
     */
    nsresult CheckNewManifestContentHash(nsIRequest *aRequest);

    void ReadStrictFileOriginPolicyPref();

    enum {
        PARSE_INIT,
        PARSE_CACHE_ENTRIES,
        PARSE_FALLBACK_ENTRIES,
        PARSE_BYPASS_ENTRIES,
        PARSE_UNKNOWN_SECTION,
        PARSE_ERROR
    } mParserState;

    nsCString mReadBuf;

    nsCOMArray<nsIURI> mExplicitURIs;
    nsCOMArray<nsIURI> mFallbackURIs;

    // All opportunistic caching namespaces.  Used to decide whether
    // to include previously-opportunistically-cached entries.
    nsTArray<nsCString> mOpportunisticNamespaces;

    // Array of nsIApplicationCacheNamespace objects specified by the
    // manifest.
    nsCOMPtr<nsIMutableArray> mNamespaces;

    bool mNeedsUpdate;
    bool mStrictFileOriginPolicy;

    // manifest hash data
    nsCOMPtr<nsICryptoHash> mManifestHash;
    bool mManifestHashInitialized;
    nsCString mManifestHashValue;
    nsCString mOldManifestHashValue;
};

class nsOfflineCacheUpdateOwner
{
public:
    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate) = 0;
};

class nsOfflineCacheUpdate MOZ_FINAL : public nsIOfflineCacheUpdate
                                     , public nsIOfflineCacheUpdateObserver
                                     , public nsIRunnable
                                     , public nsOfflineCacheUpdateOwner
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOFFLINECACHEUPDATE
    NS_DECL_NSIOFFLINECACHEUPDATEOBSERVER
    NS_DECL_NSIRUNNABLE

    nsOfflineCacheUpdate();
    ~nsOfflineCacheUpdate();

    static nsresult GetCacheKey(nsIURI *aURI, nsACString &aKey);

    nsresult Init();

    nsresult Begin();

    void LoadCompleted(nsOfflineCacheUpdateItem *aItem);
    void ManifestCheckCompleted(nsresult aStatus,
                                const nsCString &aManifestHash);
    void StickDocument(nsIURI *aDocumentURI);

    void SetOwner(nsOfflineCacheUpdateOwner *aOwner);

    bool IsForGroupID(const nsCSubstring &groupID);

    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate);

protected:
    friend class nsOfflineCacheUpdateItem;
    void OnByteProgress(uint64_t byteIncrement);

private:
    nsresult InitInternal(nsIURI *aManifestURI);
    nsresult HandleManifest(bool *aDoUpdate);
    nsresult AddURI(nsIURI *aURI, uint32_t aItemType);

    nsresult ProcessNextURI();

    // Adds items from the previous cache witha type matching aType.
    // If namespaceFilter is non-null, only items matching the
    // specified namespaces will be added.
    nsresult AddExistingItems(uint32_t aType,
                              nsTArray<nsCString>* namespaceFilter = nullptr);
    nsresult ScheduleImplicit();
    void AssociateDocuments(nsIApplicationCache* cache);
    bool CheckUpdateAvailability();
    void NotifyUpdateAvailability(bool updateAvailable);

    void GatherObservers(nsCOMArray<nsIOfflineCacheUpdateObserver> &aObservers);
    void NotifyState(uint32_t state);
    nsresult Finish();
    nsresult FinishNoNotify();

    // Find one non-pinned cache group and evict it.
    nsresult EvictOneNonPinned();

    enum {
        STATE_UNINITIALIZED,
        STATE_INITIALIZED,
        STATE_CHECKING,
        STATE_DOWNLOADING,
        STATE_CANCELLED,
        STATE_FINISHED
    } mState;

    nsOfflineCacheUpdateOwner *mOwner;

    bool mAddedItems;
    bool mPartialUpdate;
    bool mOnlyCheckUpdate;
    bool mSucceeded;
    bool mObsolete;

    nsCString mUpdateDomain;
    nsCString mGroupID;
    nsCOMPtr<nsIURI> mManifestURI;
    nsCOMPtr<nsIURI> mDocumentURI;
    nsCOMPtr<nsIFile> mCustomProfileDir;

    uint32_t mAppID;
    bool mInBrowser;

    nsCOMPtr<nsIObserver> mUpdateAvailableObserver;

    nsCOMPtr<nsIApplicationCache> mApplicationCache;
    nsCOMPtr<nsIApplicationCache> mPreviousApplicationCache;

    nsCOMPtr<nsIObserverService> mObserverService;

    nsRefPtr<nsOfflineManifestItem> mManifestItem;

    /* Items being updated */
    uint32_t mItemsInProgress;
    nsTArray<nsRefPtr<nsOfflineCacheUpdateItem> > mItems;

    /* Clients watching this update for changes */
    nsCOMArray<nsIWeakReference> mWeakObservers;
    nsCOMArray<nsIOfflineCacheUpdateObserver> mObservers;

    /* Documents that requested this update */
    nsCOMArray<nsIURI> mDocumentURIs;

    /* Reschedule count.  When an update is rescheduled due to
     * mismatched manifests, the reschedule count will be increased. */
    uint32_t mRescheduleCount;

    /* Whena an entry for a pinned app is retried, retries count is
     * increaded. */
    uint32_t mPinnedEntryRetriesCount;

    nsRefPtr<nsOfflineCacheUpdate> mImplicitUpdate;

    bool                           mPinned;

    uint64_t                       mByteProgress;
};

class nsOfflineCacheUpdateService MOZ_FINAL : public nsIOfflineCacheUpdateService
                                            , public nsIObserver
                                            , public nsOfflineCacheUpdateOwner
                                            , public nsSupportsWeakReference
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOFFLINECACHEUPDATESERVICE
    NS_DECL_NSIOBSERVER

    nsOfflineCacheUpdateService();
    ~nsOfflineCacheUpdateService();

    nsresult Init();

    nsresult ScheduleUpdate(nsOfflineCacheUpdate *aUpdate);
    nsresult FindUpdate(nsIURI *aManifestURI,
                        uint32_t aAppID,
                        bool aInBrowser,
                        nsOfflineCacheUpdate **aUpdate);

    nsresult Schedule(nsIURI *aManifestURI,
                      nsIURI *aDocumentURI,
                      nsIDOMDocument *aDocument,
                      nsIDOMWindow* aWindow,
                      nsIFile* aCustomProfileDir,
                      uint32_t aAppID,
                      bool aInBrowser,
                      nsIOfflineCacheUpdate **aUpdate);

    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate);

    /**
     * Returns the singleton nsOfflineCacheUpdateService without an addref, or
     * nullptr if the service couldn't be created.
     */
    static nsOfflineCacheUpdateService *EnsureService();

    /** Addrefs and returns the singleton nsOfflineCacheUpdateService. */
    static nsOfflineCacheUpdateService *GetInstance();

    static nsresult OfflineAppPinnedForURI(nsIURI *aDocumentURI,
                                           nsIPrefBranch *aPrefBranch,
                                           bool *aPinned);

private:
    nsresult ProcessNextUpdate();

    nsTArray<nsRefPtr<nsOfflineCacheUpdate> > mUpdates;

    bool mDisabled;
    bool mUpdateRunning;
};

#endif
