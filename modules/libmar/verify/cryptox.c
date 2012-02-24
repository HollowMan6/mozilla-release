/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is cryptographic wrappers for Mozilla archive code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian R. Bondy <netzen@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifdef XP_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <stdlib.h>
#include "cryptox.h"

#if defined(MAR_NSS)

/** 
 * Loads the public key for the specified cert name from the NSS store.
 * 
 * @param certName The cert name to find.
 * @param publicKey Out parameter for the public key to use.
 * @param cert      Out parameter for the certificate to use.
 * @return CryptoX_Success on success, CryptoX_Error on error.
*/
CryptoX_Result
NSS_LoadPublicKey(const char *certNickname, 
                  SECKEYPublicKey **publicKey, 
                  CERTCertificate **cert)
{
  secuPWData pwdata = { PW_NONE, 0 };
  if (!cert || !publicKey || !cert) {
    return CryptoX_Error;
  }

  /* Get the cert and embedded public key out of the database */
  *cert = PK11_FindCertFromNickname(certNickname, &pwdata);
  if (!*cert) {
    return CryptoX_Error;
  }
  *publicKey = CERT_ExtractPublicKey(*cert);
  if (!*publicKey) {
    CERT_DestroyCertificate(*cert);
    return CryptoX_Error;
  }
  return CryptoX_Success;
}

CryptoX_Result
NSS_VerifyBegin(VFYContext **ctx, 
                SECKEYPublicKey * const *publicKey)
{
  SECStatus status;
  if (!ctx || !publicKey || !*publicKey) {
    return CryptoX_Error;
  }

  /* Check that the key length is large enough for our requirements */
  if ((SECKEY_PublicKeyStrength(*publicKey) * 8) < 
      XP_MIN_SIGNATURE_LEN_IN_BYTES) {
    fprintf(stderr, "ERROR: Key length must be >= %d bytes\n", 
            XP_MIN_SIGNATURE_LEN_IN_BYTES);
    return CryptoX_Error;
  }

  *ctx = VFY_CreateContext(*publicKey, NULL, 
                           SEC_OID_ISO_SHA1_WITH_RSA_SIGNATURE, NULL);
  if (*ctx == NULL) {
    return CryptoX_Error;
  }

  status = VFY_Begin(*ctx);
  return SECSuccess == status ? CryptoX_Success : CryptoX_Error;
}

/**
 * Verifies if a verify context matches the passed in signature.
 *
 * @param ctx          The verify context that the signature should match.
 * @param signature    The signature to match.
 * @param signatureLen The length of the signature.
 * @return CryptoX_Success on success, CryptoX_Error on error.
*/
CryptoX_Result
NSS_VerifySignature(VFYContext * const *ctx, 
                    const unsigned char *signature, 
                    unsigned int signatureLen)
{
  SECItem signedItem;
  SECStatus status;
  if (!ctx || !signature || !*ctx) {
    return CryptoX_Error;
  }

  signedItem.len = signatureLen;
  signedItem.data = (unsigned char*)signature;
  status = VFY_EndWithSignature(*ctx, &signedItem);
  return SECSuccess == status ? CryptoX_Success : CryptoX_Error;
}

#elif defined(XP_WIN)
/**
 * Verifies if a signature + public key matches a hash context.
 *
 * @param hash      The hash context that the signature should match.
 * @param pubKey    The public key to use on the signature.
 * @param signature The signature to check.
 * @param signatureLen The length of the signature.
 * @return CryptoX_Success on success, CryptoX_Error on error.
*/
CryptoX_Result
CyprtoAPI_VerifySignature(HCRYPTHASH *hash, 
                          HCRYPTKEY *pubKey,
                          const BYTE *signature, 
                          DWORD signatureLen)
{
  DWORD i;
  BOOL result;
/* Windows APIs expect the bytes in the signature to be in little-endian 
 * order, but we write the signature in big-endian order.  Other APIs like 
 * NSS and OpenSSL expect big-endian order.
 */
  BYTE *signatureReversed;
  if (!hash || !pubKey || !signature || signatureLen < 1) {
    return CryptoX_Error;
  }

  signatureReversed = malloc(signatureLen);
  if (!signatureReversed) {
    return CryptoX_Error;
  }

  for (i = 0; i < signatureLen; i++) {
    signatureReversed[i] = signature[signatureLen - 1 - i]; 
  }
  result = CryptVerifySignature(*hash, signatureReversed,
                                signatureLen, *pubKey, NULL, 0);
  free(signatureReversed);
  return result ? CryptoX_Success : CryptoX_Error;
}

/** 
 * Obtains the public key for the passed in cert data
 * 
 * @param provider       The cyrto provider
 * @param certData       Data of the certificate to extract the public key from
 * @param sizeOfCertData The size of the certData buffer
 * @param certStore      Pointer to the handle of the certificate store to use
 * @param CryptoX_Success on success
*/
CryptoX_Result
CryptoAPI_LoadPublicKey(HCRYPTPROV provider, 
                        BYTE *certData,
                        DWORD sizeOfCertData,
                        HCRYPTKEY *publicKey,
                        HCERTSTORE *certStore)
{
  CRYPT_DATA_BLOB blob;
  CERT_CONTEXT *context;
  if (!provider || !certData || !publicKey || !certStore) {
    return CryptoX_Error;
  }

  blob.cbData = sizeOfCertData;
  blob.pbData = certData;
  if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob, 
                        CERT_QUERY_CONTENT_FLAG_CERT, 
                        CERT_QUERY_FORMAT_FLAG_BINARY, 
                        0, NULL, NULL, NULL, 
                        certStore, NULL, (const void **)&context)) {
    return CryptoX_Error;
  }

  if (!CryptImportPublicKeyInfo(provider, 
                                PKCS_7_ASN_ENCODING | X509_ASN_ENCODING,
                                &context->pCertInfo->SubjectPublicKeyInfo,
                                publicKey)) {
    CertFreeCertificateContext(context);
    return CryptoX_Error;
  }

  CertFreeCertificateContext(context);
  return CryptoX_Success;
}

/* Try to acquire context in this way:
  * 1. Enhanced provider without creating a new key set
  * 2. Enhanced provider with creating a new key set
  * 3. Default provider without creating a new key set
  * 4. Default provider without creating a new key set
  * #2 and #4 should not be needed because of the CRYPT_VERIFYCONTEXT, 
  * but we add it just in case.
  *
  * @param provider Out parameter containing the provider handle.
  * @return CryptoX_Success on success, CryptoX_Error on error.
 */
CryptoX_Result
CryptoAPI_InitCryptoContext(HCRYPTPROV *provider)
{
  if (!CryptAcquireContext(provider, 
                           NULL, 
                           MS_ENHANCED_PROV, 
                           PROV_RSA_FULL, 
                           CRYPT_VERIFYCONTEXT)) {
    if (!CryptAcquireContext(provider, 
                             NULL, 
                             MS_ENHANCED_PROV, 
                             PROV_RSA_FULL, 
                             CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT)) {
      if (!CryptAcquireContext(provider, 
                               NULL, 
                               NULL, 
                               PROV_RSA_FULL, 
                               CRYPT_VERIFYCONTEXT)) {
        if (!CryptAcquireContext(provider, 
                                 NULL, 
                                 NULL, 
                                 PROV_RSA_FULL, 
                                 CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT)) {
          *provider = CryptoX_InvalidHandleValue;
          return CryptoX_Error;
        }
      }
    }
  }
  return CryptoX_Success;
}

/** 
  * Begins a signature verification hash context
  *
  * @param provider The crypt provider to use
  * @param hash     Out parameter for a handle to the hash context
  * @return CryptoX_Success on success, CryptoX_Error on error.
*/
CryptoX_Result
CryptoAPI_VerifyBegin(HCRYPTPROV provider, HCRYPTHASH* hash)
{
  BOOL result;
  if (!provider || !hash) {
    return CryptoX_Error;
  }

  *hash = (HCRYPTHASH)NULL;
  result = CryptCreateHash(provider, CALG_SHA1,
                           0, 0, hash);
  return result ? CryptoX_Success : CryptoX_Error;
}

/** 
  * Updates a signature verification hash context
  *
  * @param hash The hash context to udpate
  * @param buf  The buffer to update the hash context with
  * @param len The size of the passed in buffer
  * @return CryptoX_Success on success, CryptoX_Error on error.
*/
CryptoX_Result
CryptoAPI_VerifyUpdate(HCRYPTHASH* hash, BYTE *buf, DWORD len)
{
  BOOL result;
  if (!hash || !buf) {
    return CryptoX_Error;
  }

  result = CryptHashData(*hash, buf, len, 0);
  return result ? CryptoX_Success : CryptoX_Error;
}

#endif



