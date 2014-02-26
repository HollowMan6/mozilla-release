/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheFileIOManager__h__
#define CacheFileIOManager__h__

#include "CacheIOThread.h"
#include "CacheEntriesEnumerator.h"
#include "nsIEventTarget.h"
#include "nsITimer.h"
#include "nsCOMPtr.h"
#include "mozilla/SHA1.h"
#include "mozilla/TimeStamp.h"
#include "nsTArray.h"
#include "nsString.h"
#include "nsTHashtable.h"
#include "prio.h"

//#define DEBUG_HANDLES 1

class nsIFile;
class nsITimer;
class nsIDirectoryEnumerator;

namespace mozilla {
namespace net {

#ifdef DEBUG_HANDLES
class CacheFileHandlesEntry;
#endif

const char kEntriesDir[] = "entries";
const char kDoomedDir[]  = "doomed";
const char kTrashDir[]   = "trash";


class CacheFileHandle : public nsISupports
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  bool DispatchRelease();

  CacheFileHandle(const SHA1Sum::Hash *aHash, bool aPriority);
  CacheFileHandle(const nsACString &aKey, bool aPriority);
  CacheFileHandle(const CacheFileHandle &aOther);
  void Log();
  bool IsDoomed() const { return mIsDoomed; }
  const SHA1Sum::Hash *Hash() const { return mHash; }
  int64_t FileSize() const { return mFileSize; }
  uint32_t FileSizeInK() const;
  bool IsPriority() const { return mPriority; }
  bool FileExists() const { return mFileExists; }
  bool IsClosed() const { return mClosed; }
  bool IsSpecialFile() const { return !mHash; }
  nsCString & Key() { return mKey; }

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

private:
  friend class CacheFileIOManager;
  friend class CacheFileHandles;
  friend class ReleaseNSPRHandleEvent;

  virtual ~CacheFileHandle();

  const SHA1Sum::Hash *mHash;
  bool                 mIsDoomed;
  bool                 mPriority;
  bool                 mClosed;
  bool                 mInvalid;
  bool                 mFileExists; // This means that the file should exists,
                                    // but it can be still deleted by OS/user
                                    // and then a subsequent OpenNSPRFileDesc()
                                    // will fail.
  nsCOMPtr<nsIFile>    mFile;
  int64_t              mFileSize;
  PRFileDesc          *mFD;  // if null then the file doesn't exists on the disk
  nsCString            mKey;
};

class CacheFileHandles {
public:
  CacheFileHandles();
  ~CacheFileHandles();

  nsresult GetHandle(const SHA1Sum::Hash *aHash, bool aReturnDoomed, CacheFileHandle **_retval);
  nsresult NewHandle(const SHA1Sum::Hash *aHash, bool aPriority, CacheFileHandle **_retval);
  void     RemoveHandle(CacheFileHandle *aHandlle);
  void     GetAllHandles(nsTArray<nsRefPtr<CacheFileHandle> > *_retval);
  void     ClearAll();
  uint32_t HandleCount();

#ifdef DEBUG_HANDLES
  void     Log(CacheFileHandlesEntry *entry);
#endif

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  class HandleHashKey : public PLDHashEntryHdr
  {
  public:
    typedef const SHA1Sum::Hash& KeyType;
    typedef const SHA1Sum::Hash* KeyTypePointer;

    HandleHashKey(KeyTypePointer aKey)
    {
      MOZ_COUNT_CTOR(HandleHashKey);
      mHash = (SHA1Sum::Hash*)new uint8_t[SHA1Sum::HashSize];
      memcpy(mHash, aKey, sizeof(SHA1Sum::Hash));
    }
    HandleHashKey(const HandleHashKey& aOther)
    {
      NS_NOTREACHED("HandleHashKey copy constructor is forbidden!");
    }
    ~HandleHashKey()
    {
      MOZ_COUNT_DTOR(HandleHashKey);
    }

    bool KeyEquals(KeyTypePointer aKey) const
    {
      return memcmp(mHash, aKey, sizeof(SHA1Sum::Hash)) == 0;
    }
    static KeyTypePointer KeyToPointer(KeyType aKey)
    {
      return &aKey;
    }
    static PLDHashNumber HashKey(KeyTypePointer aKey)
    {
      return (reinterpret_cast<const uint32_t *>(aKey))[0];
    }

    void AddHandle(CacheFileHandle* aHandle);
    void RemoveHandle(CacheFileHandle* aHandle);
    already_AddRefed<CacheFileHandle> GetNewestHandle();
    void GetHandles(nsTArray<nsRefPtr<CacheFileHandle> > &aResult);

    SHA1Sum::Hash *Hash() const { return mHash; }
    bool IsEmpty() const { return mHandles.Length() == 0; }

    enum { ALLOW_MEMMOVE = true };

#ifdef DEBUG
    void AssertHandlesState();
#endif

    // Memory reporting
    size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
    size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;

  private:
    nsAutoArrayPtr<SHA1Sum::Hash> mHash;
    // Use weak pointers since the hash table access is on a single thread
    // only and CacheFileHandle removes itself from this table in its dtor
    // that may only be called on the same thread as we work with the hashtable
    // since we dispatch its Release() to this thread.
    nsTArray<CacheFileHandle*> mHandles;
  };

private:
  nsTHashtable<HandleHashKey> mTable;
};

////////////////////////////////////////////////////////////////////////////////

class OpenFileEvent;
class CloseFileEvent;
class ReadEvent;
class WriteEvent;
class MetadataWriteScheduleEvent;

#define CACHEFILEIOLISTENER_IID \
{ /* dcaf2ddc-17cf-4242-bca1-8c86936375a5 */       \
  0xdcaf2ddc,                                      \
  0x17cf,                                          \
  0x4242,                                          \
  {0xbc, 0xa1, 0x8c, 0x86, 0x93, 0x63, 0x75, 0xa5} \
}

class CacheFileIOListener : public nsISupports
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(CACHEFILEIOLISTENER_IID)

  NS_IMETHOD OnFileOpened(CacheFileHandle *aHandle, nsresult aResult) = 0;
  NS_IMETHOD OnDataWritten(CacheFileHandle *aHandle, const char *aBuf,
                           nsresult aResult) = 0;
  NS_IMETHOD OnDataRead(CacheFileHandle *aHandle, char *aBuf,
                        nsresult aResult) = 0;
  NS_IMETHOD OnFileDoomed(CacheFileHandle *aHandle, nsresult aResult) = 0;
  NS_IMETHOD OnEOFSet(CacheFileHandle *aHandle, nsresult aResult) = 0;
  NS_IMETHOD OnFileRenamed(CacheFileHandle *aHandle, nsresult aResult) = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(CacheFileIOListener, CACHEFILEIOLISTENER_IID)


class CacheFileIOManager : public nsITimerCallback
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK

  enum {
    OPEN         = 0U,
    CREATE       = 1U,
    CREATE_NEW   = 2U,
    PRIORITY     = 4U,
    SPECIAL_FILE = 8U
  };

  CacheFileIOManager();

  static nsresult Init();
  static nsresult Shutdown();
  static nsresult OnProfile();
  static already_AddRefed<nsIEventTarget> IOTarget();
  static already_AddRefed<CacheIOThread> IOThread();
  static bool IsOnIOThread();
  static bool IsOnIOThreadOrCeased();
  static bool IsShutdown();

  // Make aFile's WriteMetadataIfNeeded be called automatically after
  // a short interval.
  static nsresult ScheduleMetadataWrite(CacheFile * aFile);
  // Remove aFile from the scheduling registry array.
  // WriteMetadataIfNeeded will not be automatically called.
  static nsresult UnscheduleMetadataWrite(CacheFile * aFile);
  // Shuts the scheduling off and flushes all pending metadata writes.
  static nsresult ShutdownMetadataWriteScheduling();

  static nsresult OpenFile(const nsACString &aKey,
                           uint32_t aFlags,
                           CacheFileIOListener *aCallback);
  static nsresult Read(CacheFileHandle *aHandle, int64_t aOffset,
                       char *aBuf, int32_t aCount,
                       CacheFileIOListener *aCallback);
  static nsresult Write(CacheFileHandle *aHandle, int64_t aOffset,
                        const char *aBuf, int32_t aCount, bool aValidate,
                        CacheFileIOListener *aCallback);
  static nsresult DoomFile(CacheFileHandle *aHandle,
                           CacheFileIOListener *aCallback);
  static nsresult DoomFileByKey(const nsACString &aKey,
                                CacheFileIOListener *aCallback);
  static nsresult ReleaseNSPRHandle(CacheFileHandle *aHandle);
  static nsresult TruncateSeekSetEOF(CacheFileHandle *aHandle,
                                     int64_t aTruncatePos, int64_t aEOFPos,
                                     CacheFileIOListener *aCallback);
  static nsresult RenameFile(CacheFileHandle *aHandle,
                             const nsACString &aNewName,
                             CacheFileIOListener *aCallback);
  static nsresult EvictIfOverLimit();

  static nsresult InitIndexEntry(CacheFileHandle *aHandle,
                                 uint32_t         aAppId,
                                 bool             aAnonymous,
                                 bool             aInBrowser);
  static nsresult UpdateIndexEntry(CacheFileHandle *aHandle,
                                   const uint32_t  *aFrecency,
                                   const uint32_t  *aExpirationTime);

  static nsresult UpdateIndexEntry();

  enum EEnumerateMode {
    ENTRIES,
    DOOMED
  };

  static nsresult EnumerateEntryFiles(EEnumerateMode aMode,
                                      CacheEntriesEnumerator** aEnumerator);

  static void GetCacheDirectory(nsIFile** result);

  // Memory reporting
  static size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);
  static size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

private:
  friend class CacheFileHandle;
  friend class CacheFileChunk;
  friend class CacheFile;
  friend class ShutdownEvent;
  friend class OpenFileEvent;
  friend class CloseHandleEvent;
  friend class ReadEvent;
  friend class WriteEvent;
  friend class DoomFileEvent;
  friend class DoomFileByKeyEvent;
  friend class ReleaseNSPRHandleEvent;
  friend class TruncateSeekSetEOFEvent;
  friend class RenameFileEvent;
  friend class CacheIndex;
  friend class MetadataWriteScheduleEvent;

  virtual ~CacheFileIOManager();

  nsresult InitInternal();
  nsresult ShutdownInternal();

  nsresult OpenFileInternal(const SHA1Sum::Hash *aHash,
                            uint32_t aFlags,
                            CacheFileHandle **_retval);
  nsresult OpenSpecialFileInternal(const nsACString &aKey,
                                   uint32_t aFlags,
                                   CacheFileHandle **_retval);
  nsresult CloseHandleInternal(CacheFileHandle *aHandle);
  nsresult ReadInternal(CacheFileHandle *aHandle, int64_t aOffset,
                        char *aBuf, int32_t aCount);
  nsresult WriteInternal(CacheFileHandle *aHandle, int64_t aOffset,
                         const char *aBuf, int32_t aCount, bool aValidate);
  nsresult DoomFileInternal(CacheFileHandle *aHandle);
  nsresult DoomFileByKeyInternal(const SHA1Sum::Hash *aHash);
  nsresult ReleaseNSPRHandleInternal(CacheFileHandle *aHandle);
  nsresult TruncateSeekSetEOFInternal(CacheFileHandle *aHandle,
                                      int64_t aTruncatePos, int64_t aEOFPos);
  nsresult RenameFileInternal(CacheFileHandle *aHandle,
                              const nsACString &aNewName);
  nsresult EvictIfOverLimitInternal();
  nsresult OverLimitEvictionInternal();

  nsresult TrashDirectory(nsIFile *aFile);
  static void OnTrashTimer(nsITimer *aTimer, void *aClosure);
  nsresult StartRemovingTrash();
  nsresult RemoveTrashInternal();
  nsresult FindTrashDirToRemove();

  nsresult CreateFile(CacheFileHandle *aHandle);
  static void HashToStr(const SHA1Sum::Hash *aHash, nsACString &_retval);
  static nsresult StrToHash(const nsACString &aHash, SHA1Sum::Hash *_retval);
  nsresult GetFile(const SHA1Sum::Hash *aHash, nsIFile **_retval);
  nsresult GetSpecialFile(const nsACString &aKey, nsIFile **_retval);
  nsresult GetDoomedFile(nsIFile **_retval);
  nsresult IsEmptyDirectory(nsIFile *aFile, bool *_retval);
  nsresult CheckAndCreateDir(nsIFile *aFile, const char *aDir,
                             bool aEnsureEmptyDir);
  nsresult CreateCacheTree();
  nsresult OpenNSPRHandle(CacheFileHandle *aHandle, bool aCreate = false);
  void     NSPRHandleUsed(CacheFileHandle *aHandle);

  nsresult ScheduleMetadataWriteInternal(CacheFile * aFile);
  nsresult UnscheduleMetadataWriteInternal(CacheFile * aFile);
  nsresult ShutdownMetadataWriteSchedulingInternal();

  // Memory reporting (private part)
  size_t SizeOfExcludingThisInternal(mozilla::MallocSizeOf mallocSizeOf) const;

  static CacheFileIOManager           *gInstance;
  TimeStamp                            mStartTime;
  bool                                 mShuttingDown;
  nsRefPtr<CacheIOThread>              mIOThread;
  nsCOMPtr<nsIFile>                    mCacheDirectory;
  bool                                 mTreeCreated;
  CacheFileHandles                     mHandles;
  nsTArray<CacheFileHandle *>          mHandlesByLastUsed;
  nsTArray<nsRefPtr<CacheFileHandle> > mSpecialHandles;
  nsTArray<nsRefPtr<CacheFile> >       mScheduledMetadataWrites;
  nsCOMPtr<nsITimer>                   mMetadataWritesTimer;
  bool                                 mOverLimitEvicting;
  bool                                 mRemovingTrashDirs;
  nsCOMPtr<nsITimer>                   mTrashTimer;
  nsCOMPtr<nsIFile>                    mTrashDir;
  nsCOMPtr<nsIDirectoryEnumerator>     mTrashDirEnumerator;
  nsTArray<nsCString>                  mFailedTrashDirs;
};

} // net
} // mozilla

#endif
