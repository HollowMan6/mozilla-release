/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// Eric Vaughan
// Netscape Communications
//
// See documentation in associated header file
//

#include "nsDeckFrame.h"
#include "nsStyleContext.h"
#include "nsPresContext.h"
#include "nsIContent.h"
#include "nsCOMPtr.h"
#include "nsNameSpaceManager.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsIPresShell.h"
#include "nsCSSRendering.h"
#include "nsViewManager.h"
#include "nsBoxLayoutState.h"
#include "nsStackLayout.h"
#include "nsDisplayList.h"
#include "nsContainerFrame.h"

#ifdef ACCESSIBILITY
#include "nsAccessibilityService.h"
#endif

nsIFrame*
NS_NewDeckFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsDeckFrame(aPresShell, aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsDeckFrame)

NS_QUERYFRAME_HEAD(nsDeckFrame)
  NS_QUERYFRAME_ENTRY(nsDeckFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsBoxFrame)


nsDeckFrame::nsDeckFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
  : nsBoxFrame(aPresShell, aContext), mIndex(0)
{
  nsCOMPtr<nsBoxLayout> layout;
  NS_NewStackLayout(aPresShell, layout);
  SetLayoutManager(layout);
}

nsIAtom*
nsDeckFrame::GetType() const
{
  return nsGkAtoms::deckFrame;
}

nsresult
nsDeckFrame::AttributeChanged(int32_t         aNameSpaceID,
                              nsIAtom*        aAttribute,
                              int32_t         aModType)
{
  nsresult rv = nsBoxFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                             aModType);


   // if the index changed hide the old element and make the new element visible
  if (aAttribute == nsGkAtoms::selectedIndex) {
    IndexChanged();
  }

  return rv;
}

void
nsDeckFrame::Init(nsIContent*       aContent,
                  nsContainerFrame* aParent,
                  nsIFrame*         aPrevInFlow)
{
  nsBoxFrame::Init(aContent, aParent, aPrevInFlow);

  mIndex = GetSelectedIndex();
}

void
nsDeckFrame::HideBox(nsIFrame* aBox)
{
  nsIPresShell::ClearMouseCapture(aBox);
}

void
nsDeckFrame::IndexChanged()
{
  //did the index change?
  int32_t index = GetSelectedIndex();
  if (index == mIndex)
    return;

  // redraw
  InvalidateFrame();

  // hide the currently showing box
  nsIFrame* currentBox = GetSelectedBox();
  if (currentBox) // only hide if it exists
    HideBox(currentBox);

  mIndex = index;

#ifdef ACCESSIBILITY
  nsAccessibilityService* accService = GetAccService();
  if (accService) {
    accService->DeckPanelSwitched(PresContext()->GetPresShell(), mContent,
                                  currentBox, GetSelectedBox());
  }
#endif
}

int32_t
nsDeckFrame::GetSelectedIndex()
{
  // default index is 0
  int32_t index = 0;

  // get the index attribute
  nsAutoString value;
  if (mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::selectedIndex, value))
  {
    nsresult error;

    // convert it to an integer
    index = value.ToInteger(&error);
  }

  return index;
}

nsIFrame* 
nsDeckFrame::GetSelectedBox()
{
  return (mIndex >= 0) ? mFrames.FrameAt(mIndex) : nullptr; 
}

void
nsDeckFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists)
{
  // if a tab is hidden all its children are too.
  if (!StyleVisibility()->mVisible)
    return;
    
  nsBoxFrame::BuildDisplayList(aBuilder, aDirtyRect, aLists);
}

void
nsDeckFrame::BuildDisplayListForChildren(nsDisplayListBuilder*   aBuilder,
                                         const nsRect&           aDirtyRect,
                                         const nsDisplayListSet& aLists)
{
  // only paint the selected box
  nsIFrame* box = GetSelectedBox();
  if (!box)
    return;

  // Putting the child in the background list. This is a little weird but
  // it matches what we were doing before.
  nsDisplayListSet set(aLists, aLists.BlockBorderBackgrounds());
  BuildDisplayListForChild(aBuilder, box, aDirtyRect, set);
}

NS_IMETHODIMP
nsDeckFrame::DoLayout(nsBoxLayoutState& aState)
{
  // Make sure we tweak the state so it does not resize our children.
  // We will do that.
  uint32_t oldFlags = aState.LayoutFlags();
  aState.SetLayoutFlags(NS_FRAME_NO_SIZE_VIEW | NS_FRAME_NO_VISIBILITY);

  // do a normal layout
  nsresult rv = nsBoxFrame::DoLayout(aState);

  // run though each child. Hide all but the selected one
  nsIFrame* box = nsBox::GetChildBox(this);

  nscoord count = 0;
  while (box) 
  {
    // make collapsed children not show up
    if (count != mIndex) 
      HideBox(box);

    box = GetNextBox(box);
    count++;
  }

  aState.SetLayoutFlags(oldFlags);

  return rv;
}

