/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Double hashing implementation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pldhash.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/MathAlgorithms.h"
#include "nsDebug.h"     /* for PR_ASSERT */
#include "nsAlgorithm.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ChaosMode.h"

#ifdef PL_DHASHMETER
# define METER(x)       x
#else
# define METER(x)       /* nothing */
#endif

/*
 * The following DEBUG-only code is used to assert that calls to one of
 * table->ops or to an enumerator do not cause re-entry into a call that
 * can mutate the table.
 */
#ifdef DEBUG

/*
 * Most callers that assert about the recursion level don't care about
 * this magical value because they are asserting that mutation is
 * allowed (and therefore the level is 0 or 1, depending on whether they
 * incremented it).
 *
 * Only PL_DHashTableFinish needs to allow this special value.
 */
#define IMMUTABLE_RECURSION_LEVEL ((uint16_t)-1)

#define RECURSION_LEVEL_SAFE_TO_FINISH(table_)                                \
    (table_->recursionLevel == 0 ||                                           \
     table_->recursionLevel == IMMUTABLE_RECURSION_LEVEL)

#define INCREMENT_RECURSION_LEVEL(table_)                                     \
    do {                                                                      \
        if (table_->recursionLevel != IMMUTABLE_RECURSION_LEVEL)              \
            ++table_->recursionLevel;                                         \
    } while(0)
#define DECREMENT_RECURSION_LEVEL(table_)                                     \
    do {                                                                      \
        if (table_->recursionLevel != IMMUTABLE_RECURSION_LEVEL) {            \
            MOZ_ASSERT(table_->recursionLevel > 0);                           \
            --table_->recursionLevel;                                         \
        }                                                                     \
    } while(0)

#else

#define INCREMENT_RECURSION_LEVEL(table_)   do { } while(0)
#define DECREMENT_RECURSION_LEVEL(table_)   do { } while(0)

#endif /* defined(DEBUG) */

using namespace mozilla;

void*
PL_DHashAllocTable(PLDHashTable* aTable, uint32_t aNBytes)
{
  return malloc(aNBytes);
}

void
PL_DHashFreeTable(PLDHashTable* aTable, void* aPtr)
{
  free(aPtr);
}

PLDHashNumber
PL_DHashStringKey(PLDHashTable* aTable, const void* aKey)
{
  return HashString(static_cast<const char*>(aKey));
}

PLDHashNumber
PL_DHashVoidPtrKeyStub(PLDHashTable* aTable, const void* aKey)
{
  return (PLDHashNumber)(ptrdiff_t)aKey >> 2;
}

bool
PL_DHashMatchEntryStub(PLDHashTable* aTable,
                       const PLDHashEntryHdr* aEntry,
                       const void* aKey)
{
  const PLDHashEntryStub* stub = (const PLDHashEntryStub*)aEntry;

  return stub->key == aKey;
}

bool
PL_DHashMatchStringKey(PLDHashTable* aTable,
                       const PLDHashEntryHdr* aEntry,
                       const void* aKey)
{
  const PLDHashEntryStub* stub = (const PLDHashEntryStub*)aEntry;

  /* XXX tolerate null keys on account of sloppy Mozilla callers. */
  return stub->key == aKey ||
         (stub->key && aKey &&
          strcmp((const char*)stub->key, (const char*)aKey) == 0);
}

MOZ_ALWAYS_INLINE void
PLDHashTable::MoveEntryStub(const PLDHashEntryHdr* aFrom,
                            PLDHashEntryHdr* aTo)
{
  memcpy(aTo, aFrom, entrySize);
}

void
PL_DHashMoveEntryStub(PLDHashTable* aTable,
                      const PLDHashEntryHdr* aFrom,
                      PLDHashEntryHdr* aTo)
{
  aTable->MoveEntryStub(aFrom, aTo);
}

MOZ_ALWAYS_INLINE void
PLDHashTable::ClearEntryStub(PLDHashEntryHdr* aEntry)
{
  memset(aEntry, 0, entrySize);
}

void
PL_DHashClearEntryStub(PLDHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  aTable->ClearEntryStub(aEntry);
}

MOZ_ALWAYS_INLINE void
PLDHashTable::FreeStringKey(PLDHashEntryHdr* aEntry)
{
  const PLDHashEntryStub* stub = (const PLDHashEntryStub*)aEntry;

  free((void*)stub->key);
  memset(aEntry, 0, entrySize);
}

void
PL_DHashFreeStringKey(PLDHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  aTable->FreeStringKey(aEntry);
}

void
PL_DHashFinalizeStub(PLDHashTable* aTable)
{
}

static const PLDHashTableOps stub_ops = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  PL_DHashMatchEntryStub,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  nullptr
};

const PLDHashTableOps*
PL_DHashGetStubOps(void)
{
  return &stub_ops;
}

static bool
SizeOfEntryStore(uint32_t aCapacity, uint32_t aEntrySize, uint32_t* aNbytes)
{
  uint64_t nbytes64 = uint64_t(aCapacity) * uint64_t(aEntrySize);
  *aNbytes = aCapacity * aEntrySize;
  return uint64_t(*aNbytes) == nbytes64;   // returns false on overflow
}

PLDHashTable*
PL_NewDHashTable(const PLDHashTableOps* aOps, void* aData, uint32_t aEntrySize,
                 uint32_t aLength)
{
  PLDHashTable* table = (PLDHashTable*)malloc(sizeof(*table));
  if (!table) {
    return nullptr;
  }
  if (!PL_DHashTableInit(table, aOps, aData, aEntrySize, fallible_t(),
                         aLength)) {
    free(table);
    return nullptr;
  }
  return table;
}

void
PL_DHashTableDestroy(PLDHashTable* aTable)
{
  PL_DHashTableFinish(aTable);
  free(aTable);
}

/*
 * Compute max and min load numbers (entry counts).  We have a secondary max
 * that allows us to overload a table reasonably if it cannot be grown further
 * (i.e. if ChangeTable() fails).  The table slows down drastically if the
 * secondary max is too close to 1, but 0.96875 gives only a slight slowdown
 * while allowing 1.3x more elements.
 */
static inline uint32_t
MaxLoad(uint32_t aCapacity)
{
  return aCapacity - (aCapacity >> 2);  // == aCapacity * 0.75
}
static inline uint32_t
MaxLoadOnGrowthFailure(uint32_t aCapacity)
{
  return aCapacity - (aCapacity >> 5);  // == aCapacity * 0.96875
}
static inline uint32_t
MinLoad(uint32_t aCapacity)
{
  return aCapacity >> 2;                // == aCapacity * 0.25
}

static inline uint32_t
MinCapacity(uint32_t aLength)
{
  return (aLength * 4 + (3 - 1)) / 3;   // == ceil(aLength * 4 / 3)
}

MOZ_ALWAYS_INLINE bool
PLDHashTable::Init(const PLDHashTableOps* aOps, void* aData,
                   uint32_t aEntrySize, const fallible_t&, uint32_t aLength)
{
#ifdef DEBUG
  if (aEntrySize > 16 * sizeof(void*)) {
    printf_stderr(
      "pldhash: for the table at address %p, the given aEntrySize"
      " of %lu definitely favors chaining over double hashing.\n",
      (void*)this,
      (unsigned long) aEntrySize);
  }
#endif

  if (aLength > PL_DHASH_MAX_INITIAL_LENGTH) {
    return false;
  }

  ops = aOps;
  data = aData;

  // Compute the smallest capacity allowing |aLength| elements to be inserted
  // without rehashing.
  uint32_t capacity = MinCapacity(aLength);
  if (capacity < PL_DHASH_MIN_CAPACITY) {
    capacity = PL_DHASH_MIN_CAPACITY;
  }

  int log2 = CeilingLog2(capacity);

  capacity = 1u << log2;
  MOZ_ASSERT(capacity <= PL_DHASH_MAX_CAPACITY);
  hashShift = PL_DHASH_BITS - log2;
  entrySize = aEntrySize;
  entryCount = removedCount = 0;
  generation = 0;
  uint32_t nbytes;
  if (!SizeOfEntryStore(capacity, aEntrySize, &nbytes)) {
    return false;  // overflowed
  }

  entryStore = (char*)aOps->allocTable(this, nbytes);
  if (!entryStore) {
    return false;
  }
  memset(entryStore, 0, nbytes);
  METER(memset(&stats, 0, sizeof(stats)));

#ifdef DEBUG
  recursionLevel = 0;
#endif

  return true;
}

bool
PL_DHashTableInit(PLDHashTable* aTable, const PLDHashTableOps* aOps,
                  void* aData, uint32_t aEntrySize,
                  const fallible_t& aFallible, uint32_t aLength)
{
  return aTable->Init(aOps, aData, aEntrySize, aFallible, aLength);
}

void
PL_DHashTableInit(PLDHashTable* aTable, const PLDHashTableOps* aOps,
                  void* aData, uint32_t aEntrySize, uint32_t aLength)
{
  if (!PL_DHashTableInit(aTable, aOps, aData, aEntrySize, fallible_t(),
                         aLength)) {
    if (aLength > PL_DHASH_MAX_INITIAL_LENGTH) {
      MOZ_CRASH();          // the asked-for length was too big
    }
    uint32_t capacity = MinCapacity(aLength), nbytes;
    if (!SizeOfEntryStore(capacity, aEntrySize, &nbytes)) {
      MOZ_CRASH();          // the required entryStore size was too big
    }
    NS_ABORT_OOM(nbytes);   // allocation failed
  }
}

/*
 * Double hashing needs the second hash code to be relatively prime to table
 * size, so we simply make hash2 odd.
 */
#define HASH1(hash0, shift)         ((hash0) >> (shift))
#define HASH2(hash0,log2,shift)     ((((hash0) << (log2)) >> (shift)) | 1)

/*
 * Reserve keyHash 0 for free entries and 1 for removed-entry sentinels.  Note
 * that a removed-entry sentinel need be stored only if the removed entry had
 * a colliding entry added after it.  Therefore we can use 1 as the collision
 * flag in addition to the removed-entry sentinel value.  Multiplicative hash
 * uses the high order bits of keyHash, so this least-significant reservation
 * should not hurt the hash function's effectiveness much.
 */
#define COLLISION_FLAG              ((PLDHashNumber) 1)
#define MARK_ENTRY_FREE(entry)      ((entry)->keyHash = 0)
#define MARK_ENTRY_REMOVED(entry)   ((entry)->keyHash = 1)
#define ENTRY_IS_REMOVED(entry)     ((entry)->keyHash == 1)
#define ENTRY_IS_LIVE(entry)        ((entry)->keyHash >= 2)
#define ENSURE_LIVE_KEYHASH(hash0)  if (hash0 < 2) hash0 -= 2; else (void)0

/* Match an entry's keyHash against an unstored one computed from a key. */
#define MATCH_ENTRY_KEYHASH(entry,hash0) \
    (((entry)->keyHash & ~COLLISION_FLAG) == (hash0))

/* Compute the address of the indexed entry in table. */
#define ADDRESS_ENTRY(table, index) \
    ((PLDHashEntryHdr *)((table)->entryStore + (index) * (table)->entrySize))

MOZ_ALWAYS_INLINE void
PLDHashTable::Finish()
{
  INCREMENT_RECURSION_LEVEL(this);

  /* Call finalize before clearing entries, so it can enumerate them. */
  ops->finalize(this);

  /* Clear any remaining live entries. */
  char* entryAddr = entryStore;
  char* entryLimit = entryAddr + Capacity() * entrySize;
  while (entryAddr < entryLimit) {
    PLDHashEntryHdr* entry = (PLDHashEntryHdr*)entryAddr;
    if (ENTRY_IS_LIVE(entry)) {
      METER(stats.removeEnums++);
      ops->clearEntry(this, entry);
    }
    entryAddr += entrySize;
  }

  DECREMENT_RECURSION_LEVEL(this);
  MOZ_ASSERT(RECURSION_LEVEL_SAFE_TO_FINISH(this));

  /* Free entry storage last. */
  ops->freeTable(this, entryStore);
}

void
PL_DHashTableFinish(PLDHashTable* aTable)
{
  aTable->Finish();
}

PLDHashEntryHdr* PL_DHASH_FASTCALL
PLDHashTable::SearchTable(const void* aKey, PLDHashNumber aKeyHash,
                          PLDHashOperator aOp)
{
  METER(stats.searches++);
  NS_ASSERTION(!(aKeyHash & COLLISION_FLAG),
               "!(aKeyHash & COLLISION_FLAG)");

  /* Compute the primary hash address. */
  PLDHashNumber hash1 = HASH1(aKeyHash, hashShift);
  PLDHashEntryHdr* entry = ADDRESS_ENTRY(this, hash1);

  /* Miss: return space for a new entry. */
  if (PL_DHASH_ENTRY_IS_FREE(entry)) {
    METER(stats.misses++);
    return entry;
  }

  /* Hit: return entry. */
  PLDHashMatchEntry matchEntry = ops->matchEntry;
  if (MATCH_ENTRY_KEYHASH(entry, aKeyHash) &&
      matchEntry(this, entry, aKey)) {
    METER(stats.hits++);
    return entry;
  }

  /* Collision: double hash. */
  int sizeLog2 = PL_DHASH_BITS - hashShift;
  PLDHashNumber hash2 = HASH2(aKeyHash, sizeLog2, hashShift);
  uint32_t sizeMask = (1u << sizeLog2) - 1;

  /* Save the first removed entry pointer so PL_DHASH_ADD can recycle it. */
  PLDHashEntryHdr* firstRemoved = nullptr;

  for (;;) {
    if (MOZ_UNLIKELY(ENTRY_IS_REMOVED(entry))) {
      if (!firstRemoved) {
        firstRemoved = entry;
      }
    } else {
      if (aOp == PL_DHASH_ADD) {
        entry->keyHash |= COLLISION_FLAG;
      }
    }

    METER(stats.steps++);
    hash1 -= hash2;
    hash1 &= sizeMask;

    entry = ADDRESS_ENTRY(this, hash1);
    if (PL_DHASH_ENTRY_IS_FREE(entry)) {
      METER(stats.misses++);
      return (firstRemoved && aOp == PL_DHASH_ADD) ? firstRemoved : entry;
    }

    if (MATCH_ENTRY_KEYHASH(entry, aKeyHash) &&
        matchEntry(this, entry, aKey)) {
      METER(stats.hits++);
      return entry;
    }
  }

  /* NOTREACHED */
  return nullptr;
}

/*
 * This is a copy of SearchTable, used by ChangeTable, hardcoded to
 *   1. assume |aOp == PL_DHASH_ADD|,
 *   2. assume that |aKey| will never match an existing entry, and
 *   3. assume that no entries have been removed from the current table
 *      structure.
 * Avoiding the need for |aKey| means we can avoid needing a way to map
 * entries to keys, which means callers can use complex key types more
 * easily.
 */
PLDHashEntryHdr* PL_DHASH_FASTCALL
PLDHashTable::FindFreeEntry(PLDHashNumber aKeyHash)
{
  METER(stats.searches++);
  NS_ASSERTION(!(aKeyHash & COLLISION_FLAG),
               "!(aKeyHash & COLLISION_FLAG)");

  /* Compute the primary hash address. */
  PLDHashNumber hash1 = HASH1(aKeyHash, hashShift);
  PLDHashEntryHdr* entry = ADDRESS_ENTRY(this, hash1);

  /* Miss: return space for a new entry. */
  if (PL_DHASH_ENTRY_IS_FREE(entry)) {
    METER(stats.misses++);
    return entry;
  }

  /* Collision: double hash. */
  int sizeLog2 = PL_DHASH_BITS - hashShift;
  PLDHashNumber hash2 = HASH2(aKeyHash, sizeLog2, hashShift);
  uint32_t sizeMask = (1u << sizeLog2) - 1;

  for (;;) {
    NS_ASSERTION(!ENTRY_IS_REMOVED(entry),
                 "!ENTRY_IS_REMOVED(entry)");
    entry->keyHash |= COLLISION_FLAG;

    METER(stats.steps++);
    hash1 -= hash2;
    hash1 &= sizeMask;

    entry = ADDRESS_ENTRY(this, hash1);
    if (PL_DHASH_ENTRY_IS_FREE(entry)) {
      METER(stats.misses++);
      return entry;
    }
  }

  /* NOTREACHED */
  return nullptr;
}

bool
PLDHashTable::ChangeTable(int aDeltaLog2)
{
  /* Look, but don't touch, until we succeed in getting new entry store. */
  int oldLog2 = PL_DHASH_BITS - hashShift;
  int newLog2 = oldLog2 + aDeltaLog2;
  uint32_t newCapacity = 1u << newLog2;
  if (newCapacity > PL_DHASH_MAX_CAPACITY) {
    return false;
  }

  uint32_t nbytes;
  if (!SizeOfEntryStore(newCapacity, entrySize, &nbytes)) {
    return false;   // overflowed
  }

  char* newEntryStore = (char*)ops->allocTable(this, nbytes);
  if (!newEntryStore) {
    return false;
  }

  /* We can't fail from here on, so update table parameters. */
#ifdef DEBUG
  uint32_t recursionLevelTmp = recursionLevel;
#endif
  hashShift = PL_DHASH_BITS - newLog2;
  removedCount = 0;
  generation++;

  /* Assign the new entry store to table. */
  memset(newEntryStore, 0, nbytes);
  char* oldEntryStore;
  char* oldEntryAddr;
  oldEntryAddr = oldEntryStore = entryStore;
  entryStore = newEntryStore;
  PLDHashMoveEntry moveEntry = ops->moveEntry;
#ifdef DEBUG
  recursionLevel = recursionLevelTmp;
#endif

  /* Copy only live entries, leaving removed ones behind. */
  uint32_t oldCapacity = 1u << oldLog2;
  for (uint32_t i = 0; i < oldCapacity; ++i) {
    PLDHashEntryHdr* oldEntry = (PLDHashEntryHdr*)oldEntryAddr;
    if (ENTRY_IS_LIVE(oldEntry)) {
      oldEntry->keyHash &= ~COLLISION_FLAG;
      PLDHashEntryHdr* newEntry = FindFreeEntry(oldEntry->keyHash);
      NS_ASSERTION(PL_DHASH_ENTRY_IS_FREE(newEntry),
                   "PL_DHASH_ENTRY_IS_FREE(newEntry)");
      moveEntry(this, oldEntry, newEntry);
      newEntry->keyHash = oldEntry->keyHash;
    }
    oldEntryAddr += entrySize;
  }

  ops->freeTable(this, oldEntryStore);
  return true;
}

MOZ_ALWAYS_INLINE PLDHashEntryHdr*
PLDHashTable::Operate(const void* aKey, PLDHashOperator aOp)
{
  PLDHashEntryHdr* entry;

  MOZ_ASSERT(aOp == PL_DHASH_LOOKUP || recursionLevel == 0);
  INCREMENT_RECURSION_LEVEL(this);

  PLDHashNumber keyHash = ops->hashKey(this, aKey);
  keyHash *= PL_DHASH_GOLDEN_RATIO;

  /* Avoid 0 and 1 hash codes, they indicate free and removed entries. */
  ENSURE_LIVE_KEYHASH(keyHash);
  keyHash &= ~COLLISION_FLAG;

  switch (aOp) {
    case PL_DHASH_LOOKUP:
      METER(stats.lookups++);
      entry = SearchTable(aKey, keyHash, aOp);
      break;

    case PL_DHASH_ADD: {
      /*
       * If alpha is >= .75, grow or compress the table.  If aKey is already
       * in the table, we may grow once more than necessary, but only if we
       * are on the edge of being overloaded.
       */
      uint32_t capacity = Capacity();
      if (entryCount + removedCount >= MaxLoad(capacity)) {
        /* Compress if a quarter or more of all entries are removed. */
        int deltaLog2;
        if (removedCount >= capacity >> 2) {
          METER(stats.compresses++);
          deltaLog2 = 0;
        } else {
          METER(stats.grows++);
          deltaLog2 = 1;
        }

        /*
         * Grow or compress the table.  If ChangeTable() fails, allow
         * overloading up to the secondary max.  Once we hit the secondary
         * max, return null.
         */
        if (!ChangeTable(deltaLog2) &&
            entryCount + removedCount >=
            MaxLoadOnGrowthFailure(capacity)) {
          METER(stats.addFailures++);
          entry = nullptr;
          break;
        }
      }

      /*
       * Look for entry after possibly growing, so we don't have to add it,
       * then skip it while growing the table and re-add it after.
       */
      entry = SearchTable(aKey, keyHash, aOp);
      if (!ENTRY_IS_LIVE(entry)) {
        /* Initialize the entry, indicating that it's no longer free. */
        METER(stats.addMisses++);
        if (ENTRY_IS_REMOVED(entry)) {
          METER(stats.addOverRemoved++);
          removedCount--;
          keyHash |= COLLISION_FLAG;
        }
        if (ops->initEntry && !ops->initEntry(this, entry, aKey)) {
          /* We haven't claimed entry yet; fail with null return. */
          memset(entry + 1, 0, entrySize - sizeof(*entry));
          entry = nullptr;
          break;
        }
        entry->keyHash = keyHash;
        entryCount++;
      }
      METER(else {
        stats.addHits++;
      });
      break;
    }

    case PL_DHASH_REMOVE:
      entry = SearchTable(aKey, keyHash, aOp);
      if (ENTRY_IS_LIVE(entry)) {
        /* Clear this entry and mark it as "removed". */
        METER(stats.removeHits++);
        PL_DHashTableRawRemove(this, entry);

        /* Shrink if alpha is <= .25 and the table isn't too small already. */
        uint32_t capacity = Capacity();
        if (capacity > PL_DHASH_MIN_CAPACITY &&
            entryCount <= MinLoad(capacity)) {
          METER(stats.shrinks++);
          (void) ChangeTable(-1);
        }
      }
      METER(else {
        stats.removeMisses++;
      });
      entry = nullptr;
      break;

    default:
      NS_NOTREACHED("0");
      entry = nullptr;
  }

  DECREMENT_RECURSION_LEVEL(this);

  return entry;
}

PLDHashEntryHdr* PL_DHASH_FASTCALL
PL_DHashTableOperate(PLDHashTable* aTable, const void* aKey, PLDHashOperator aOp)
{
  return aTable->Operate(aKey, aOp);
}

MOZ_ALWAYS_INLINE void
PLDHashTable::RawRemove(PLDHashEntryHdr* aEntry)
{
  MOZ_ASSERT(recursionLevel != IMMUTABLE_RECURSION_LEVEL);

  NS_ASSERTION(ENTRY_IS_LIVE(aEntry), "ENTRY_IS_LIVE(aEntry)");

  /* Load keyHash first in case clearEntry() goofs it. */
  PLDHashNumber keyHash = aEntry->keyHash;
  ops->clearEntry(this, aEntry);
  if (keyHash & COLLISION_FLAG) {
    MARK_ENTRY_REMOVED(aEntry);
    removedCount++;
  } else {
    METER(stats.removeFrees++);
    MARK_ENTRY_FREE(aEntry);
  }
  entryCount--;
}

void
PL_DHashTableRawRemove(PLDHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  aTable->RawRemove(aEntry);
}

MOZ_ALWAYS_INLINE uint32_t
PLDHashTable::Enumerate(PLDHashEnumerator aEtor, void* aArg)
{
  INCREMENT_RECURSION_LEVEL(this);

  char* entryAddr = entryStore;
  uint32_t capacity = Capacity();
  uint32_t tableSize = capacity * entrySize;
  char* entryLimit = entryAddr + tableSize;
  uint32_t i = 0;
  bool didRemove = false;

  if (ChaosMode::isActive()) {
    // Start iterating at a random point in the hashtable. It would be
    // even more chaotic to iterate in fully random order, but that's a lot
    // more work.
    entryAddr += ChaosMode::randomUint32LessThan(capacity) * entrySize;
    if (entryAddr >= entryLimit) {
      entryAddr -= tableSize;
    }
  }

  for (uint32_t e = 0; e < capacity; ++e) {
    PLDHashEntryHdr* entry = (PLDHashEntryHdr*)entryAddr;
    if (ENTRY_IS_LIVE(entry)) {
      PLDHashOperator op = aEtor(this, entry, i++, aArg);
      if (op & PL_DHASH_REMOVE) {
        METER(stats.removeEnums++);
        PL_DHashTableRawRemove(this, entry);
        didRemove = true;
      }
      if (op & PL_DHASH_STOP) {
        break;
      }
    }
    entryAddr += entrySize;
    if (entryAddr >= entryLimit) {
      entryAddr -= tableSize;
    }
  }

  MOZ_ASSERT(!didRemove || recursionLevel == 1);

  /*
   * Shrink or compress if a quarter or more of all entries are removed, or
   * if the table is underloaded according to the minimum alpha, and is not
   * minimal-size already.  Do this only if we removed above, so non-removing
   * enumerations can count on stable |entryStore| until the next
   * non-lookup-Operate or removing-Enumerate.
   */
  if (didRemove &&
      (removedCount >= capacity >> 2 ||
       (capacity > PL_DHASH_MIN_CAPACITY &&
        entryCount <= MinLoad(capacity)))) {
    METER(stats.enumShrinks++);
    capacity = entryCount;
    capacity += capacity >> 1;
    if (capacity < PL_DHASH_MIN_CAPACITY) {
      capacity = PL_DHASH_MIN_CAPACITY;
    }

    uint32_t ceiling = CeilingLog2(capacity);
    ceiling -= PL_DHASH_BITS - hashShift;

    (void) ChangeTable(ceiling);
  }

  DECREMENT_RECURSION_LEVEL(this);

  return i;
}

uint32_t
PL_DHashTableEnumerate(PLDHashTable* aTable, PLDHashEnumerator aEtor,
                       void* aArg)
{
  return aTable->Enumerate(aEtor, aArg);
}

struct SizeOfEntryExcludingThisArg
{
  size_t total;
  PLDHashSizeOfEntryExcludingThisFun sizeOfEntryExcludingThis;
  MallocSizeOf mallocSizeOf;
  void* arg;  // the arg passed by the user
};

static PLDHashOperator
SizeOfEntryExcludingThisEnumerator(PLDHashTable* aTable, PLDHashEntryHdr* aHdr,
                                   uint32_t aNumber, void* aArg)
{
  SizeOfEntryExcludingThisArg* e = (SizeOfEntryExcludingThisArg*)aArg;
  e->total += e->sizeOfEntryExcludingThis(aHdr, e->mallocSizeOf, e->arg);
  return PL_DHASH_NEXT;
}

MOZ_ALWAYS_INLINE size_t
PLDHashTable::SizeOfExcludingThis(
    PLDHashSizeOfEntryExcludingThisFun aSizeOfEntryExcludingThis,
    MallocSizeOf aMallocSizeOf, void* aArg /* = nullptr */) const
{
  size_t n = 0;
  n += aMallocSizeOf(entryStore);
  if (aSizeOfEntryExcludingThis) {
    SizeOfEntryExcludingThisArg arg2 = {
      0, aSizeOfEntryExcludingThis, aMallocSizeOf, aArg
    };
    PL_DHashTableEnumerate(const_cast<PLDHashTable*>(this),
                           SizeOfEntryExcludingThisEnumerator, &arg2);
    n += arg2.total;
  }
  return n;
}

MOZ_ALWAYS_INLINE size_t
PLDHashTable::SizeOfIncludingThis(
    PLDHashSizeOfEntryExcludingThisFun aSizeOfEntryExcludingThis,
    MallocSizeOf aMallocSizeOf, void* aArg /* = nullptr */) const
{
  return aMallocSizeOf(this) +
         SizeOfExcludingThis(aSizeOfEntryExcludingThis, aMallocSizeOf, aArg);
}

size_t
PL_DHashTableSizeOfExcludingThis(
    const PLDHashTable* aTable,
    PLDHashSizeOfEntryExcludingThisFun aSizeOfEntryExcludingThis,
    MallocSizeOf aMallocSizeOf, void* aArg /* = nullptr */)
{
  return aTable->SizeOfExcludingThis(aSizeOfEntryExcludingThis,
                                     aMallocSizeOf, aArg);
}

size_t
PL_DHashTableSizeOfIncludingThis(
    const PLDHashTable* aTable,
    PLDHashSizeOfEntryExcludingThisFun aSizeOfEntryExcludingThis,
    MallocSizeOf aMallocSizeOf, void* aArg /* = nullptr */)
{
  return aTable->SizeOfIncludingThis(aSizeOfEntryExcludingThis,
                                     aMallocSizeOf, aArg);
}

#ifdef DEBUG
MOZ_ALWAYS_INLINE void
PLDHashTable::MarkImmutable()
{
  recursionLevel = IMMUTABLE_RECURSION_LEVEL;
}

void
PL_DHashMarkTableImmutable(PLDHashTable* aTable)
{
  aTable->MarkImmutable();
}
#endif

#ifdef PL_DHASHMETER
#include <math.h>

void
PLDHashTable::DumpMeter(PLDHashEnumerator aDump, FILE* aFp)
{
  PLDHashNumber hash1, hash2, maxChainHash1, maxChainHash2;
  double sqsum, mean, variance, sigma;
  PLDHashEntryHdr* entry;

  char* entryAddr = entryStore;
  int sizeLog2 = PL_DHASH_BITS - hashShift;
  uint32_t capacity = Capacity();
  uint32_t sizeMask = (1u << sizeLog2) - 1;
  uint32_t chainCount = 0, maxChainLen = 0;
  hash2 = 0;
  sqsum = 0;

  for (uint32_t i = 0; i < capacity; i++) {
    entry = (PLDHashEntryHdr*)entryAddr;
    entryAddr += entrySize;
    if (!ENTRY_IS_LIVE(entry)) {
      continue;
    }
    hash1 = HASH1(entry->keyHash & ~COLLISION_FLAG, hashShift);
    PLDHashNumber saveHash1 = hash1;
    PLDHashEntryHdr* probe = ADDRESS_ENTRY(this, hash1);
    uint32_t chainLen = 1;
    if (probe == entry) {
      /* Start of a (possibly unit-length) chain. */
      chainCount++;
    } else {
      hash2 = HASH2(entry->keyHash & ~COLLISION_FLAG, sizeLog2,
                    hashShift);
      do {
        chainLen++;
        hash1 -= hash2;
        hash1 &= sizeMask;
        probe = ADDRESS_ENTRY(this, hash1);
      } while (probe != entry);
    }
    sqsum += chainLen * chainLen;
    if (chainLen > maxChainLen) {
      maxChainLen = chainLen;
      maxChainHash1 = saveHash1;
      maxChainHash2 = hash2;
    }
  }

  if (entryCount && chainCount) {
    mean = (double)entryCount / chainCount;
    variance = chainCount * sqsum - entryCount * entryCount;
    if (variance < 0 || chainCount == 1) {
      variance = 0;
    } else {
      variance /= chainCount * (chainCount - 1);
    }
    sigma = sqrt(variance);
  } else {
    mean = sigma = 0;
  }

  fprintf(aFp, "Double hashing statistics:\n");
  fprintf(aFp, "    table size (in entries): %u\n", tableSize);
  fprintf(aFp, "          number of entries: %u\n", entryCount);
  fprintf(aFp, "  number of removed entries: %u\n", removedCount);
  fprintf(aFp, "         number of searches: %u\n", stats.searches);
  fprintf(aFp, "             number of hits: %u\n", stats.hits);
  fprintf(aFp, "           number of misses: %u\n", stats.misses);
  fprintf(aFp, "      mean steps per search: %g\n",
          stats.searches ? (double)stats.steps / stats.searches : 0.);
  fprintf(aFp, "     mean hash chain length: %g\n", mean);
  fprintf(aFp, "         standard deviation: %g\n", sigma);
  fprintf(aFp, "  maximum hash chain length: %u\n", maxChainLen);
  fprintf(aFp, "          number of lookups: %u\n", stats.lookups);
  fprintf(aFp, " adds that made a new entry: %u\n", stats.addMisses);
  fprintf(aFp, "adds that recycled removeds: %u\n", stats.addOverRemoved);
  fprintf(aFp, "   adds that found an entry: %u\n", stats.addHits);
  fprintf(aFp, "               add failures: %u\n", stats.addFailures);
  fprintf(aFp, "             useful removes: %u\n", stats.removeHits);
  fprintf(aFp, "            useless removes: %u\n", stats.removeMisses);
  fprintf(aFp, "removes that freed an entry: %u\n", stats.removeFrees);
  fprintf(aFp, "  removes while enumerating: %u\n", stats.removeEnums);
  fprintf(aFp, "            number of grows: %u\n", stats.grows);
  fprintf(aFp, "          number of shrinks: %u\n", stats.shrinks);
  fprintf(aFp, "       number of compresses: %u\n", stats.compresses);
  fprintf(aFp, "number of enumerate shrinks: %u\n", stats.enumShrinks);

  if (aDump && maxChainLen && hash2) {
    fputs("Maximum hash chain:\n", aFp);
    hash1 = maxChainHash1;
    hash2 = maxChainHash2;
    entry = ADDRESS_ENTRY(this, hash1);
    uint32_t i = 0;
    do {
      if (aDump(this, entry, i++, aFp) != PL_DHASH_NEXT) {
        break;
      }
      hash1 -= hash2;
      hash1 &= sizeMask;
      entry = ADDRESS_ENTRY(this, hash1);
    } while (PL_DHASH_ENTRY_IS_BUSY(entry));
  }
}

void
PL_DHashTableDumpMeter(PLDHashTable* aTable, PLDHashEnumerator aDump, FILE* aFp)
{
  aTable->DumpMeter(aDump, aFp);
}
#endif /* PL_DHASHMETER */
