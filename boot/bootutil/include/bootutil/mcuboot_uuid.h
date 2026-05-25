/*
 *  Copyright (c) 2025 Nordic Semiconductor ASA
 *
 *  SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MCUBOOT_UUID_H__
#define __MCUBOOT_UUID_H__

/**
 * @file mcuboot_uuid.h
 *
 * @note A vendor ID as well as class ID values may be statically generated
 *       using CMake, based on the vendor domain name as well as product name.
 *       It is advised to use vendor ID as an input while generating device
 *       class ID to avoid collisions between UUIDs from two different vendors.
 */

#include <stdint.h>
#include "bootutil/fault_injection_hardening.h"
#include <flash_map_backend/flash_map_backend.h>

#ifdef __cplusplus
extern "C" {
#endif


/** The 128-bit UUID, used for identifying vendors as well as image classes. */
struct image_uuid {
	uint8_t raw[16];
};

/**
 * @brief Initialises the UUID module.
 *
 * @return FIH_SUCCESS on success
 */
fih_ret boot_uuid_init(void);

/**
 * @brief Check if the specified vendor UUID is allowed for a given image.
 *
 * @param[in] fap       Pointer to the flash area structure.
 * @param[in] uuid_vid  The reference to the image's vendor ID value.
 *
 * @return FIH_SUCCESS on success.
 */
fih_ret boot_uuid_vid_match(const struct flash_area *fap, const struct image_uuid *uuid_vid);

/**
 * @brief Check if the specified image class UUID is allowed for a given image.
 *
 * @param[in] fap       Pointer to the flash area structure.
 * @param[in] uuid_cid  The reference to the image's class ID value.
 *
 * @return FIH_SUCCESS on success
 */
fih_ret boot_uuid_cid_match(const struct flash_area *fap, const struct image_uuid *uuid_cid);

/**
 * @brief Find the image index and partition index for a given class ID and vendor ID.
 *
 * @param[in]  cid              The class ID to search for.
 * @param[in]  vid              The vendor ID to search for.
 * @param[out] image_index      The index of the image found.
 * @param[out] partition_index  The index of the partition found.
 *
 * @retval 0 on success,
 * @retval -EINVAL if the input parameters are invalid.
 * @retval -ENOENT if the image or partition is not found.
 */
int boot_uuid_find_image(const struct image_uuid *cid, const struct image_uuid *vid,
			 size_t *image_index, size_t *partition_index);

#ifdef __cplusplus
}
#endif

#endif /* __MCUBOOT_UUID_H__ */
