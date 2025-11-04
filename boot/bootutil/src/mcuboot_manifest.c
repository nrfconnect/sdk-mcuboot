/*
 *  Copyright (c) 2025 Nordic Semiconductor ASA
 *
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <bootutil/mcuboot_manifest.h>

#if defined(MCUBOOT_MANIFEST_UPDATES) || defined(CONFIG_NCS_MCUBOOT_MANIFEST_UPDATES)

#ifndef MCUBOOT_MANIFEST_IMAGE_INDEX
#ifdef CONFIG_NCS_MCUBOOT_MANIFEST_IMAGE_INDEX
#define MCUBOOT_MANIFEST_IMAGE_INDEX CONFIG_NCS_MCUBOOT_MANIFEST_IMAGE_INDEX
#else
#error "MCUBOOT_MANIFEST_IMAGE_INDEX must be defined when MCUBOOT_MANIFEST_UPDATES is enabled"
#endif
#endif

bool bootutil_verify_manifest(const struct mcuboot_manifest *manifest)
{
    if (manifest == NULL) {
        return false;
    }

    /* Currently only the simplest manifest format is supported */
    if (manifest->format != 0x1) {
        return false;
    }

    if (manifest->image_count != MCUBOOT_IMAGE_NUMBER - 1) {
        return false;
    }

    return true;
}

bool bootutil_verify_manifest_image_hash(const struct mcuboot_manifest *manifest,
                                                       const uint8_t *exp_hash, uint32_t image_index)
{
    if (!bootutil_verify_manifest(manifest)) {
        return false;
    }

    if (image_index >= MCUBOOT_IMAGE_NUMBER) {
        return false;
    }

    if (image_index < MCUBOOT_MANIFEST_IMAGE_INDEX) {
        if (memcmp(exp_hash, manifest->image_hash[image_index], IMAGE_HASH_SIZE) == 0) {
            return true;
        }
    } else if (image_index > MCUBOOT_MANIFEST_IMAGE_INDEX) {
        if (memcmp(exp_hash, manifest->image_hash[image_index - 1], IMAGE_HASH_SIZE) == 0) {
            return true;
        }
    }

    return false;
}

#endif /* MCUBOOT_MANIFEST_UPDATES || CONFIG_NCS_MCUBOOT_MANIFEST_UPDATES */
