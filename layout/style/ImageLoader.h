/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// A class that handles style system image loads (other image loads are handled
// by the nodes in the content tree).

#include "nsAutoPtr.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsInterfaceHashtable.h"
#include "nsCSSValue.h"
#include "imgIRequest.h"
#include "imgIOnloadBlocker.h"
#include "imgINotificationObserver.h"
#include "mozilla/Attributes.h"

class nsIFrame;
class nsIDocument;
class nsPresContext;
class nsIURI;
class nsIPrincipal;

namespace mozilla {
namespace css {

class ImageLoader MOZ_FINAL : public imgINotificationObserver,
                              public imgIOnloadBlocker {
public:
  typedef mozilla::css::ImageValue Image;

  ImageLoader(nsIDocument* aDocument)
  : mDocument(aDocument),
    mInClone(false)
  {
    MOZ_ASSERT(mDocument);

    mRequestToFrameMap.Init();
    mFrameToRequestMap.Init();
    mImages.Init();
  }

  NS_DECL_ISUPPORTS
  NS_DECL_IMGIONLOADBLOCKER
  NS_DECL_IMGINOTIFICATIONOBSERVER

  void DropDocumentReference();

  void MaybeRegisterCSSImage(Image* aImage);
  void DeregisterCSSImage(Image* aImage);

  void AssociateRequestToFrame(imgIRequest* aRequest,
                               nsIFrame* aFrame);

  void DisassociateRequestFromFrame(imgIRequest* aRequest,
                                    nsIFrame* aFrame);

  void DropRequestsForFrame(nsIFrame* aFrame);

  void SetAnimationMode(uint16_t aMode);

  void ClearFrames();

  void LoadImage(nsIURI* aURI, nsIPrincipal* aPrincipal, nsIURI* aReferrer,
                 Image* aCSSValue);

  void DestroyRequest(imgIRequest* aRequest);

private:
  // We need to be able to look up the frames associated with a request (for
  // delivering notifications) and the requests associated with a frame (when
  // the frame goes away). Thus we maintain hashtables going both ways.  These
  // should always be in sync.

  typedef nsTArray<nsIFrame*> FrameSet;
  typedef nsTArray<nsCOMPtr<imgIRequest> > RequestSet;
  typedef nsTHashtable<nsPtrHashKey<Image> > ImageHashSet;
  typedef nsClassHashtable<nsISupportsHashKey,
                           FrameSet> RequestToFrameMap;
  typedef nsClassHashtable<nsPtrHashKey<nsIFrame>,
                           RequestSet> FrameToRequestMap;

  void AddImage(Image* aCSSImage);
  void RemoveImage(Image* aCSSImage);

  nsPresContext* GetPresContext();

  void DoRedraw(FrameSet* aFrameSet);

  static PLDHashOperator
  SetAnimationModeEnumerator(nsISupports* aKey, FrameSet* aValue,
                             void* aClosure);

  nsresult OnStartContainer(imgIRequest *aRequest, imgIContainer* aImage);
  nsresult OnStopFrame(imgIRequest *aRequest);
  nsresult OnImageIsAnimated(imgIRequest *aRequest);
  nsresult FrameChanged(imgIRequest* aRequest);
  // Do not override OnDataAvailable since background images are not
  // displayed incrementally; they are displayed after the entire image
  // has been loaded.

  // A map of imgIRequests to the nsIFrames that are using them.
  RequestToFrameMap mRequestToFrameMap;

  // A map of nsIFrames to the imgIRequests they use.
  FrameToRequestMap mFrameToRequestMap;

  // A weak pointer to our document. Nulled out by DropDocumentReference.
  nsIDocument* mDocument;

  // The set of all nsCSSValue::Images (whether they're associated a frame or
  // not).  We'll need this when we go away to remove any requests associated
  // with our document from those Images.
  ImageHashSet mImages;

  // Are we cloning?  If so, ignore any notifications we get.
  bool mInClone;
};

} // namespace css
} // namespace mozilla
