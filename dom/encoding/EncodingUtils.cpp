/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/EncodingUtils.h"

#include "mozilla/ArrayUtils.h" // ArrayLength
#include "nsUConvPropertySearch.h"
#include "nsIUnicodeDecoder.h"
#include "nsIUnicodeEncoder.h"
#include "nsComponentManagerUtils.h"

namespace mozilla {
namespace dom {

static const char* labelsEncodings[][3] = {
#include "labelsencodings.properties.h"
};

bool
EncodingUtils::FindEncodingForLabel(const nsACString& aLabel,
                                    nsACString& aOutEncoding)
{
  // Save aLabel first because it may refer the same string as aOutEncoding.
  nsCString label(aLabel);

  EncodingUtils::TrimSpaceCharacters(label);
  if (label.IsEmpty()) {
    aOutEncoding.Truncate();
    return false;
  }

  ToLowerCase(label);
  return NS_SUCCEEDED(nsUConvPropertySearch::SearchPropertyValue(
      labelsEncodings, ArrayLength(labelsEncodings), label, aOutEncoding));
}

bool
EncodingUtils::IsAsciiCompatible(const nsACString& aPreferredName)
{
  return !(aPreferredName.LowerCaseEqualsLiteral("utf-16") ||
           aPreferredName.LowerCaseEqualsLiteral("utf-16be") ||
           aPreferredName.LowerCaseEqualsLiteral("utf-16le") ||
           aPreferredName.LowerCaseEqualsLiteral("replacement") ||
           aPreferredName.LowerCaseEqualsLiteral("hz-gb-2312") ||
           aPreferredName.LowerCaseEqualsLiteral("utf-7") ||
           aPreferredName.LowerCaseEqualsLiteral("x-imap4-modified-utf7"));
}

already_AddRefed<nsIUnicodeDecoder>
EncodingUtils::DecoderForEncoding(const nsACString& aEncoding)
{
  nsAutoCString contractId(NS_UNICODEDECODER_CONTRACTID_BASE);
  contractId.Append(aEncoding);

  nsCOMPtr<nsIUnicodeDecoder> decoder = do_CreateInstance(contractId.get());
  MOZ_ASSERT(decoder, "Tried to create decoder for unknown encoding.");
  return decoder.forget();
}

already_AddRefed<nsIUnicodeEncoder>
EncodingUtils::EncoderForEncoding(const nsACString& aEncoding)
{
  nsAutoCString contractId(NS_UNICODEENCODER_CONTRACTID_BASE);
  contractId.Append(aEncoding);

  nsCOMPtr<nsIUnicodeEncoder> encoder = do_CreateInstance(contractId.get());
  MOZ_ASSERT(encoder, "Tried to create encoder for unknown encoding.");
  return encoder.forget();
}

} // namespace dom
} // namespace mozilla
