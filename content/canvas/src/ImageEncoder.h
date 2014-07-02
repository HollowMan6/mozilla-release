/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ImageEncoder_h
#define ImageEncoder_h

#include "imgIEncoder.h"
#include "nsDOMFile.h"
#include "nsError.h"
#include "mozilla/dom/HTMLCanvasElementBinding.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsSize.h"

class nsICanvasRenderingContextInternal;
class nsIGlobalObject;

namespace mozilla {
namespace dom {

class EncodingRunnable;

class ImageEncoder
{
public:
  // Extracts data synchronously and gives you a stream containing the image
  // represented by aContext. aType may change to "image/png" if we had to fall
  // back to a PNG encoder. A return value of NS_OK implies successful data
  // extraction. If there are any unrecognized custom parse options in
  // aOptions, NS_ERROR_INVALID_ARG will be returned. When encountering this
  // error it is usual to call this function again without any options at all.
  static nsresult ExtractData(nsAString& aType,
                              const nsAString& aOptions,
                              const nsIntSize aSize,
                              nsICanvasRenderingContextInternal* aContext,
                              nsIInputStream** aStream);

  // Extracts data asynchronously. aType may change to "image/png" if we had to
  // fall back to a PNG encoder. aOptions are the options to be passed to the
  // encoder and aUsingCustomOptions specifies whether custom parse options were
  // used (i.e. by using -moz-parse-options). If there are any unrecognized
  // custom parse options, we fall back to the default values for the encoder
  // without any options at all. A return value of NS_OK only implies
  // successful dispatching of the extraction step to the encoding thread.
  static nsresult ExtractDataAsync(nsAString& aType,
                                   const nsAString& aOptions,
                                   bool aUsingCustomOptions,
                                   uint8_t* aImageBuffer,
                                   int32_t aFormat,
                                   const nsIntSize aSize,
                                   nsICanvasRenderingContextInternal* aContext,
                                   nsIGlobalObject* aGlobal,
                                   FileCallback& aCallback);

  // Gives you a stream containing the image represented by aImageBuffer.
  // The format is given in aFormat, for example
  // imgIEncoder::INPUT_FORMAT_HOSTARGB.
  static nsresult GetInputStream(int32_t aWidth,
                                 int32_t aHeight,
                                 uint8_t* aImageBuffer,
                                 int32_t aFormat,
                                 imgIEncoder* aEncoder,
                                 const char16_t* aEncoderOptions,
                                 nsIInputStream** aStream);

private:
  // When called asynchronously, aContext is null.
  static nsresult
  ExtractDataInternal(const nsAString& aType,
                      const nsAString& aOptions,
                      uint8_t* aImageBuffer,
                      int32_t aFormat,
                      const nsIntSize aSize,
                      nsICanvasRenderingContextInternal* aContext,
                      nsIInputStream** aStream,
                      imgIEncoder* aEncoder);

  // Creates and returns an encoder instance of the type specified in aType.
  // aType may change to "image/png" if no instance of the original type could
  // be created and we had to fall back to a PNG encoder. A null return value
  // should be interpreted as NS_IMAGELIB_ERROR_NO_ENCODER and aType is
  // undefined in this case.
  static already_AddRefed<imgIEncoder> GetImageEncoder(nsAString& aType);

  friend class EncodingRunnable;
};

} // namespace dom
} // namespace mozilla

#endif // ImageEncoder_h
