/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <mcuboot_config/mcuboot_config.h>
#include "bootutil/bootutil_log.h"

#include <psa/crypto.h>
#include <psa/crypto_types.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_BOOT_SIGNATURE_USING_KMU)
#include <cracen_psa_kmu.h>
#endif

BOOT_LOG_MODULE_REGISTER(ed25519_psa);

#define SHA512_DIGEST_LENGTH    64
#define EDDSA_KEY_LENGTH        32
#define EDDSA_SIGNAGURE_LENGTH  64

#if defined(CONFIG_BOOT_SIGNATURE_USING_KMU)
/* List of KMU stored key ids available for MCUboot */
#define PSA_KEY_INDEX_SIZE 2

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 || \
	defined(CONFIG_NCS_BOOT_SIGNATURE_KMU_UROT_MAPPING)
#define PSA_KEY_STARTING_ID 226
#else
#define PSA_KEY_STARTING_ID 242
#endif

#define MAKE_PSA_KMU_KEY_ID(id) PSA_KEY_HANDLE_FROM_CRACEN_KMU_SLOT(CRACEN_KMU_KEY_USAGE_SCHEME_RAW, id)
static psa_key_id_t key_ids[] =  {
    MAKE_PSA_KMU_KEY_ID(PSA_KEY_STARTING_ID),
    MAKE_PSA_KMU_KEY_ID(PSA_KEY_STARTING_ID + PSA_KEY_INDEX_SIZE),
    MAKE_PSA_KMU_KEY_ID(PSA_KEY_STARTING_ID + (2 * PSA_KEY_INDEX_SIZE))
};

#define KEY_SLOTS_COUNT CONFIG_BOOT_SIGNATURE_KMU_SLOTS

#if defined(CONFIG_BOOT_KMU_KEYS_REVOCATION)
#include <bootutil/key_revocation.h>
static psa_key_id_t *validated_with = NULL;
#endif

BUILD_ASSERT(CONFIG_BOOT_SIGNATURE_KMU_SLOTS <= ARRAY_SIZE(key_ids),
	     "Invalid number of KMU slots, up to 3 are supported on nRF54L15");
#endif

#if defined(CONFIG_NCS_BOOT_SIGNATURE_USING_ITS)
static const psa_key_id_t key_ids[] =  {
    0x40022100,
    0x40022101,
    0x40022102,
    0x40022103
};

#define KEY_SLOTS_COUNT ARRAY_SIZE(key_ids)
#endif

#if !defined(CONFIG_BOOT_SIGNATURE_USING_KMU) && !defined(CONFIG_NCS_BOOT_SIGNATURE_USING_ITS)
int ED25519_verify(const uint8_t *message, size_t message_len,
                   const uint8_t signature[EDDSA_SIGNAGURE_LENGTH],
                   const uint8_t public_key[EDDSA_KEY_LENGTH])
{
    /* Set to any error */
    psa_status_t status = PSA_ERROR_BAD_STATE;
    psa_key_attributes_t key_attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t kid;
    int ret = 0;        /* Fail by default */

    BOOT_LOG_DBG("ED25519_verify: PSA implementation");

    /* Initialize PSA Crypto */
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("PSA crypto init failed %d\n", status);
        return 0;
    }

    status = PSA_ERROR_BAD_STATE;

    psa_set_key_type(&key_attr,
                     PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&key_attr, PSA_ALG_PURE_EDDSA);

    status = psa_import_key(&key_attr, public_key, EDDSA_KEY_LENGTH, &kid);
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("ED25519 key import failed %d", status);
        return 0;
    }

    status = psa_verify_message(kid, PSA_ALG_PURE_EDDSA, message, message_len,
                                signature, EDDSA_SIGNAGURE_LENGTH);
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("ED25519 signature verification failed %d", status);
        ret = 0;
        /* Pass through to destroy key */
    } else {
	ret = 1;
        /* Pass through to destroy key */
    }

    status = psa_destroy_key(kid);

    if (status != PSA_SUCCESS) {
        /* Just for logging */
        BOOT_LOG_WRN("Failed to destroy key %d", status);
    }

    return ret;
}
#else
int ED25519_verify(const uint8_t *message, size_t message_len,
                          const uint8_t signature[EDDSA_SIGNAGURE_LENGTH],
                          const uint8_t public_key[EDDSA_KEY_LENGTH])
{
    ARG_UNUSED(public_key);
    /* Set to any error */
    psa_status_t status = PSA_ERROR_BAD_STATE;

    /* Initialize PSA Crypto */
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("PSA crypto init failed %d", status);
        return 0;
    }

    status = PSA_ERROR_BAD_STATE;

    for (int i = 0; i < KEY_SLOTS_COUNT; ++i) {
        psa_key_id_t kid = key_ids[i];

        status = psa_verify_message(kid, PSA_ALG_PURE_EDDSA, message,
                                    message_len, signature,
                                    EDDSA_SIGNAGURE_LENGTH);
        if (status == PSA_SUCCESS) {
#if defined(CONFIG_BOOT_KMU_KEYS_REVOCATION)
            validated_with = key_ids + i;
#endif
            return 1;
        }

    }

    BOOT_LOG_ERR("ED25519 signature verification failed %d", status);

    return 0;
}
#if defined(CONFIG_BOOT_KMU_KEYS_REVOCATION)
int exec_revoke(void)
{
    int ret = BOOT_KEY_REVOKE_OK;
    psa_status_t status = psa_crypto_init();

    if (!validated_with) {
        ret = BOOT_KEY_REVOKE_INVALID;
        goto out;
    }

    if (status != PSA_SUCCESS) {
            BOOT_LOG_ERR("PSA crypto init failed with error %d", status);
            ret = BOOT_KEY_REVOKE_FAILED;
            goto out;
    }
    for (int i = 0; i < CONFIG_BOOT_SIGNATURE_KMU_SLOTS; i++) {
        if ((key_ids + i) == validated_with) {
            break;
        }
        BOOT_LOG_DBG("Invalidating key ID %d", i);

        status = psa_destroy_key(key_ids[i]);
        if (status == PSA_SUCCESS) {
            BOOT_LOG_DBG("Success on key ID %d", i);
        } else {
            BOOT_LOG_ERR("Key invalidation failed with: %d", status);
        }
    }
out:
    return ret;
}
#endif /* CONFIG_BOOT_KMU_KEYS_REVOCATION */

void nrf_crypto_keys_housekeeping(void)
{
    psa_status_t status;

    /* We will continue through all keys, even if we have error while
     * processing any of it. Only doing BOOT_LOG_DBG, as we do not
     * really want to inform on failures to lock.
     */
    for (int i = 0; i < CONFIG_BOOT_SIGNATURE_KMU_SLOTS; ++i) {
        psa_key_attributes_t attr;

        status = psa_get_key_attributes(key_ids[i], &attr);
        BOOT_LOG_DBG("KMU key 0x%x(%d) attr query status == %d",
                     key_ids[i], i, status);

        if (status == PSA_SUCCESS) {
            status = cracen_kmu_block(&attr);
            BOOT_LOG_DBG("KMU key lock status == %d", status);
        }

        status = psa_purge_key(key_ids[i]);
        BOOT_LOG_DBG("KMU key 0x%x(%d) purge status == %d",
                     key_ids[i], i, status);
    }
}

#endif
