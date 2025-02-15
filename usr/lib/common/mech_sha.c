/*
 * COPYRIGHT (c) International Business Machines Corp. 2001-2017
 *
 * This program is provided under the terms of the Common Public License,
 * version 1.0 (CPL-1.0). Any use, reproduction or distribution for this
 * software constitutes recipient's acceptance of CPL-1.0 terms which can be
 * found in the file LICENSE file or at
 * https://opensource.org/licenses/cpl1.0.php
 */

// File:  mech_sha.c
//
// Mechanisms for SHA-1 related routines
//
// The following applies to the software SHA implementation:
//    Written 2 September 1992, Peter C. Gutmann.
//    This implementation placed in the public domain.
//
//    Modified 1 June 1993, Colin Plumb.
//    Modified for the new SHS based on Peter Gutmann's work,
//    18 July 1994, Colin Plumb.
//    Gutmann's work.
//    Renamed to SHA and comments updated a bit 1 November 1995, Colin Plumb.
//    These modifications placed in the public domain.
//
//    Comments to pgut1@cs.aukuni.ac.nz
//

#include <pthread.h>
#include <string.h>             // for memcmp() et al
#include <stdlib.h>
#include <memory.h>

#include "pkcs11types.h"
#include "defs.h"
#include "host_defs.h"
#include "h_extern.h"
#include "tok_spec_struct.h"
#include "trace.h"

#include <openssl/evp.h>
#include <openssl/crypto.h>

//
// Software SHA-1 implementation (OpenSSL based)
//

static void sw_sha1_free(STDLL_TokData_t *tokdata, SESSION *sess,
                         CK_BYTE *context, CK_ULONG context_len)
{
    UNUSED(tokdata);
    UNUSED(sess);
    UNUSED(context_len);

    EVP_MD_CTX_free((EVP_MD_CTX *)context);
}

CK_RV sw_sha1_init(DIGEST_CONTEXT *ctx)
{
    ctx->context_len = 1;
    ctx->context = (CK_BYTE *)EVP_MD_CTX_new();
    if (ctx->context == NULL) {
        TRACE_ERROR("%s\n", ock_err(ERR_HOST_MEMORY));
        ctx->context_len = 0;
        return CKR_HOST_MEMORY;
    }

    if (!EVP_DigestInit_ex((EVP_MD_CTX *)ctx->context, EVP_sha1(), NULL)) {
        TRACE_ERROR("%s\n", ock_err(ERR_FUNCTION_FAILED));
        EVP_MD_CTX_free((EVP_MD_CTX *)ctx->context);
        ctx->context = NULL;
        ctx->context_len = 0;
        return CKR_FUNCTION_FAILED;
    }

    ctx->state_unsaveable = CK_TRUE;
    ctx->context_free_func = sw_sha1_free;

    return CKR_OK;
}

CK_RV sw_sha1_hash(DIGEST_CONTEXT *ctx, CK_BYTE *in_data,
                   CK_ULONG in_data_len, CK_BYTE *out_data,
                   CK_ULONG *out_data_len)
{
    unsigned int len;

    if (!ctx || !out_data_len) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (*out_data_len < SHA1_HASH_SIZE) {
        TRACE_ERROR("%s\n", ock_err(ERR_BUFFER_TOO_SMALL));
        return CKR_BUFFER_TOO_SMALL;
    }

    if (ctx->context == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    len = *out_data_len;
    if (!EVP_DigestUpdate((EVP_MD_CTX *)ctx->context, in_data, in_data_len) ||
        !EVP_DigestFinal((EVP_MD_CTX *)ctx->context, out_data, &len)) {
        TRACE_ERROR("%s\n", ock_err(ERR_FUNCTION_FAILED));
        return CKR_FUNCTION_FAILED;
    }

    *out_data_len = len;

    EVP_MD_CTX_free((EVP_MD_CTX *)ctx->context);
    ctx->context = NULL;
    ctx->context_free_func = NULL;

    return CKR_OK;
}

static CK_RV sw_sha1_update(DIGEST_CONTEXT *ctx, CK_BYTE *in_data,
                            CK_ULONG in_data_len)
{
    if (ctx->context == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    if (!EVP_DigestUpdate((EVP_MD_CTX *)ctx->context, in_data, in_data_len)) {
        TRACE_ERROR("%s\n", ock_err(ERR_FUNCTION_FAILED));
        return CKR_FUNCTION_FAILED;
    }

    return CKR_OK;
}

static CK_RV sw_sha1_final(DIGEST_CONTEXT *ctx, CK_BYTE *out_data,
                           CK_ULONG *out_data_len)
{
    unsigned int len;

    if (ctx->context == NULL)
        return CKR_OPERATION_NOT_INITIALIZED;

    if (*out_data_len < SHA1_HASH_SIZE) {
        TRACE_ERROR("%s\n", ock_err(ERR_BUFFER_TOO_SMALL));
        return CKR_BUFFER_TOO_SMALL;
    }

    len = *out_data_len;
    if (!EVP_DigestFinal((EVP_MD_CTX *)ctx->context, out_data, &len)) {
        TRACE_ERROR("%s\n", ock_err(ERR_FUNCTION_FAILED));
        return CKR_FUNCTION_FAILED;
    }

    *out_data_len = len;

    EVP_MD_CTX_free((EVP_MD_CTX *)ctx->context);
    ctx->context = NULL;
    ctx->context_free_func = NULL;

    return CKR_OK;
}

CK_RV sha_init(STDLL_TokData_t *tokdata, SESSION *sess, DIGEST_CONTEXT *ctx,
               CK_MECHANISM *mech)
{
    UNUSED(sess);

    if (token_specific.t_sha_init != NULL) {
        return token_specific.t_sha_init(tokdata, ctx, mech);
    } else {
        /* For current tokens, continue legacy of using software
         *  implemented SHA-1 if the token does not have its own
         *  SHA-1 implementation.
         *  Future tokens' crypto should be its own so that
         *  opencryptoki is not responsible for crypto. If token
         *  does not have SHA-1, then should be mechanism not
         *  supported. JML
         */
        if (mech->mechanism == CKM_SHA_1) {
            return sw_sha1_init(ctx);
        } else {
            return CKR_MECHANISM_INVALID;
        }
    }
}

CK_RV sha_hash(STDLL_TokData_t *tokdata, SESSION *sess, CK_BBOOL length_only,
               DIGEST_CONTEXT *ctx, CK_BYTE *in_data, CK_ULONG in_data_len,
               CK_BYTE *out_data, CK_ULONG *out_data_len)
{
    CK_ULONG hsize;

    UNUSED(sess);

    if (!ctx || !out_data_len) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    switch (ctx->mech.mechanism) {
    case CKM_SHA_1:
        hsize = SHA1_HASH_SIZE;
        break;
    case CKM_SHA224:
    case CKM_SHA512_224:
        hsize = SHA224_HASH_SIZE;
        break;
    case CKM_SHA256:
    case CKM_SHA512_256:
        hsize = SHA256_HASH_SIZE;
        break;
    case CKM_SHA384:
        hsize = SHA384_HASH_SIZE;
        break;
    case CKM_SHA512:
        hsize = SHA512_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_224:
        hsize = SHA3_224_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_256:
        hsize = SHA3_256_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_384:
        hsize = SHA3_384_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_512:
        hsize = SHA3_512_HASH_SIZE;
        break;
    default:
        return CKR_MECHANISM_INVALID;
    }

    if (length_only == TRUE) {
        *out_data_len = hsize;
        return CKR_OK;
    }

    if (*out_data_len < hsize) {
        *out_data_len = hsize;
        TRACE_ERROR("%s\n", ock_err(ERR_BUFFER_TOO_SMALL));
        return CKR_BUFFER_TOO_SMALL;
    }

    if (ctx->context == NULL)
        return CKR_HOST_MEMORY;

    if (token_specific.t_sha != NULL) {
        return token_specific.t_sha(tokdata, ctx, in_data, in_data_len,
                                    out_data, out_data_len);
    } else {
        if (ctx->mech.mechanism == CKM_SHA_1)
            return sw_sha1_hash(ctx, in_data, in_data_len, out_data,
                                out_data_len);
        else
            return CKR_MECHANISM_INVALID;
    }
}

//
//
CK_RV sha_hash_update(STDLL_TokData_t *tokdata, SESSION *sess,
                      DIGEST_CONTEXT *ctx, CK_BYTE *in_data,
                      CK_ULONG in_data_len)
{
    UNUSED(sess);

    /* if no data to hash, just return */
    if (!in_data_len)
        return CKR_OK;

    if (token_specific.t_sha_update != NULL) {
        return token_specific.t_sha_update(tokdata, ctx, in_data, in_data_len);
    } else {
        if (ctx->mech.mechanism == CKM_SHA_1)
            return sw_sha1_update(ctx, in_data, in_data_len);
        else
            return CKR_MECHANISM_INVALID;
    }
}

CK_RV sha_hash_final(STDLL_TokData_t *tokdata, SESSION *sess,
                     CK_BYTE length_only, DIGEST_CONTEXT *ctx,
                     CK_BYTE *out_data, CK_ULONG *out_data_len)
{
    CK_ULONG hsize;

    UNUSED(sess);

    if (!out_data_len) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    switch (ctx->mech.mechanism) {
    case CKM_SHA_1:
        hsize = SHA1_HASH_SIZE;
        break;
    case CKM_SHA224:
    case CKM_SHA512_224:
        hsize = SHA224_HASH_SIZE;
        break;
    case CKM_SHA256:
    case CKM_SHA512_256:
        hsize = SHA256_HASH_SIZE;
        break;
    case CKM_SHA384:
        hsize = SHA384_HASH_SIZE;
        break;
    case CKM_SHA512:
        hsize = SHA512_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_224:
        hsize = SHA3_224_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_256:
        hsize = SHA3_256_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_384:
        hsize = SHA3_384_HASH_SIZE;
        break;
    case CKM_IBM_SHA3_512:
        hsize = SHA3_512_HASH_SIZE;
        break;
    default:
        return CKR_MECHANISM_INVALID;
    }

    if (length_only == TRUE) {
        *out_data_len = hsize;
        return CKR_OK;
    }

    if (*out_data_len < hsize) {
        *out_data_len = hsize;
        TRACE_ERROR("%s\n", ock_err(ERR_BUFFER_TOO_SMALL));
        return CKR_BUFFER_TOO_SMALL;
    }

    if (token_specific.t_sha_final != NULL) {
        return token_specific.t_sha_final(tokdata, ctx, out_data, out_data_len);
    } else {
        if (ctx->mech.mechanism == CKM_SHA_1)
            return sw_sha1_final(ctx, out_data, out_data_len);
        else
            return CKR_MECHANISM_INVALID;
    }
}

// this routine gets called for these mechanisms actually:
// CKM_SHA_1_HMAC
// CKM_SHA_1_HMAC_GENERAL
// CKM_SHA224_HMAC
// CKM_SHA224_HMAC_GENERAL
// CKM_SHA256_HMAC
// CKM_SHA256_HMAC_GENERAL
// CKM_SHA384_HMAC
// CKM_SHA384_HMAC_GENERAL
// CKM_SHA512_HMAC
// CKM_SHA512_HMAC_GENERAL
// CKM_SHA512_224_HMAC
// CKM_SHA512_224_HMAC_GENERAL
// CKM_SHA512_256_HMAC
// CKM_SHA512_256_HMAC_GENERAL
// CKM_IBM_SHA3_224_HMAC
// CKM_IBM_SHA3_256_HMAC
// CKM_IBM_SHA3_384_HMAC
// CKM_IBM_SHA3_512_HMAC
//
CK_RV sha_hmac_sign(STDLL_TokData_t *tokdata,
                    SESSION *sess, CK_BBOOL length_only,
                    SIGN_VERIFY_CONTEXT *ctx, CK_BYTE *in_data,
                    CK_ULONG in_data_len, CK_BYTE *out_data,
                    CK_ULONG *out_data_len)
{
    OBJECT *key_obj = NULL;
    CK_ATTRIBUTE *attr = NULL;
    CK_BYTE hash[MAX_SHA_HASH_SIZE];
    DIGEST_CONTEXT digest_ctx;
    CK_MECHANISM digest_mech;
    CK_BYTE k_ipad[MAX_SHA_BLOCK_SIZE];
    CK_BYTE k_opad[MAX_SHA_BLOCK_SIZE];
    CK_ULONG key_bytes, hash_len, hmac_len, digest_hash_len, digest_block_size;
    CK_BBOOL general = FALSE;
    CK_ULONG i;
    CK_RV rc;

    if (!sess || !ctx || !out_data_len) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    digest_mech.ulParameterLen = 0;
    digest_mech.pParameter = NULL;
    rc = get_hmac_digest(ctx->mech.mechanism, &digest_mech.mechanism, &general);
    if (rc != 0) {
        TRACE_ERROR("get_hmac_digest failed");
        return rc;
    }

    rc = get_sha_block_size(digest_mech.mechanism, &digest_block_size);
    if (rc != 0) {
        TRACE_ERROR("get_sha_block_size failed");
        return rc;
    }

    rc = get_sha_size(digest_mech.mechanism, &digest_hash_len);
    if (rc != 0) {
        TRACE_ERROR("get_sha_size failed");
        return rc;
    }

    if (general == FALSE) {
        hmac_len = digest_hash_len;
    } else {
        hmac_len = *(CK_ULONG *)ctx->mech.pParameter;
        if (hmac_len > digest_hash_len)
            return CKR_MECHANISM_PARAM_INVALID;

        if (hmac_len == 0) {
            *out_data_len = 0;
            return CKR_OK;
        }
    }

    if (length_only == TRUE) {
        *out_data_len = hmac_len;
        return CKR_OK;
    }

    if (token_specific.t_hmac_sign != NULL)
        return token_specific.t_hmac_sign(tokdata, sess, in_data,
                                          in_data_len, out_data, out_data_len);

    /* Do manual hmac if token doesn't have an hmac crypto call.
     * Secure tokens should not do manual hmac.
     */

    memset(&digest_ctx, 0x0, sizeof(DIGEST_CONTEXT));

    rc = object_mgr_find_in_map1(tokdata, ctx->key, &key_obj, READ_LOCK);
    if (rc != CKR_OK) {
        TRACE_ERROR("Failed to acquire key from specified handle.\n");
        if (rc == CKR_OBJECT_HANDLE_INVALID)
            return CKR_KEY_HANDLE_INVALID;
        else
            return rc;
    }

    rc = template_attribute_get_non_empty(key_obj->template, CKA_VALUE, &attr);
    if (rc != CKR_OK) {
        TRACE_ERROR("Could not find CKA_VALUE in the template\n");
        goto done;
    }

    key_bytes = attr->ulValueLen;

    // build (K XOR ipad), (K XOR opad)
    //
    if (key_bytes > digest_block_size) {
        rc = digest_mgr_init(tokdata, sess, &digest_ctx, &digest_mech, FALSE);
        if (rc != CKR_OK) {
            TRACE_DEVEL("Digest Mgr Init failed.\n");
            goto done;
        }

        hash_len = digest_hash_len;
        rc = digest_mgr_digest(tokdata, sess, FALSE, &digest_ctx,
                               attr->pValue, attr->ulValueLen, hash, &hash_len);
        if (rc != CKR_OK) {
            TRACE_DEVEL("Digest Mgr Digest failed.\n");
            digest_mgr_cleanup(tokdata, sess, &digest_ctx);
            goto done;
        }

        memset(&digest_ctx, 0x0, sizeof(DIGEST_CONTEXT));

        for (i = 0; i < hash_len; i++) {
            k_ipad[i] = hash[i] ^ 0x36;
            k_opad[i] = hash[i] ^ 0x5C;
        }

        memset(&k_ipad[i], 0x36, digest_block_size - i);
        memset(&k_opad[i], 0x5C, digest_block_size - i);
    } else {
        CK_BYTE *key = attr->pValue;

        for (i = 0; i < key_bytes; i++) {
            k_ipad[i] = key[i] ^ 0x36;
            k_opad[i] = key[i] ^ 0x5C;
        }

        memset(&k_ipad[i], 0x36, digest_block_size - key_bytes);
        memset(&k_opad[i], 0x5C, digest_block_size - key_bytes);
    }

    // inner hash
    //
    rc = digest_mgr_init(tokdata, sess, &digest_ctx, &digest_mech, FALSE);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Init failed.\n");
        goto done;
    }

    rc = digest_mgr_digest_update(tokdata, sess, &digest_ctx, k_ipad,
                                  digest_block_size);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Update failed.\n");
        goto done;
    }

    rc = digest_mgr_digest_update(tokdata, sess, &digest_ctx, in_data,
                                  in_data_len);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Update failed.\n");
        goto done;
    }

    hash_len = digest_hash_len;
    rc = digest_mgr_digest_final(tokdata, sess, FALSE, &digest_ctx, hash,
                                 &hash_len);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Final failed.\n");
        goto done;
    }

    memset(&digest_ctx, 0x0, sizeof(DIGEST_CONTEXT));

    // outer hash
    //
    rc = digest_mgr_init(tokdata, sess, &digest_ctx, &digest_mech, FALSE);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Init failed.\n");
        goto done;
    }

    rc = digest_mgr_digest_update(tokdata, sess, &digest_ctx, k_opad,
                                  digest_block_size);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Update failed.\n");
        goto done;
    }

    rc = digest_mgr_digest_update(tokdata, sess, &digest_ctx, hash, hash_len);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Update failed.\n");
        goto done;
    }

    hash_len = digest_hash_len;
    rc = digest_mgr_digest_final(tokdata, sess, FALSE, &digest_ctx, hash,
                                 &hash_len);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Digest Mgr Final failed.\n");
        goto done;
    }

    memcpy(out_data, hash, hmac_len);
    *out_data_len = hmac_len;

done:
    object_put(tokdata, key_obj, TRUE);
    key_obj = NULL;

    return rc;
}

// this routine gets called for these mechanisms actually:
// CKM_SHA_1_HMAC
// CKM_SHA_1_HMAC_GENERAL
// CKM_SHA224_HMAC
// CKM_SHA224_HMAC_GENERAL
// CKM_SHA256_HMAC
// CKM_SHA256_HMAC_GENERAL
// CKM_SHA384_HMAC
// CKM_SHA384_HMAC_GENERAL
// CKM_SHA512_HMAC
// CKM_SHA512_HMAC_GENERAL
// CKM_SHA512_224_HMAC
// CKM_SHA512_224_HMAC_GENERAL
// CKM_SHA512_256_HMAC
// CKM_SHA512_256_HMAC_GENERAL
// CKM_IBM_SHA3_224_HMAC
// CKM_IBM_SHA3_256_HMAC
// CKM_IBM_SHA3_384_HMAC
// CKM_IBM_SHA3_512_HMAC
//
CK_RV sha_hmac_verify(STDLL_TokData_t *tokdata, SESSION *sess,
                      SIGN_VERIFY_CONTEXT *ctx,
                      CK_BYTE *in_data, CK_ULONG in_data_len,
                      CK_BYTE *signature, CK_ULONG sig_len)
{
    CK_BYTE hmac[MAX_SHA_HASH_SIZE];
    SIGN_VERIFY_CONTEXT hmac_ctx;
    CK_ULONG hmac_len, len, digest_mech, digest_hash_len;
    CK_BBOOL general = FALSE;
    CK_RV rc;

    if (!sess || !ctx || !in_data || !signature) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (token_specific.t_hmac_verify != NULL)
        return token_specific.t_hmac_verify(tokdata, sess, in_data,
                                            in_data_len, signature, sig_len);

    /* Do manual hmac verify  if token doesn't have an hmac crypto call.
     * Secure tokens should not do manual hmac.
     */

    rc = get_hmac_digest(ctx->mech.mechanism, &digest_mech, &general);
    if (rc != 0) {
        TRACE_ERROR("get_hmac_digest failed");
        return rc;
    }

    rc = get_sha_size(digest_mech, &digest_hash_len);
    if (rc != 0) {
        TRACE_ERROR("get_sha_size failed");
        return rc;
    }

    if (general == FALSE) {
        hmac_len = digest_hash_len;
    } else {
        hmac_len = *(CK_ULONG *)ctx->mech.pParameter;
        if (hmac_len > digest_hash_len)
            return CKR_MECHANISM_PARAM_INVALID;
    }

    memset(&hmac_ctx, 0, sizeof(SIGN_VERIFY_CONTEXT));

    rc = sign_mgr_init(tokdata, sess, &hmac_ctx, &ctx->mech, FALSE, ctx->key,
                       FALSE);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Sign Mgr Init failed.\n");
        goto done;
    }
    len = hmac_len;
    rc = sign_mgr_sign(tokdata, sess, FALSE, &hmac_ctx, in_data, in_data_len,
                       hmac, &len);
    if (rc != CKR_OK) {
        TRACE_DEVEL("Sign Mgr Sign failed.\n");
        goto done;
    }
    if ((len != hmac_len) || (len != sig_len)) {
        TRACE_ERROR("%s\n", ock_err(ERR_SIGNATURE_LEN_RANGE));
        rc = CKR_SIGNATURE_LEN_RANGE;
        goto done;
    }

    if (CRYPTO_memcmp(hmac, signature, hmac_len) != 0) {
        TRACE_ERROR("%s\n", ock_err(ERR_SIGNATURE_INVALID));
        rc = CKR_SIGNATURE_INVALID;
    }

done:
    sign_mgr_cleanup(tokdata, sess, &hmac_ctx);
    return rc;
}



CK_RV hmac_sign_init(STDLL_TokData_t *tokdata, SESSION *sess,
                     CK_MECHANISM *mech, CK_OBJECT_HANDLE hkey)
{
    if (token_specific.t_hmac_sign_init != NULL)
        return token_specific.t_hmac_sign_init(tokdata, sess, mech, hkey);

    /* Return ok with the intention that the local hmac
     * implementation will get used instead.
     * For those tokens not supporting HMAC at all,
     * will need to return CKR_MECHANISM_INVALID.
     */
    return CKR_OK;
}

CK_RV hmac_sign_update(STDLL_TokData_t *tokdata, SESSION *sess,
                       CK_BYTE *in_data, CK_ULONG in_data_len)
{
    SIGN_VERIFY_CONTEXT *ctx = &sess->sign_ctx;

    if (!sess || !ctx) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (token_specific.t_hmac_sign_update != NULL)
        return token_specific.t_hmac_sign_update(tokdata, sess,
                                                 in_data, in_data_len);

    TRACE_ERROR("hmac-update is not supported\n");

    return CKR_MECHANISM_INVALID;
}

CK_RV hmac_sign_final(STDLL_TokData_t *tokdata, SESSION *sess,
                      CK_BYTE *signature, CK_ULONG *sig_len)
{
    SIGN_VERIFY_CONTEXT *ctx = &sess->sign_ctx;

    if (!sess || !ctx) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (token_specific.t_hmac_sign_final != NULL)
        return token_specific.t_hmac_sign_final(tokdata, sess,
                                                signature, sig_len);

    TRACE_ERROR("hmac-final is not supported\n");

    return CKR_MECHANISM_INVALID;
}

CK_RV hmac_verify_init(STDLL_TokData_t *tokdata, SESSION *sess,
                       CK_MECHANISM *mech, CK_OBJECT_HANDLE hkey)
{
    if (token_specific.t_hmac_verify_init != NULL)
        return token_specific.t_hmac_verify_init(tokdata, sess, mech, hkey);

    /* Return ok with the intention that the local hmac
     * implementation will get used instead.
     * For those tokens not supporting HMAC at all,
     * will need to return CKR_MECHANISM_INVALID.
     */
    return CKR_OK;
}

CK_RV hmac_verify_update(STDLL_TokData_t *tokdata, SESSION *sess,
                         CK_BYTE *in_data, CK_ULONG in_data_len)
{
    SIGN_VERIFY_CONTEXT *ctx = &sess->sign_ctx;

    if (!sess || !ctx) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (token_specific.t_hmac_verify_update != NULL)
        return token_specific.t_hmac_verify_update(tokdata, sess,
                                                   in_data, in_data_len);

    TRACE_ERROR("hmac-update is not supported\n");

    return CKR_MECHANISM_INVALID;
}

CK_RV hmac_verify_final(STDLL_TokData_t *tokdata, SESSION *sess,
                        CK_BYTE *signature, CK_ULONG sig_len)
{
    SIGN_VERIFY_CONTEXT *ctx = &sess->sign_ctx;

    if (!sess || !ctx) {
        TRACE_ERROR("%s received bad argument(s)\n", __func__);
        return CKR_FUNCTION_FAILED;
    }

    if (token_specific.t_hmac_verify_final != NULL)
        return token_specific.t_hmac_verify_final(tokdata, sess,
                                                  signature, sig_len);

    TRACE_ERROR("hmac-final is not supported\n");

    return CKR_MECHANISM_INVALID;
}

CK_RV ckm_generic_secret_key_gen(STDLL_TokData_t *tokdata, TEMPLATE *tmpl)
{
    if (token_specific.t_generic_secret_key_gen == NULL) {
        TRACE_ERROR("%s\n", ock_err(ERR_MECHANISM_INVALID));
        return CKR_MECHANISM_INVALID;
    }

    return token_specific.t_generic_secret_key_gen(tokdata, tmpl);
}
