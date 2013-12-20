/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
// vim:cindent:ai:sw=4:ts=4:et:
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implementation of CSS counters (for numbering things) */

#include "nsCounterManager.h"
#include "nsBulletFrame.h" // legacy location for list style type to text code
#include "nsContentUtils.h"
#include "nsTArray.h"
#include "mozilla/Likely.h"
#include "nsIContent.h"

bool
nsCounterUseNode::InitTextFrame(nsGenConList* aList,
        nsIFrame* aPseudoFrame, nsIFrame* aTextFrame)
{
  nsCounterNode::InitTextFrame(aList, aPseudoFrame, aTextFrame);

  nsCounterList *counterList = static_cast<nsCounterList*>(aList);
  counterList->Insert(this);
  bool dirty = counterList->IsDirty();
  if (!dirty) {
    if (counterList->IsLast(this)) {
      Calc(counterList);
      nsAutoString contentString;
      GetText(contentString);
      aTextFrame->GetContent()->SetText(contentString, false);
    } else {
      // In all other cases (list already dirty or node not at the end),
      // just start with an empty string for now and when we recalculate
      // the list we'll change the value to the right one.
      counterList->SetDirty();
      return true;
    }
  }
  
  return false;
}

// assign the correct |mValueAfter| value to a node that has been inserted
// Should be called immediately after calling |Insert|.
void nsCounterUseNode::Calc(nsCounterList *aList)
{
    NS_ASSERTION(!aList->IsDirty(),
                 "Why are we calculating with a dirty list?");
    mValueAfter = aList->ValueBefore(this);
}

// assign the correct |mValueAfter| value to a node that has been inserted
// Should be called immediately after calling |Insert|.
void nsCounterChangeNode::Calc(nsCounterList *aList)
{
    NS_ASSERTION(!aList->IsDirty(),
                 "Why are we calculating with a dirty list?");
    if (mType == RESET) {
        mValueAfter = mChangeValue;
    } else {
        NS_ASSERTION(mType == INCREMENT, "invalid type");
        mValueAfter = nsCounterManager::IncrementCounter(aList->ValueBefore(this),
                                                         mChangeValue);
    }
}

// The text that should be displayed for this counter.
void
nsCounterUseNode::GetText(nsString& aResult)
{
    aResult.Truncate();

    nsAutoTArray<nsCounterNode*, 8> stack;
    stack.AppendElement(static_cast<nsCounterNode*>(this));

    if (mAllCounters && mScopeStart)
        for (nsCounterNode *n = mScopeStart; n->mScopePrev; n = n->mScopeStart)
            stack.AppendElement(n->mScopePrev);

    const nsCSSValue& styleItem = mCounterStyle->Item(mAllCounters ? 2 : 1);
    int32_t style = styleItem.GetIntValue();
    const PRUnichar* separator;
    if (mAllCounters)
        separator = mCounterStyle->Item(1).GetStringBufferValue();

    for (uint32_t i = stack.Length() - 1;; --i) {
        nsCounterNode *n = stack[i];
        bool isTextRTL;
        nsBulletFrame::AppendCounterText(
                style, n->mValueAfter, aResult, isTextRTL);
        if (i == 0)
            break;
        NS_ASSERTION(mAllCounters, "yikes, separator is uninitialized");
        aResult.Append(separator);
    }
}

void
nsCounterList::SetScope(nsCounterNode *aNode)
{
    // This function is responsible for setting |mScopeStart| and
    // |mScopePrev| (whose purpose is described in nsCounterManager.h).
    // We do this by starting from the node immediately preceding
    // |aNode| in content tree order, which is reasonably likely to be
    // the previous element in our scope (or, for a reset, the previous
    // element in the containing scope, which is what we want).  If
    // we're not in the same scope that it is, then it's too deep in the
    // frame tree, so we walk up parent scopes until we find something
    // appropriate.

    if (aNode == First()) {
        aNode->mScopeStart = nullptr;
        aNode->mScopePrev = nullptr;
        return;
    }

    // Get the content node for aNode's rendering object's *parent*,
    // since scope includes siblings, so we want a descendant check on
    // parents.
    nsIContent *nodeContent = aNode->mPseudoFrame->GetContent()->GetParent();

    for (nsCounterNode *prev = Prev(aNode), *start;
         prev; prev = start->mScopePrev) {
        // If |prev| starts a scope (because it's a real or implied
        // reset), we want it as the scope start rather than the start
        // of its enclosing scope.  Otherwise, there's no enclosing
        // scope, so the next thing in prev's scope shares its scope
        // start.
        start = (prev->mType == nsCounterNode::RESET || !prev->mScopeStart)
                  ? prev : prev->mScopeStart;

        // |startContent| is analogous to |nodeContent| (see above).
        nsIContent *startContent = start->mPseudoFrame->GetContent()->GetParent();
        NS_ASSERTION(nodeContent || !startContent,
                     "null check on startContent should be sufficient to "
                     "null check nodeContent as well, since if nodeContent "
                     "is for the root, startContent (which is before it) "
                     "must be too");

             // A reset's outer scope can't be a scope created by a sibling.
        if (!(aNode->mType == nsCounterNode::RESET &&
              nodeContent == startContent) &&
              // everything is inside the root (except the case above,
              // a second reset on the root)
            (!startContent ||
             nsContentUtils::ContentIsDescendantOf(nodeContent,
                                                   startContent))) {
            aNode->mScopeStart = start;
            aNode->mScopePrev  = prev;
            return;
        }
    }

    aNode->mScopeStart = nullptr;
    aNode->mScopePrev  = nullptr;
}

void
nsCounterList::RecalcAll()
{
    mDirty = false;

    nsCounterNode *node = First();
    if (!node)
        return;

    do {
        SetScope(node);
        node->Calc(this);

        if (node->mType == nsCounterNode::USE) {
            nsCounterUseNode *useNode = node->UseNode();
            // Null-check mText, since if the frame constructor isn't
            // batching, we could end up here while the node is being
            // constructed.
            if (useNode->mText) {
                nsAutoString text;
                useNode->GetText(text);
                useNode->mText->SetData(text);
            }
        }
    } while ((node = Next(node)) != First());
}

nsCounterManager::nsCounterManager()
    : mNames(16)
{
}

bool
nsCounterManager::AddCounterResetsAndIncrements(nsIFrame *aFrame)
{
    const nsStyleContent *styleContent = aFrame->StyleContent();
    if (!styleContent->CounterIncrementCount() &&
        !styleContent->CounterResetCount())
        return false;

    // Add in order, resets first, so all the comparisons will be optimized
    // for addition at the end of the list.
    int32_t i, i_end;
    bool dirty = false;
    for (i = 0, i_end = styleContent->CounterResetCount(); i != i_end; ++i)
        dirty |= AddResetOrIncrement(aFrame, i,
                                     styleContent->GetCounterResetAt(i),
                                     nsCounterChangeNode::RESET);
    for (i = 0, i_end = styleContent->CounterIncrementCount(); i != i_end; ++i)
        dirty |= AddResetOrIncrement(aFrame, i,
                                     styleContent->GetCounterIncrementAt(i),
                                     nsCounterChangeNode::INCREMENT);
    return dirty;
}

bool
nsCounterManager::AddResetOrIncrement(nsIFrame *aFrame, int32_t aIndex,
                                      const nsStyleCounterData *aCounterData,
                                      nsCounterNode::Type aType)
{
    nsCounterChangeNode *node =
        new nsCounterChangeNode(aFrame, aType, aCounterData->mValue, aIndex);

    nsCounterList *counterList = CounterListFor(aCounterData->mCounter);
    if (!counterList) {
        NS_NOTREACHED("CounterListFor failed (should only happen on OOM)");
        return false;
    }

    counterList->Insert(node);
    if (!counterList->IsLast(node)) {
        // Tell the caller it's responsible for recalculating the entire
        // list.
        counterList->SetDirty();
        return true;
    }

    // Don't call Calc() if the list is already dirty -- it'll be recalculated
    // anyway, and trying to calculate with a dirty list doesn't work.
    if (MOZ_LIKELY(!counterList->IsDirty())) {
        node->Calc(counterList);
    }
    return false;
}

nsCounterList*
nsCounterManager::CounterListFor(const nsSubstring& aCounterName)
{
    // XXX Why doesn't nsTHashtable provide an API that allows us to use
    // get/put in one hashtable lookup?
    nsCounterList *counterList;
    if (!mNames.Get(aCounterName, &counterList)) {
        counterList = new nsCounterList();
        mNames.Put(aCounterName, counterList);
    }
    return counterList;
}

static PLDHashOperator
RecalcDirtyLists(const nsAString& aKey, nsCounterList* aList, void* aClosure)
{
    if (aList->IsDirty())
        aList->RecalcAll();
    return PL_DHASH_NEXT;
}

void
nsCounterManager::RecalcAll()
{
    mNames.EnumerateRead(RecalcDirtyLists, nullptr);
}

struct DestroyNodesData {
    DestroyNodesData(nsIFrame *aFrame)
        : mFrame(aFrame)
        , mDestroyedAny(false)
    {
    }

    nsIFrame *mFrame;
    bool mDestroyedAny;
};

static PLDHashOperator
DestroyNodesInList(const nsAString& aKey, nsCounterList* aList, void* aClosure)
{
    DestroyNodesData *data = static_cast<DestroyNodesData*>(aClosure);
    if (aList->DestroyNodesFor(data->mFrame)) {
        data->mDestroyedAny = true;
        aList->SetDirty();
    }
    return PL_DHASH_NEXT;
}

bool
nsCounterManager::DestroyNodesFor(nsIFrame *aFrame)
{
    DestroyNodesData data(aFrame);
    mNames.EnumerateRead(DestroyNodesInList, &data);
    return data.mDestroyedAny;
}

#ifdef DEBUG
static PLDHashOperator
DumpList(const nsAString& aKey, nsCounterList* aList, void* aClosure)
{
    printf("Counter named \"%s\":\n", NS_ConvertUTF16toUTF8(aKey).get());
    nsCounterNode *node = aList->First();

    if (node) {
        int32_t i = 0;
        do {
            const char *types[] = { "RESET", "INCREMENT", "USE" };
            printf("  Node #%d @%p frame=%p index=%d type=%s valAfter=%d\n"
                   "       scope-start=%p scope-prev=%p",
                   i++, (void*)node, (void*)node->mPseudoFrame,
                   node->mContentIndex, types[node->mType], node->mValueAfter,
                   (void*)node->mScopeStart, (void*)node->mScopePrev);
            if (node->mType == nsCounterNode::USE) {
                nsAutoString text;
                node->UseNode()->GetText(text);
                printf(" text=%s", NS_ConvertUTF16toUTF8(text).get());
            }
            printf("\n");
        } while ((node = aList->Next(node)) != aList->First());
    }
    return PL_DHASH_NEXT;
}

void
nsCounterManager::Dump()
{
    printf("\n\nCounter Manager Lists:\n");
    mNames.EnumerateRead(DumpList, nullptr);
    printf("\n\n");
}
#endif
