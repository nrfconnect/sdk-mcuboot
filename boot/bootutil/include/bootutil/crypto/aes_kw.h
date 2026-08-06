/*
 * This module provides a thin abstraction over some of the crypto
 * primitives to make it easier to swap out the used crypto library.
 *
 * At this point, there is only one choice: MCUBOOT_USE_MBED_TLS.
 */

#ifndef __BOOTUTIL_CRYPTO_AES_KW_H_
#define __BOOTUTIL_CRYPTO_AES_KW_H_

#include "mcuboot_config/mcuboot_config.h"

#if (defined(MCUBOOT_USE_MBED_TLS)) != 1
    #error "One crypto backend must be defined: MBED_TLS"
#endif

#if defined(MCUBOOT_USE_MBED_TLS)
    #include <mbedtls/private/aes.h>
    #include <mbedtls/nist_kw.h>
#endif /* MCUBOOT_USE_MBED_TLS */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MCUBOOT_USE_MBED_TLS)
typedef mbedtls_nist_kw_context bootutil_aes_kw_context;
static inline void bootutil_aes_kw_init(bootutil_aes_kw_context *ctx)
{
    (void)mbedtls_nist_kw_init(ctx);
}

static inline void bootutil_aes_kw_drop(bootutil_aes_kw_context *ctx)
{
    mbedtls_nist_kw_free(ctx);
}

static inline int bootutil_aes_kw_set_unwrap_key(bootutil_aes_kw_context *ctx, const uint8_t *k, uint32_t klen)
{
    return mbedtls_nist_kw_setkey(ctx, MBEDTLS_CIPHER_ID_AES, k, klen * 8, 0);
}

static inline int bootutil_aes_kw_unwrap(bootutil_aes_kw_context *ctx, const uint8_t *wrapped_key, uint32_t wrapped_key_len, uint8_t *key, uint32_t key_len)
{
    size_t olen;
    return mbedtls_nist_kw_unwrap(ctx, MBEDTLS_KW_MODE_KW, wrapped_key, wrapped_key_len, key, &olen, key_len);
}
#endif /* MCUBOOT_USE_MBED_TLS */

#ifdef __cplusplus
}
#endif

#endif /* __BOOTUTIL_CRYPTO_AES_KW_H_ */
