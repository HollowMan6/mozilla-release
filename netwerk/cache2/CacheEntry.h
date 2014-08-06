/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheEntry__h__
#define CacheEntry__h__

#include "nsICacheEntry.h"
#include "CacheFile.h"

#include "nsIRunnable.h"
#include "nsIOutputStream.h"
#include "nsICacheEntryOpenCallback.h"
#include "nsICacheEntryDoomCallback.h"

#include "nsCOMPtr.h"
#include "nsRefPtrHashtable.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "nsString.h"
#include "nsCOMArray.h"
#include "nsThreadUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"

static inline uint32_t
PRTimeToSeconds(PRTime t_usec)
{
  PRTime usec_per_sec = PR_USEC_PER_SEC;
  return uint32_t(t_usec /= usec_per_sec);
}

#define NowInSeconds() PRTimeToSeconds(PR_Now())

class nsIStorageStream;
class nsIOutputStream;
class nsIURI;
class nsIThread;

namespace mozilla {
namespace net {

class CacheStorageService;
class CacheStorage;
class CacheFileOutputStream;
class CacheOutputCloseListener;
class CacheEntryHandle;

class CacheEntry : public nsICacheEntry
                 , public nsIRunnable
                 , public CacheFileListener
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHEENTRY
  NS_DECL_NSIRUNNABLE

  CacheEntry(const nsACString& aStorageID, nsIURI* aURI, const nsACString& aEnhanceID,
             bool aUseDisk);

  void AsyncOpen(nsICacheEntryOpenCallback* aCallback, uint32_t aFlags);

  CacheEntryHandle* NewHandle();

public:
  uint32_t GetMetadataMemoryConsumption();
  nsCString const &GetStorageID() const { return mStorageID; }
  nsCString const &GetEnhanceID() const { return mEnhanceID; }
  nsIURI* GetURI() const { return mURI; }
  // Accessible at any time
  bool IsUsingDisk() const { return mUseDisk; }
  bool IsReferenced() const;
  bool IsFileDoomed();
  bool IsDoomed() const { return mIsDoomed; }

  // Methods for entry management (eviction from memory),
  // called only on the management thread.

  // TODO make these inline
  double GetFrecency() const;
  uint32_t GetExpirationTime() const;
  uint32_t UseCount() const { return mUseCount; }

  bool IsRegistered() const;
  bool CanRegister() const;
  void SetRegistered(bool aRegistered);

  TimeStamp const& LoadStart() const { return mLoadStart; }

  enum EPurge {
    PURGE_DATA_ONLY_DISK_BACKED,
    PURGE_WHOLE_ONLY_DISK_BACKED,
    PURGE_WHOLE,
  };

  bool Purge(uint32_t aWhat);
  void PurgeAndDoom();
  void DoomAlreadyRemoved();

  nsresult HashingKeyWithStorage(nsACString &aResult) const;
  nsresult HashingKey(nsACString &aResult) const;

  static nsresult HashingKey(nsCSubstring const& aStorageID,
                             nsCSubstring const& aEnhanceID,
                             nsIURI* aURI,
                             nsACString &aResult);

  static nsresult HashingKey(nsCSubstring const& aStorageID,
                             nsCSubstring const& aEnhanceID,
                             nsCSubstring const& aURISpec,
                             nsACString &aResult);

  // Accessed only on the service management thread
  double mFrecency;
  uint32_t mSortingExpirationTime;

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

private:
  virtual ~CacheEntry();

  // CacheFileListener
  NS_IMETHOD OnFileReady(nsresult aResult, bool aIsNew);
  NS_IMETHOD OnFileDoomed(nsresult aResult);

  // Keep the service alive during life-time of an entry
  nsRefPtr<CacheStorageService> mService;

  // We must monitor when a cache entry whose consumer is responsible
  // for writing it the first time gets released.  We must then invoke
  // waiting callbacks to not break the chain.
  class Callback
  {
  public:
    Callback(CacheEntry* aEntry,
             nsICacheEntryOpenCallback *aCallback,
             bool aReadOnly, bool aCheckOnAnyThread);
    Callback(Callback const &aThat);
    ~Callback();

    // Called when this callback record changes it's owning entry,
    // mainly during recreation.
    void ExchangeEntry(CacheEntry* aEntry);

    // We are raising reference count here to take into account the pending
    // callback (that virtually holds a ref to this entry before it gets
    // it's pointer).
    nsRefPtr<CacheEntry> mEntry;
    nsCOMPtr<nsICacheEntryOpenCallback> mCallback;
    nsCOMPtr<nsIThread> mTargetThread;
    bool mReadOnly : 1;
    bool mCheckOnAnyThread : 1;
    bool mRecheckAfterWrite : 1;
    bool mNotWanted : 1;

    nsresult OnCheckThread(bool *aOnCheckThread) const;
    nsresult OnAvailThread(bool *aOnAvailThread) const;
  };

  // Since OnCacheEntryAvailable must be invoked on the main thread
  // we need a runnable for it...
  class AvailableCallbackRunnable : public nsRunnable
  {
  public:
    AvailableCallbackRunnable(CacheEntry* aEntry,
                              Callback const &aCallback)
      : mEntry(aEntry)
      , mCallback(aCallback)
    {}

  private:
    NS_IMETHOD Run()
    {
      mEntry->InvokeAvailableCallback(mCallback);
      return NS_OK;
    }

    nsRefPtr<CacheEntry> mEntry;
    Callback mCallback;
  };

  // Since OnCacheEntryDoomed must be invoked on the main thread
  // we need a runnable for it...
  class DoomCallbackRunnable : public nsRunnable
  {
  public:
    DoomCallbackRunnable(CacheEntry* aEntry, nsresult aRv)
      : mEntry(aEntry), mRv(aRv) {}

  private:
    NS_IMETHOD Run()
    {
      nsCOMPtr<nsICacheEntryDoomCallback> callback;
      {
        mozilla::MutexAutoLock lock(mEntry->mLock);
        mEntry->mDoomCallback.swap(callback);
      }

      if (callback)
        callback->OnCacheEntryDoomed(mRv);
      return NS_OK;
    }

    nsRefPtr<CacheEntry> mEntry;
    nsresult mRv;
  };

  // Starts the load or just invokes the callback, bypasses (when required)
  // if busy.  Returns true on job done, false on bypass.
  bool Open(Callback & aCallback, bool aTruncate, bool aPriority, bool aBypassIfBusy);
  // Loads from disk asynchronously
  bool Load(bool aTruncate, bool aPriority);
  void OnLoaded();

  void RememberCallback(Callback & aCallback);
  void InvokeCallbacksLock();
  void InvokeCallbacks();
  bool InvokeCallbacks(bool aReadOnly);
  bool InvokeCallback(Callback & aCallback);
  void InvokeAvailableCallback(Callback const & aCallback);

  nsresult OpenOutputStreamInternal(int64_t offset, nsIOutputStream * *_retval);

  // When this entry is new and recreated w/o a callback, we need to wrap it
  // with a handle to detect writing consumer is gone.
  CacheEntryHandle* NewWriteHandle();
  void OnHandleClosed(CacheEntryHandle const* aHandle);

private:
  friend class CacheEntryHandle;
  // Increment/decrements the number of handles keeping this entry.
  void AddHandleRef() { ++mHandlesCount; }
  void ReleaseHandleRef() { --mHandlesCount; }
  // Current number of handles keeping this entry.
  uint32_t HandlesCount() const { return mHandlesCount; }

private:
  friend class CacheOutputCloseListener;
  void OnOutputClosed();

private:
  // Schedules a background operation on the management thread.
  // When executed on the management thread directly, the operation(s)
  // is (are) executed immediately.
  void BackgroundOp(uint32_t aOperation, bool aForceAsync = false);
  void StoreFrecency();

  // Called only from DoomAlreadyRemoved()
  void DoomFile();

  already_AddRefed<CacheEntryHandle> ReopenTruncated(bool aMemoryOnly,
                                                     nsICacheEntryOpenCallback* aCallback);
  void TransferCallbacks(CacheEntry & aFromEntry);

  mozilla::Mutex mLock;

  // Reflects the number of existing handles for this entry
  ::mozilla::ThreadSafeAutoRefCnt mHandlesCount;

  nsTArray<Callback> mCallbacks;
  nsCOMPtr<nsICacheEntryDoomCallback> mDoomCallback;

  nsRefPtr<CacheFile> mFile;
  nsresult mFileStatus;
  nsCOMPtr<nsIURI> mURI;
  nsCString mEnhanceID;
  nsCString mStorageID;

  // Whether it's allowed to persist the data to disk
  bool const mUseDisk;

  // Set when entry is doomed with AsyncDoom() or DoomAlreadyRemoved().
  // Left as a standalone flag to not bother with locking (there is no need).
  bool mIsDoomed;

  // Following flags are all synchronized with the cache entry lock.

  // Whether security info has already been looked up in metadata.
  bool mSecurityInfoLoaded : 1;
  // Prevents any callback invocation
  bool mPreventCallbacks : 1;
  // true: after load and an existing file, or after output stream has been opened.
  //       note - when opening an input stream, and this flag is false, output stream
  //       is open along ; this makes input streams on new entries behave correctly
  //       when EOF is reached (WOULD_BLOCK is returned).
  // false: after load and a new file, or dropped to back to false when a writer
  //        fails to open an output stream.
  bool mHasData : 1;

#ifdef PR_LOG
  static char const * StateString(uint32_t aState);
#endif

  enum EState {      // transiting to:
    NOTLOADED = 0,   // -> LOADING | EMPTY
    LOADING = 1,     // -> EMPTY | READY
    EMPTY = 2,       // -> WRITING
    WRITING = 3,     // -> EMPTY | READY
    READY = 4,       // -> REVALIDATING
    REVALIDATING = 5 // -> READY
  };

  // State of this entry.
  EState mState;

  enum ERegistration {
    NEVERREGISTERED = 0, // The entry has never been registered
    REGISTERED = 1,      // The entry is stored in the memory pool index
    DEREGISTERED = 2     // The entry has been removed from the pool
  };

  // Accessed only on the management thread.  Records the state of registration
  // this entry in the memory pool intermediate cache.
  ERegistration mRegistration;

  // If a new (empty) entry is requested to open an input stream before
  // output stream has been opened, we must open output stream internally
  // on CacheFile and hold until writer releases the entry or opens the output
  // stream for read (then we trade him mOutputStream).
  nsCOMPtr<nsIOutputStream> mOutputStream;

  // Weak reference to the current writter.  There can be more then one
  // writer at a time and OnHandleClosed() must be processed only for the
  // current one.
  CacheEntryHandle* mWriter;

  // Background thread scheduled operation.  Set (under the lock) one
  // of this flags to tell the background thread what to do.
  class Ops {
  public:
    static uint32_t const REGISTER =          1 << 0;
    static uint32_t const FRECENCYUPDATE =    1 << 1;
    static uint32_t const CALLBACKS =         1 << 2;
    static uint32_t const UNREGISTER =        1 << 3;

    Ops() : mFlags(0) { }
    uint32_t Grab() { uint32_t flags = mFlags; mFlags = 0; return flags; }
    bool Set(uint32_t aFlags) { if (mFlags & aFlags) return false; mFlags |= aFlags; return true; }
  private:
    uint32_t mFlags;
  } mBackgroundOperations;

  nsCOMPtr<nsISupports> mSecurityInfo;
  int64_t mPredictedDataSize;
  mozilla::TimeStamp mLoadStart;
  uint32_t mUseCount;
  nsCOMPtr<nsIThread> mReleaseThread;
};


class CacheEntryHandle : public nsICacheEntry
{
public:
  explicit CacheEntryHandle(CacheEntry* aEntry);
  CacheEntry* Entry() const { return mEntry; }

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_FORWARD_NSICACHEENTRY(mEntry->)
private:
  virtual ~CacheEntryHandle();
  nsRefPtr<CacheEntry> mEntry;
};


class CacheOutputCloseListener MOZ_FINAL : public nsRunnable
{
public:
  void OnOutputClosed();

private:
  friend class CacheEntry;

  virtual ~CacheOutputCloseListener();

  NS_DECL_NSIRUNNABLE
  explicit CacheOutputCloseListener(CacheEntry* aEntry);

private:
  nsRefPtr<CacheEntry> mEntry;
};

} // net
} // mozilla

#endif
