/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2017-2018 Linaro LTD
 * Copyright (c) 2017-2019 JUUL Labs
 * Copyright (c) 2020-2023 Arm Limited
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * Original license:
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>

#include "mcuboot_config/mcuboot_config.h"

#ifdef MCUBOOT_SIGN_RSA
#include "bootutil/sign_key.h"

#include <assert.h>
#include <string.h>
#include <stdint.h>

#include <psa/crypto.h>
#include <psa/crypto_types.h>
#include <zephyr/sys/util.h>

#include "bootutil/bootutil_log.h"
#include "bootutil/crypto/sha.h"
#include "bootutil_priv.h"

#include <psa/crypto.h>

BOOT_LOG_MODULE_DECLARE(mcuboot);

#define RSA_SIGNATURE_LENGTH        (256)
#define RSA_PUBLIC_KEY_BIT_SIZE     (2048)

extern const unsigned int rsa_pub_key_len;

int rsa_verify(const uint8_t *message, size_t message_len,
                   const uint8_t signature[RSA_SIGNATURE_LENGTH],
                   const uint8_t public_key[PSA_EXPORT_PUBLIC_KEY_MAX_SIZE])
{
    /* Set to any error */
    psa_status_t status = PSA_ERROR_BAD_STATE;
    psa_key_attributes_t key_attr = PSA_KEY_ATTRIBUTES_INIT;
    int ret = 0;        /* Fail by default */
    psa_key_id_t key_id;

    BOOT_LOG_DBG("rsa_verify: PSA implementation, plain key");

    /* Initialize PSA Crypto */
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("PSA crypto init failed %d\n", status);
        return 0;
    }

    status = PSA_ERROR_BAD_STATE;

    psa_set_key_usage_flags(&key_attr, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_lifetime(&key_attr, PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_algorithm(&key_attr, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    psa_set_key_type(&key_attr, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&key_attr, RSA_PUBLIC_KEY_BIT_SIZE);

    BOOT_LOG_DBG("Importing RSA PSS key of size: %d", rsa_pub_key_len);

    status = psa_import_key(&key_attr, public_key, rsa_pub_key_len,
                            &key_id);
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("RSA import key failed %d", status);
        ret = 0;
    }

    BOOT_LOG_DBG("Key imported. key_id: %d", (int)key_id);

    status = PSA_ERROR_BAD_STATE;

    BOOT_LOG_INF("Verifying RSA PSS with signature len: %d", RSA_SIGNATURE_LENGTH);

    status = psa_verify_hash(key_id, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256),
                             message, message_len, signature, RSA_SIGNATURE_LENGTH);
    if (status != PSA_SUCCESS) {
        BOOT_LOG_ERR("RSA signature verification failed %d", status);
        ret = 0;
    } else {
        BOOT_LOG_INF("RSA signature verification successful");
        ret = 1;
    }

    return ret;
}


static fih_ret
bootutil_verify(uint8_t *buf, uint32_t blen,
                uint8_t *sig, size_t slen,
                uint8_t key_id)
{
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    uint8_t *pubkey = NULL;

    BOOT_LOG_DBG("bootutil_verify: RSA key_id %d", (int)key_id);

    if (slen != RSA_SIGNATURE_LENGTH) {
        BOOT_LOG_DBG("bootutil_verify: expected slen %d, got %u",
                     RSA_SIGNATURE_LENGTH, (unsigned int)slen);
        FIH_SET(fih_rc, FIH_FAILURE);
        goto out;
    }

    pubkey = (uint8_t *)bootutil_keys[key_id].key;

    BOOT_LOG_DBG("bootutil_verify: RSA key_id %d", (int)key_id);

    rc = rsa_verify(buf, blen, sig, pubkey);

    BOOT_LOG_DBG("bootutil_verify: rsa_verify status %d", rc);

    if (rc == 0) {
        /* if verify returns 0, there was an error. */
        FIH_SET(fih_rc, FIH_FAILURE);
        goto out;
    }

    FIH_SET(fih_rc, FIH_SUCCESS);
out:

    FIH_RET(fih_rc);
}

/* Hash signature verification function.
 * Verifies hash against provided signature.
 * The function verifies that hash is of expected size and then
 * calls bootutil_verify to do the signature verification.
 */
fih_ret
bootutil_verify_sig(uint8_t *hash, uint32_t hlen,
                    uint8_t *sig, size_t slen,
                    uint8_t key_id)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("bootutil_verify_sig: RSA key_id %d", (int)key_id);

    if (hlen != IMAGE_HASH_SIZE) {
        BOOT_LOG_DBG("bootutil_verify_sig: expected hlen %d, got %d",
                     IMAGE_HASH_SIZE, hlen);
        FIH_SET(fih_rc, FIH_FAILURE);
        goto out;
    }

    FIH_CALL(bootutil_verify, fih_rc, hash, IMAGE_HASH_SIZE, sig,
             slen, key_id);

out:
    FIH_RET(fih_rc);
}

/* Image verification function.
 * The function directly calls bootutil_verify to verify signature
 * of image.
 */
fih_ret
bootutil_verify_img(uint8_t *img, uint32_t size,
                    uint8_t *sig, size_t slen,
                    uint8_t key_id)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("bootutil_verify_img: RSA key_id %d", (int)key_id);

    FIH_CALL(bootutil_verify, fih_rc, img, size, sig,
             slen, key_id);

    FIH_RET(fih_rc);
}

#endif /* MCUBOOT_SIGN_RSA */
