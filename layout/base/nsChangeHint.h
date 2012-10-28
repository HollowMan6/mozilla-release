/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* constants for what needs to be recomputed in response to style changes */

#ifndef nsChangeHint_h___
#define nsChangeHint_h___

#include "nsDebug.h"

// Defines for various style related constants

enum nsChangeHint {
  // change was visual only (e.g., COLOR=)
  // Invalidates all descendant frames (including following
  // placeholders to out-of-flow frames).
  nsChangeHint_RepaintFrame = 0x01,

  // For reflow, we want flags to give us arbitrary FrameNeedsReflow behavior.
  // just do a FrameNeedsReflow.
  nsChangeHint_NeedReflow = 0x02,

  // Invalidate intrinsic widths on the frame's ancestors.  Must not be set
  // without setting nsChangeHint_NeedReflow.
  nsChangeHint_ClearAncestorIntrinsics = 0x04,

  // Invalidate intrinsic widths on the frame's descendants.  Must not be set
  // without also setting nsChangeHint_ClearAncestorIntrinsics.
  nsChangeHint_ClearDescendantIntrinsics = 0x08,

  // Force unconditional reflow of all descendants.  Must not be set without
  // setting nsChangeHint_NeedReflow, but is independent of both the
  // Clear*Intrinsics flags.
  nsChangeHint_NeedDirtyReflow = 0x10,

  // change requires view to be updated, if there is one (e.g., clip:).
  // Updates all descendants (including following placeholders to out-of-flows).
  nsChangeHint_SyncFrameView = 0x20,

  // The currently shown mouse cursor needs to be updated
  nsChangeHint_UpdateCursor = 0x40,

  /**
   * SVG filter/mask/clip effects need to be recomputed because the URI
   * in the filter/mask/clip-path property has changed. This wipes
   * out cached nsSVGPropertyBase and subclasses which hold a reference to
   * the element referenced by the URI, and a mutation observer for
   * the DOM subtree rooted at that element. Also, for filters they store a
   * bounding-box for the filter result so that if the filter changes we can
   * invalidate the old covered area.
   */
  nsChangeHint_UpdateEffects = 0x80,

  /**
   * Visual change only, but the change can be handled entirely by
   * updating the layer(s) for the frame.
   * Updates all descendants (including following placeholders to out-of-flows).
   */
  nsChangeHint_UpdateOpacityLayer = 0x100,
  /**
   * Updates all descendants. Any placeholder descendants' out-of-flows
   * are also descendants of the transformed frame, so they're updated.
   */
  nsChangeHint_UpdateTransformLayer = 0x200,

  /**
   * Change requires frame change (e.g., display:).
   * This subsumes all the above. Reconstructs all frame descendants,
   * including following placeholders to out-of-flows.
   */
  nsChangeHint_ReconstructFrame = 0x400,

  /**
   * The frame's effect on its ancestors' overflow areas has changed,
   * either through a change in its transform or a change in its position.
   * Does not update any descendant frames.
   */
  nsChangeHint_UpdateOverflow = 0x800,

  /**
   * The children-only transform of an SVG frame changed, requiring the
   * overflow rects of the frame's immediate children to be updated.
   */
  nsChangeHint_ChildrenOnlyTransform = 0x1000,

  /**
   * This change hint has *no* change handling behavior.  However, it
   * exists to be a non-inherited hint, because when the border-style
   * changes, and it's inherited by a child, that might require a reflow
   * due to the border-width change on the child.
   */
  nsChangeHint_BorderStyleNoneChange = 0x2000

  // IMPORTANT NOTE: When adding new hints, consider whether you need to
  // add them to NS_HintsNotHandledForDescendantsIn() below.
};

// Redefine these operators to return nothing. This will catch any use
// of these operators on hints. We should not be using these operators
// on nsChangeHints
inline void operator<(nsChangeHint s1, nsChangeHint s2) {}
inline void operator>(nsChangeHint s1, nsChangeHint s2) {}
inline void operator!=(nsChangeHint s1, nsChangeHint s2) {}
inline void operator==(nsChangeHint s1, nsChangeHint s2) {}
inline void operator<=(nsChangeHint s1, nsChangeHint s2) {}
inline void operator>=(nsChangeHint s1, nsChangeHint s2) {}

// Operators on nsChangeHints

// Merge two hints, taking the union
inline nsChangeHint NS_CombineHint(nsChangeHint aH1, nsChangeHint aH2) {
  return (nsChangeHint)(aH1 | aH2);
}

// Merge two hints, taking the union
inline nsChangeHint NS_SubtractHint(nsChangeHint aH1, nsChangeHint aH2) {
  return (nsChangeHint)(aH1 & ~aH2);
}

// Merge the "src" hint into the "dst" hint
// Returns true iff the destination changed
inline bool NS_UpdateHint(nsChangeHint& aDest, nsChangeHint aSrc) {
  nsChangeHint r = (nsChangeHint)(aDest | aSrc);
  bool changed = (int)r != (int)aDest;
  aDest = r;
  return changed;
}

// Returns true iff the second hint contains all the hints of the first hint
inline bool NS_IsHintSubset(nsChangeHint aSubset, nsChangeHint aSuperSet) {
  return (aSubset & aSuperSet) == aSubset;
}

/**
 * We have an optimization when processing change hints which prevents
 * us from visiting the descendants of a node when a hint on that node
 * is being processed.  This optimization does not apply in some of the
 * cases where applying a hint to an element does not necessarily result
 * in the same hint being handled on the descendants.
 */

// The most hints that NS_HintsNotHandledForDescendantsIn could possibly return:
#define nsChangeHint_Hints_NotHandledForDescendants nsChangeHint( \
          nsChangeHint_UpdateTransformLayer | \
          nsChangeHint_UpdateEffects | \
          nsChangeHint_UpdateOpacityLayer | \
          nsChangeHint_UpdateOverflow | \
          nsChangeHint_ChildrenOnlyTransform | \
          nsChangeHint_BorderStyleNoneChange | \
          nsChangeHint_NeedReflow | \
          nsChangeHint_ClearAncestorIntrinsics)

inline nsChangeHint NS_HintsNotHandledForDescendantsIn(nsChangeHint aChangeHint) {
  nsChangeHint result = nsChangeHint(aChangeHint & (
    nsChangeHint_UpdateTransformLayer |
    nsChangeHint_UpdateEffects |
    nsChangeHint_UpdateOpacityLayer |
    nsChangeHint_UpdateOverflow |
    nsChangeHint_ChildrenOnlyTransform |
    nsChangeHint_BorderStyleNoneChange));

  if (!NS_IsHintSubset(nsChangeHint_NeedDirtyReflow, aChangeHint) &&
      NS_IsHintSubset(nsChangeHint_NeedReflow, aChangeHint)) {
    // If NeedDirtyReflow is *not* set, then NeedReflow is a
    // non-inherited hint.
    NS_UpdateHint(result, nsChangeHint_NeedReflow);
  }

  if (!NS_IsHintSubset(nsChangeHint_ClearDescendantIntrinsics, aChangeHint) &&
      NS_IsHintSubset(nsChangeHint_ClearAncestorIntrinsics, aChangeHint)) {
    // If ClearDescendantIntrinsics is *not* set, then
    // ClearAncestorIntrinsics is a non-inherited hint.
    NS_UpdateHint(result, nsChangeHint_ClearAncestorIntrinsics);
  }

  NS_ABORT_IF_FALSE(NS_IsHintSubset(result,
                                    nsChangeHint_Hints_NotHandledForDescendants),
                    "something is inconsistent");

  return result;
}

// Redefine the old NS_STYLE_HINT constants in terms of the new hint structure
#define NS_STYLE_HINT_NONE \
  nsChangeHint(0)
#define NS_STYLE_HINT_VISUAL \
  nsChangeHint(nsChangeHint_RepaintFrame | nsChangeHint_SyncFrameView)
#define nsChangeHint_AllReflowHints                     \
  nsChangeHint(nsChangeHint_NeedReflow |                \
               nsChangeHint_ClearAncestorIntrinsics |   \
               nsChangeHint_ClearDescendantIntrinsics | \
               nsChangeHint_NeedDirtyReflow)
#define NS_STYLE_HINT_REFLOW \
  nsChangeHint(NS_STYLE_HINT_VISUAL | nsChangeHint_AllReflowHints)
#define NS_STYLE_HINT_FRAMECHANGE \
  nsChangeHint(NS_STYLE_HINT_REFLOW | nsChangeHint_ReconstructFrame)

/**
 * |nsRestyleHint| is a bitfield for the result of
 * |HasStateDependentStyle| and |HasAttributeDependentStyle|.  When no
 * restyling is necessary, use |nsRestyleHint(0)|.
 */
enum nsRestyleHint {
  eRestyle_Self = 0x1,
  eRestyle_Subtree = 0x2, /* self and descendants */
  eRestyle_LaterSiblings = 0x4 /* implies "and descendants" */
};


#endif /* nsChangeHint_h___ */
