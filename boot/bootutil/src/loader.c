/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2016-2020 Linaro LTD
 * Copyright (c) 2016-2019 JUUL Labs
 * Copyright (c) 2019-2023 Arm Limited
 * Copyright (c) 2024-2025 Nordic Semiconductor ASA
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

/**
 * This file provides an interface to the boot loader.  Functions defined in
 * this file should only be called while the boot loader is running.
 */

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "flash_map_backend/flash_map_backend.h"
#include "bootutil/bootutil.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "swap_priv.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/security_cnt.h"
#include "bootutil/boot_record.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/ramload.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/mcuboot_status.h"

#if defined(MCUBOOT_DECOMPRESS_IMAGES)
#include <nrf_compress/implementation.h>
#include <compression/decompression.h>
#endif

#ifdef __ZEPHYR__
#include <zephyr/sys/reboot.h>
#if defined(CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS)
#include <zephyr/kernel.h>
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS */
#endif

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && defined(PM_CPUNET_B0N_ADDRESS) && defined(CONFIG_PCD_APP)
#include <dfu/pcd.h>
#ifdef CONFIG_PCD_READ_NETCORE_APP_VERSION
#include <fw_info_bare.h>
int pcd_version_cmp_net(const struct flash_area *fap, struct image_header *hdr);
#endif
#endif

#ifdef MCUBOOT_ENC_IMAGES
#include "bootutil/enc_key.h"
#endif

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
#include <os/os_malloc.h>
#endif

#include "mcuboot_config/mcuboot_config.h"

#if defined(CONFIG_BOOT_KEYS_REVOCATION)
#include "bootutil/key_revocation.h"
#endif

BOOT_LOG_MODULE_DECLARE(mcuboot);

static struct boot_loader_state boot_data;
#ifdef PM_S1_ADDRESS
static bool owner_nsib[BOOT_IMAGE_NUMBER] = {false};
#endif

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO) || defined(MCUBOOT_DATA_SHARING)
static struct image_max_size image_max_sizes[BOOT_IMAGE_NUMBER] = {0};
#endif

#if (!defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)) || \
defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO)
#if !defined(__BOOTSIM__)
/* Used for holding static buffers in multiple functions to work around issues
 * in older versions of gcc (e.g. 4.8.4)
 */
struct sector_buffer_t {
    boot_sector_t primary[BOOT_IMAGE_NUMBER][BOOT_MAX_IMG_SECTORS];
    boot_sector_t secondary[BOOT_IMAGE_NUMBER][BOOT_MAX_IMG_SECTORS];
#if MCUBOOT_SWAP_USING_SCRATCH
    boot_sector_t scratch[BOOT_MAX_IMG_SECTORS];
#endif
};

static struct sector_buffer_t sector_buffers;
#endif
#endif

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 && defined(MCUBOOT_OVERWRITE_ONLY) && \
    defined(MCUBOOT_DOWNGRADE_PREVENTION)
/* s0/s1 package version of the current MCUboot image */
static const struct image_version mcuboot_s0_s1_image_version = {
    .iv_major = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MAJOR,
    .iv_minor = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_MINOR,
    .iv_revision = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_REVISION,
    .iv_build_num = CONFIG_MCUBOOT_MCUBOOT_S0_S1_VERSION_BUILD_NUMBER,
};
#endif

#if (BOOT_IMAGE_NUMBER > 1)
#define IMAGES_ITER(x) for ((x) = 0; (x) < BOOT_IMAGE_NUMBER; ++(x))
#else
#define IMAGES_ITER(x)
#endif

/*
 * This macro allows some control on the allocation of local variables.
 * When running natively on a target, we don't want to allocated huge
 * variables on the stack, so make them global instead. For the simulator
 * we want to run as many threads as there are tests, and it's safer
 * to just make those variables stack allocated.
 */
#if !defined(__BOOTSIM__)
#define TARGET_STATIC static
#else
#define TARGET_STATIC
#endif

#if BOOT_MAX_ALIGN > 1024
#define BUF_SZ BOOT_MAX_ALIGN
#else
#define BUF_SZ 1024
#endif

#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_ENC_IMAGES)
#define BOOT_COPY_REGION(state, fap_pri, fap_sec, pri_off, sec_off, sz, sector_off) \
        boot_copy_region(state, fap_pri, fap_sec, pri_off, sec_off, sz, sector_off)
#else
#define BOOT_COPY_REGION(state, fap_pri, fap_sec, pri_off, sec_off, sz, sector_off) \
        boot_copy_region(state, fap_pri, fap_sec, pri_off, sec_off, sz)
#endif

struct boot_loader_state *boot_get_loader_state(void)
{
    return &boot_data;
}

static int
boot_read_image_headers(struct boot_loader_state *state, bool require_all,
        struct boot_status *bs)
{
    int rc;
    int i;

    for (i = 0; i < BOOT_NUM_SLOTS; i++) {
        rc = BOOT_HOOK_CALL(boot_read_image_header_hook, BOOT_HOOK_REGULAR,
                            BOOT_CURR_IMG(state), i, boot_img_hdr(state, i));
        if (rc == BOOT_HOOK_REGULAR)
        {
            rc = boot_read_image_header(state, i, boot_img_hdr(state, i), bs);
        }
        if (rc != 0) {
            /* If `require_all` is set, fail on any single fail, otherwise
             * if at least the first slot's header was read successfully,
             * then the boot loader can attempt a boot.
             *
             * Failure to read any headers is a fatal error.
             */
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
            /* Patch needed for NCS. The primary slot of the second image
             * (image 1) will not contain a valid image header until an upgrade
             * of mcuboot has happened (filling S1 with the new version).
             */
            if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER && i == 0) {
                continue;
            }
#endif /* CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 */
            if (i > 0 && !require_all) {
                return 0;
            } else {
                return rc;
            }
        }
    }

    return 0;
}

/**
 * Saves boot status and shared data for current image.
 *
 * @param  state        Boot loader status information.
 * @param  active_slot  Index of the slot will be loaded for current image.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_add_shared_data(struct boot_loader_state *state,
                     uint8_t active_slot)
{
#if defined(MCUBOOT_MEASURED_BOOT) || defined(MCUBOOT_DATA_SHARING)
    int rc;

#ifdef MCUBOOT_MEASURED_BOOT
    rc = boot_save_boot_status(BOOT_CURR_IMG(state),
                                boot_img_hdr(state, active_slot),
                                BOOT_IMG_AREA(state, active_slot));
    if (rc != 0) {
        BOOT_LOG_ERR("Failed to add image data to shared area");
        return rc;
    }
#endif /* MCUBOOT_MEASURED_BOOT */

#ifdef MCUBOOT_DATA_SHARING
    rc = boot_save_shared_data(boot_img_hdr(state, active_slot),
                                BOOT_IMG_AREA(state, active_slot),
                                active_slot, image_max_sizes);
    if (rc != 0) {
        BOOT_LOG_ERR("Failed to add data to shared memory area.");
        return rc;
    }
#endif /* MCUBOOT_DATA_SHARING */

    return 0;

#else /* MCUBOOT_MEASURED_BOOT || MCUBOOT_DATA_SHARING */
    (void) (state);
    (void) (active_slot);

    return 0;
#endif
}

/**
 * Fills rsp to indicate how booting should occur.
 *
 * @param  state        Boot loader status information.
 * @param  rsp          boot_rsp struct to fill.
 */
static void
fill_rsp(struct boot_loader_state *state, struct boot_rsp *rsp)
{
    uint32_t active_slot;

#if (BOOT_IMAGE_NUMBER > 1)
    /* Always boot from the first enabled image. */
    BOOT_CURR_IMG(state) = 0;
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
        if (!state->img_mask[BOOT_CURR_IMG(state)]) {
            break;
        }
    }
    /* At least one image must be active, otherwise skip the execution */
    if (BOOT_CURR_IMG(state) >= BOOT_IMAGE_NUMBER) {
        return;
    }
#endif

#if defined(MCUBOOT_DIRECT_XIP) || defined(MCUBOOT_RAM_LOAD)
    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
#else
    active_slot = BOOT_PRIMARY_SLOT;
#endif

    rsp->br_flash_dev_id = flash_area_get_device_id(BOOT_IMG_AREA(state, active_slot));
    rsp->br_image_off = boot_img_slot_off(state, active_slot);
    rsp->br_hdr = boot_img_hdr(state, active_slot);
}

/**
 * Closes all flash areas.
 *
 * @param  state    Boot loader status information.
 */
static void
close_all_flash_areas(struct boot_loader_state *state)
{
    uint32_t slot;

    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif
#if MCUBOOT_SWAP_USING_SCRATCH
        flash_area_close(BOOT_SCRATCH_AREA(state));
#endif
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            flash_area_close(BOOT_IMG_AREA(state, BOOT_NUM_SLOTS - 1 - slot));
        }
    }
}

#if (BOOT_IMAGE_NUMBER > 1) || \
    defined(MCUBOOT_DIRECT_XIP) || \
    defined(MCUBOOT_RAM_LOAD) || \
    defined(MCUBOOT_DOWNGRADE_PREVENTION)
/**
 * Compare image version numbers
 *
 * By default, the comparison does not take build number into account.
 * Enable MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER to take the build number into account.
 *
 * @param ver1           Pointer to the first image version to compare.
 * @param ver2           Pointer to the second image version to compare.
 *
 * @retval -1           If ver1 is less than ver2.
 * @retval 0            If the image version numbers are equal.
 * @retval 1            If ver1 is greater than ver2.
 */
static int
boot_version_cmp(const struct image_version *ver1,
                 const struct image_version *ver2)
{
#if !defined(MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER)
    BOOT_LOG_DBG("boot_version_cmp: ver1 %u.%u.%u vs ver2 %u.%u.%u",
                 (unsigned)ver1->iv_major, (unsigned)ver1->iv_minor,
                 (unsigned)ver1->iv_revision, (unsigned)ver2->iv_major,
                 (unsigned)ver2->iv_minor, (unsigned)ver2->iv_revision);
#else
    BOOT_LOG_DBG("boot_version_cmp: ver1 %u.%u.%u.%u vs ver2 %u.%u.%u.%u",
                 (unsigned)ver1->iv_major, (unsigned)ver1->iv_minor,
                 (unsigned)ver1->iv_revision, (unsigned)ver1->iv_build_num,
                 (unsigned)ver2->iv_major, (unsigned)ver2->iv_minor,
                 (unsigned)ver2->iv_revision, (unsigned)ver2->iv_build_num);
#endif

    if (ver1->iv_major > ver2->iv_major) {
        return 1;
    }
    if (ver1->iv_major < ver2->iv_major) {
        return -1;
    }
    /* The major version numbers are equal, continue comparison. */
    if (ver1->iv_minor > ver2->iv_minor) {
        return 1;
    }
    if (ver1->iv_minor < ver2->iv_minor) {
        return -1;
    }
    /* The minor version numbers are equal, continue comparison. */
    if (ver1->iv_revision > ver2->iv_revision) {
        return 1;
    }
    if (ver1->iv_revision < ver2->iv_revision) {
        return -1;
    }

#if defined(MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER)
    /* The revisions are equal, continue comparison. */
    if (ver1->iv_build_num > ver2->iv_build_num) {
        return 1;
    }
    if (ver1->iv_build_num < ver2->iv_build_num) {
        return -1;
    }
#endif

    return 0;
}
#endif

#if (!defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)) || \
defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO)
static int
boot_initialize_area(struct boot_loader_state *state, int flash_area)
{
    uint32_t num_sectors = BOOT_MAX_IMG_SECTORS;
    boot_sector_t *out_sectors;
    uint32_t *out_num_sectors;
    int rc;

    num_sectors = BOOT_MAX_IMG_SECTORS;

    if (flash_area == FLASH_AREA_IMAGE_PRIMARY(BOOT_CURR_IMG(state))) {
        out_sectors = BOOT_IMG(state, BOOT_PRIMARY_SLOT).sectors;
        out_num_sectors = &BOOT_IMG(state, BOOT_PRIMARY_SLOT).num_sectors;
    } else if (flash_area == FLASH_AREA_IMAGE_SECONDARY(BOOT_CURR_IMG(state))) {
        out_sectors = BOOT_IMG(state, BOOT_SECONDARY_SLOT).sectors;
        out_num_sectors = &BOOT_IMG(state, BOOT_SECONDARY_SLOT).num_sectors;
#if MCUBOOT_SWAP_USING_SCRATCH
    } else if (flash_area == FLASH_AREA_IMAGE_SCRATCH) {
        out_sectors = state->scratch.sectors;
        out_num_sectors = &state->scratch.num_sectors;
#endif
    } else {
        return BOOT_EFLASH;
    }

#ifdef MCUBOOT_USE_FLASH_AREA_GET_SECTORS
    rc = flash_area_get_sectors(flash_area, &num_sectors, out_sectors);
#else
    _Static_assert(sizeof(int) <= sizeof(uint32_t), "Fix needed");
    rc = flash_area_to_sectors(flash_area, (int *)&num_sectors, out_sectors);
#endif /* defined(MCUBOOT_USE_FLASH_AREA_GET_SECTORS) */
    if (rc != 0) {
        return rc;
    }
    *out_num_sectors = num_sectors;
    return 0;
}
#endif

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO)
static int
boot_read_sectors_recovery(struct boot_loader_state *state)
{
    uint8_t image_index;
    int rc;

    image_index = BOOT_CURR_IMG(state);

    rc = boot_initialize_area(state, FLASH_AREA_IMAGE_PRIMARY(image_index));
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    rc = boot_initialize_area(state, FLASH_AREA_IMAGE_SECONDARY(image_index));
    if (rc != 0) {
        /* We need to differentiate from the primary image issue */
        return BOOT_EFLASH_SEC;
    }

    return 0;
}
#endif


#if (BOOT_IMAGE_NUMBER > 1)

static int
boot_verify_slot_dependencies(struct boot_loader_state *state, uint32_t slot);

/**
 * Check the image dependency whether it is satisfied and modify
 * the swap type if necessary.
 *
 * @param dep               Image dependency which has to be verified.
 *
 * @return                  0 on success; nonzero on failure.
 */
static int
boot_verify_slot_dependency(struct boot_loader_state *state,
                            struct image_dependency *dep)
{
    struct image_version *dep_version;
    size_t dep_slot;
    int rc;

    /* Determine the source of the image which is the subject of
     * the dependency and get it's version. */
#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
    uint8_t swap_type = state->swap_type[dep->image_id];
    dep_slot = BOOT_IS_UPGRADE(swap_type) ? BOOT_SECONDARY_SLOT
                                          : BOOT_PRIMARY_SLOT;
#elif defined(MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER)
    switch(dep->slot) {
        case VERSION_DEP_SLOT_ACTIVE:
            dep_slot = state->slot_usage[dep->image_id].active_slot;
            break;
        case VERSION_DEP_SLOT_PRIMARY:
            dep_slot = BOOT_PRIMARY_SLOT;
            break;
        case VERSION_DEP_SLOT_SECONDARY:
            dep_slot = BOOT_SECONDARY_SLOT;
            break;
        default:
            return -1;
    }

    if (!state->slot_usage[dep->image_id].slot_available[dep_slot]) {
        return -1;
    }
#else
    dep_slot = state->slot_usage[dep->image_id].active_slot;
#endif

    dep_version = &state->imgs[dep->image_id][dep_slot].hdr.ih_ver;

    rc = boot_version_cmp(dep_version, &dep->image_min_version);
#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
    if (rc < 0) {
        /* Dependency not satisfied.
         * Modify the swap type to decrease the version number of the image
         * (which will be located in the primary slot after the boot process),
         * consequently the number of unsatisfied dependencies will be
         * decreased or remain the same.
         */
        switch (BOOT_SWAP_TYPE(state)) {
        case BOOT_SWAP_TYPE_TEST:
        case BOOT_SWAP_TYPE_PERM:
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
            break;
        case BOOT_SWAP_TYPE_NONE:
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_REVERT;
            break;
        default:
            break;
        }
    } else {
        /* Dependency satisfied. */
        rc = 0;
    }
#else
  if (rc >= 0) {
        /* Dependency satisfied. */
        rc = 0;
    }
#endif

#ifdef MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER
    if (rc == 0) {
        switch(dep->slot) {
            case VERSION_DEP_SLOT_PRIMARY:
                state->slot_usage[dep->image_id].slot_available[BOOT_PRIMARY_SLOT] = true;
                state->slot_usage[dep->image_id].slot_available[BOOT_SECONDARY_SLOT] = false;
                state->slot_usage[dep->image_id].active_slot = BOOT_PRIMARY_SLOT;
                break;
            case VERSION_DEP_SLOT_SECONDARY:
                state->slot_usage[dep->image_id].slot_available[BOOT_PRIMARY_SLOT] = false;
                state->slot_usage[dep->image_id].slot_available[BOOT_SECONDARY_SLOT] = true;
                state->slot_usage[dep->image_id].active_slot = BOOT_SECONDARY_SLOT;
                break;
            case VERSION_DEP_SLOT_ACTIVE:
            default:
                break;
        }
    }
#endif /* MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER */

return rc;
}

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
/**
 * Iterate over all the images and verify whether the image dependencies in the
 * TLV area are all satisfied and update the related swap type if necessary.
 */
static int
boot_verify_dependencies(struct boot_loader_state *state)
{
    int rc = -1;
    uint8_t slot;

    BOOT_CURR_IMG(state) = 0;
    while (BOOT_CURR_IMG(state) < BOOT_IMAGE_NUMBER) {
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            BOOT_CURR_IMG(state)++;
            continue;
        }
        if (BOOT_SWAP_TYPE(state) != BOOT_SWAP_TYPE_NONE &&
            BOOT_SWAP_TYPE(state) != BOOT_SWAP_TYPE_FAIL) {
            slot = BOOT_SECONDARY_SLOT;
        } else {
            slot = BOOT_PRIMARY_SLOT;
        }

        rc = boot_verify_slot_dependencies(state, slot);
        if (rc == 0) {
            /* All dependencies've been satisfied, continue with next image. */
            BOOT_CURR_IMG(state)++;
        } else if (rc == BOOT_EBADIMAGE) {
            /* Cannot upgrade due to non-met dependencies, so disable all
             * image upgrades.
             */
            for (int idx = 0; idx < BOOT_IMAGE_NUMBER; idx++) {
                BOOT_CURR_IMG(state) = idx;
                BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
            }
            break;
        } else {
		/* Other error happened, images are inconsistent */
		return rc;
	}
    }
    return rc;
}
#else

/**
 * Checks the dependency of all the active slots. If an image found with
 * invalid or not satisfied dependencies the image is removed from SRAM (in
 * case of MCUBOOT_RAM_LOAD strategy) and its slot is set to unavailable.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 if dependencies are met; nonzero otherwise.
 */
static int
boot_verify_dependencies(struct boot_loader_state *state)
{
    int rc = -1;
    uint32_t active_slot;

    IMAGES_ITER(BOOT_CURR_IMG(state)) {
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
        active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
        rc = boot_verify_slot_dependencies(state, active_slot);
        if (rc != 0) {
            /* Dependencies not met or invalid dependencies. */

#ifdef MCUBOOT_RAM_LOAD
            boot_remove_image_from_sram(state);
#endif /* MCUBOOT_RAM_LOAD */

            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;

            return rc;
        }
    }

    return rc;
}
#endif

/**
 * Read all dependency TLVs of an image from the flash and verify
 * one after another to see if they are all satisfied.
 *
 * @param slot              Image slot number.
 *
 * @return                  0 on success; nonzero on failure.
 */
static int
boot_verify_slot_dependencies(struct boot_loader_state *state, uint32_t slot)
{
    const struct flash_area *fap;
    struct image_tlv_iter it;
    struct image_dependency dep;
    uint32_t off;
    uint16_t len;
    int rc;

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    BOOT_LOG_DBG("boot_verify_slot_dependencies");
#if defined(MCUBOOT_SWAP_USING_OFFSET)
    it.start_off = boot_get_state_secondary_offset(state, fap);
#endif

    rc = bootutil_tlv_iter_begin(&it, boot_img_hdr(state, slot), fap,
            IMAGE_TLV_DEPENDENCY, true);
    if (rc != 0) {
        goto done;
    }

    while (true) {
        rc = bootutil_tlv_iter_next(&it, &off, &len, NULL);
        if (rc < 0) {
            return -1;
        } else if (rc > 0) {
            rc = 0;
            break;
        }

        if (len != sizeof(dep)) {
            rc = BOOT_EBADIMAGE;
            goto done;
        }

        rc = LOAD_IMAGE_DATA(boot_img_hdr(state, slot),
                             fap, off, &dep, len);
        if (rc != 0) {
            BOOT_LOG_DBG("boot_verify_slot_dependencies: error %d reading dependency %p %d %d",
                         rc, fap, off, len);
            rc = BOOT_EFLASH;
            goto done;
        }

        if (dep.image_id >= BOOT_IMAGE_NUMBER) {
            rc = BOOT_EBADARGS;
            goto done;
        }

#ifdef MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER
        /* Validate against possible dependency slot values. */
        switch(dep->slot) {
            case VERSION_DEP_SLOT_ACTIVE:
            case VERSION_DEP_SLOT_PRIMARY:
            case VERSION_DEP_SLOT_SECONDARY:
                break;
            default:
                rc = BOOT_EBADARGS;
                goto done;
        }
#endif /* MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER */

        /* Verify dependency and modify the swap type if not satisfied. */
        rc = boot_verify_slot_dependency(state, &dep);
        if (rc != 0) {
            BOOT_LOG_DBG("boot_verify_slot_dependencies: not satisfied");
            /* Dependency not satisfied */
            goto done;
        }
    }

done:
    return rc;
}

#endif /* (BOOT_IMAGE_NUMBER > 1) */

#if !defined(MCUBOOT_DIRECT_XIP)

#if !defined(MCUBOOT_RAM_LOAD)
static uint32_t
boot_write_sz(struct boot_loader_state *state)
{
    uint32_t elem_sz;
#if MCUBOOT_SWAP_USING_SCRATCH
    uint32_t align;
#endif

    /* Figure out what size to write update status update as.  The size depends
     * on what the minimum write size is for scratch area, active image slot.
     * We need to use the bigger of those 2 values.
     */
    elem_sz = flash_area_align(BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT));
#if MCUBOOT_SWAP_USING_SCRATCH
    align = flash_area_align(BOOT_SCRATCH_AREA(state));
    if (align > elem_sz) {
        elem_sz = align;
    }
#endif

    return elem_sz;
}

/**
 * Determines the sector layout of both image slots and the scratch area.
 * This information is necessary for calculating the number of bytes to erase
 * and copy during an image swap.  The information collected during this
 * function is used to populate the state.
 */
static int
boot_read_sectors(struct boot_loader_state *state)
{
    uint8_t image_index;
    int rc;

    image_index = BOOT_CURR_IMG(state);

    rc = boot_initialize_area(state, FLASH_AREA_IMAGE_PRIMARY(image_index));
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    rc = boot_initialize_area(state, FLASH_AREA_IMAGE_SECONDARY(image_index));
    if (rc != 0) {
        /* We need to differentiate from the primary image issue */
        return BOOT_EFLASH_SEC;
    }

#if MCUBOOT_SWAP_USING_SCRATCH
    rc = boot_initialize_area(state, FLASH_AREA_IMAGE_SCRATCH);
    if (rc != 0) {
        return BOOT_EFLASH;
    }
#endif

    BOOT_WRITE_SZ(state) = boot_write_sz(state);

    return 0;
}

void
boot_status_reset(struct boot_status *bs)
{
#ifdef MCUBOOT_ENC_IMAGES
    memset(&bs->enckey, 0xff, BOOT_NUM_SLOTS * BOOT_ENC_KEY_ALIGN_SIZE);
#if MCUBOOT_SWAP_SAVE_ENCTLV
    memset(&bs->enctlv, 0xff, BOOT_NUM_SLOTS * BOOT_ENC_TLV_ALIGN_SIZE);
#endif
#endif /* MCUBOOT_ENC_IMAGES */

    bs->use_scratch = 0;
    bs->swap_size = 0;
    bs->source = 0;

#if defined(MCUBOOT_SWAP_USING_OFFSET)
    bs->op = BOOT_STATUS_OP_SWAP;
#else
    bs->op = BOOT_STATUS_OP_MOVE;
#endif
    bs->idx = BOOT_STATUS_IDX_0;
    bs->state = BOOT_STATUS_STATE_0;
    bs->swap_type = BOOT_SWAP_TYPE_NONE;
}

bool
boot_status_is_reset(const struct boot_status *bs)
{
    return (
#if defined(MCUBOOT_SWAP_USING_OFFSET)
            bs->op == BOOT_STATUS_OP_SWAP &&
#else
            bs->op == BOOT_STATUS_OP_MOVE &&
#endif
            bs->idx == BOOT_STATUS_IDX_0 &&
            bs->state == BOOT_STATUS_STATE_0);
}

/**
 * Writes the supplied boot status to the flash file system.  The boot status
 * contains the current state of an in-progress image copy operation.
 *
 * @param bs                    The boot status to write.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
boot_write_status(const struct boot_loader_state *state, struct boot_status *bs)
{
    const struct flash_area *fap;
    uint32_t off;
    int rc = 0;
    uint8_t buf[BOOT_MAX_ALIGN];
    uint32_t align;
    uint8_t erased_val;

    /* NOTE: The first sector copied (that is the last sector on slot) contains
     *       the trailer. Since in the last step the primary slot is erased, the
     *       first two status writes go to the scratch which will be copied to
     *       the primary slot!
     */

#if MCUBOOT_SWAP_USING_SCRATCH
    if (bs->use_scratch) {
        /* Write to scratch. */
        fap = state->scratch.area;
    } else {
#endif
        /* Write to the primary slot. */
        fap = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);
#if MCUBOOT_SWAP_USING_SCRATCH
    }
#endif

    off = boot_status_off(fap) +
          boot_status_internal_off(bs, BOOT_WRITE_SZ(state));
    align = flash_area_align(fap);
    erased_val = flash_area_erased_val(fap);
    memset(buf, erased_val, BOOT_MAX_ALIGN);
    buf[0] = bs->state;

    BOOT_LOG_DBG("writing swap status; fa_id=%d off=0x%lx (0x%lx)",
                 flash_area_get_id(fap), (unsigned long)off,
                 (unsigned long)flash_area_get_off(fap) + off);

    rc = flash_area_write(fap, off, buf, align);
    if (rc != 0) {
        rc = BOOT_EFLASH;
    }

    return rc;
}
#endif /* !MCUBOOT_RAM_LOAD */
#endif /* !MCUBOOT_DIRECT_XIP */

/*
 * Validate image hash/signature and optionally the security counter in a slot.
 */
static fih_ret
boot_image_check(struct boot_loader_state *state, struct image_header *hdr,
                 const struct flash_area *fap, struct boot_status *bs)
{
    TARGET_STATIC uint8_t tmpbuf[BOOT_TMPBUF_SZ];
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

#if (BOOT_IMAGE_NUMBER == 1)
    (void)state;
#endif

    (void)bs;
    (void)rc;

    /* In the case of ram loading the image has already been decrypted as it is
     * decrypted when copied in ram
     */
#if defined(MCUBOOT_ENC_IMAGES) && !defined(MCUBOOT_RAM_LOAD)
    if (MUST_DECRYPT(fap, BOOT_CURR_IMG(state), hdr)) {
#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
        rc = boot_enc_load(state, 1, hdr, fap, bs, 0);
#else
        rc = boot_enc_load(state, 1, hdr, fap, bs);
#endif
        if (rc < 0) {
            FIH_RET(fih_rc);
        }
        if (rc == 0 && boot_enc_set_key(BOOT_CURR_ENC(state), 1, bs)) {
            FIH_RET(fih_rc);
        }
    }
#endif

    for (int i = 1; i <= CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT; i++ ) {
#if CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1
      BOOT_LOG_DBG("Image validation attempt %d/%d", i, CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT);
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1 */

#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
        FIH_CALL(bootutil_img_validate, fih_rc, state, hdr, fap, tmpbuf, BOOT_TMPBUF_SZ,
                NULL, 0, NULL, 0);
#else
        FIH_CALL(bootutil_img_validate, fih_rc, state, hdr, fap, tmpbuf, BOOT_TMPBUF_SZ,
                NULL, 0, NULL);
#endif

        if (FIH_EQ(fih_rc, FIH_SUCCESS)) {
#if CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1
          BOOT_LOG_DBG("Image validation attempt %d/%d success", i, CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT);
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1 */
          break;
        } else {
#if CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1
          BOOT_LOG_WRN("Image validation attempt %d/%d failure: %d",
                       i,
                       CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT, fih_rc);
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1 */

          if (i < CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT) {
#if defined(CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS)
#if CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1
            BOOT_LOG_DBG("Waiting %d ms before next attempt",
                         CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS);
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_COUNT > 1 */
            k_busy_wait(CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS * 1000);
#endif /* CONFIG_NRF_MCUBOOT_IMG_VALIDATE_ATTEMPT_WAIT_MS */
          }
        }
    }
    FIH_RET(fih_rc);
}

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
static fih_ret
split_image_check(struct image_header *app_hdr,
                  const struct flash_area *app_fap,
                  struct image_header *loader_hdr,
                  const struct flash_area *loader_fap)
{
    static void *tmpbuf;
    uint8_t loader_hash[32];
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    if (!tmpbuf) {
        tmpbuf = malloc(BOOT_TMPBUF_SZ);
        if (!tmpbuf) {
            goto out;
        }
    }

#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, loader_hdr, loader_fap,
             tmpbuf, BOOT_TMPBUF_SZ, NULL, 0, loader_hash, 0);
#else
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, loader_hdr, loader_fap,
             tmpbuf, BOOT_TMPBUF_SZ, NULL, 0, loader_hash);
#endif
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        FIH_RET(fih_rc);
    }

#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, app_hdr, app_fap,
             tmpbuf, BOOT_TMPBUF_SZ, loader_hash, 32, NULL, 0);
#else
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, app_hdr, app_fap,
             tmpbuf, BOOT_TMPBUF_SZ, loader_hash, 32, NULL);
#endif

out:
    FIH_RET(fih_rc);
}
#endif /* !MCUBOOT_DIRECT_XIP && !MCUBOOT_RAM_LOAD */

/*
 * Check that this is a valid header.  Valid means that the magic is
 * correct, and that the sizes/offsets are "sane".  Sane means that
 * there is no overflow on the arithmetic, and that the result fits
 * within the flash area we are in. Also check the flags in the image
 * and class the image as invalid if flags for encryption/compression
 * are present but these features are not enabled.
 */
static bool
boot_is_header_valid(const struct image_header *hdr, const struct flash_area *fap,
                     struct boot_loader_state *state)
{
    uint32_t size;

    (void)state;

    if (hdr->ih_magic != IMAGE_MAGIC) {
        return false;
    }

    if (!boot_u32_safe_add(&size, hdr->ih_img_size, hdr->ih_hdr_size)) {
        return false;
    }

#ifdef MCUBOOT_DECOMPRESS_IMAGES
    if (!MUST_DECOMPRESS(fap, BOOT_CURR_IMG(state), hdr)) {
#else
    if (1) {
#endif
        if (!boot_u32_safe_add(&size, size, hdr->ih_protect_tlv_size)) {
            return false;
        }
    }

    if (size >= flash_area_get_size(fap)) {
        return false;
    }

#if !defined(MCUBOOT_ENC_IMAGES)
    if (IS_ENCRYPTED(hdr)) {
        return false;
    }
#else
    if ((hdr->ih_flags & IMAGE_F_ENCRYPTED_AES128) &&
        (hdr->ih_flags & IMAGE_F_ENCRYPTED_AES256))
    {
        return false;
    }
#endif

#if !defined(MCUBOOT_DECOMPRESS_IMAGES)
    if (IS_COMPRESSED(hdr)) {
        return false;
    }
#else
    if (MUST_DECOMPRESS(fap, BOOT_CURR_IMG(state), hdr)) {
        if (!boot_is_compressed_header_valid(hdr, fap, state)) {
            return false;
        }
    }
#endif

    return true;
}

/*
 * Check that a memory area consists of a given value.
 */
static inline bool
boot_data_is_set_to(uint8_t val, void *data, size_t len)
{
    uint8_t i;
    uint8_t *p = (uint8_t *)data;
    for (i = 0; i < len; i++) {
        if (val != p[i]) {
            return false;
        }
    }
    return true;
}

static int
boot_check_header_erased(struct boot_loader_state *state, int slot)
{
    const struct flash_area *fap = NULL;
    struct image_header *hdr;
    uint8_t erased_val;

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    erased_val = flash_area_erased_val(fap);

    hdr = boot_img_hdr(state, slot);
    if (!boot_data_is_set_to(erased_val, &hdr->ih_magic, sizeof(hdr->ih_magic))) {
        return -1;
    }

    return 0;
}

#if defined(MCUBOOT_DIRECT_XIP)
/**
 * Check if image in slot has been set with specific ROM address to run from
 * and whether the slot starts at that address.
 *
 * @returns 0 if IMAGE_F_ROM_FIXED flag is not set;
 *          0 if IMAGE_F_ROM_FIXED flag is set and ROM address specified in
 *            header matches the slot address;
 *          1 if IMF_F_ROM_FIXED flag is set but ROM address specified in header
 *          does not match the slot address.
 */
static bool
boot_rom_address_check(struct boot_loader_state *state)
{
    uint32_t active_slot;
    const struct image_header *hdr;
    uint32_t f_off;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
    hdr = boot_img_hdr(state, active_slot);
    f_off = boot_img_slot_off(state, active_slot);

    if (hdr->ih_flags & IMAGE_F_ROM_FIXED && hdr->ih_load_addr != f_off) {
        BOOT_LOG_WRN("Image in %s slot at 0x%x has been built for offset 0x%x"\
                     ", skipping",
                     active_slot == 0 ? "primary" : "secondary", f_off,
                     hdr->ih_load_addr);

        /* If there is address mismatch, the image is not bootable from this
         * slot.
         */
        return 1;
    }
    return 0;
}
#endif

/*
 * Check that there is a valid image in a slot
 *
 * @returns
 *         FIH_SUCCESS                      if image was successfully validated
 *         FIH_NO_BOOTABLE_IMAGE            if no bootloable image was found
 *         FIH_FAILURE                      on any errors
 */
static fih_ret
boot_validate_slot(struct boot_loader_state *state, int slot,
                   struct boot_status *bs, int expected_swap_type)
{
    const struct flash_area *fap;
    struct image_header *hdr;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("boot_validate_slot: slot %d, expected_swap_type %d",
                 slot, expected_swap_type);

#if !defined(MCUBOOT_SWAP_USING_OFFSET)
    (void)expected_swap_type;
#endif

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    hdr = boot_img_hdr(state, slot);
    if (boot_check_header_erased(state, slot) == 0 ||
        (hdr->ih_flags & IMAGE_F_NON_BOOTABLE)) {
#if defined(MCUBOOT_SWAP_USING_SCRATCH) || defined(MCUBOOT_SWAP_USING_MOVE) || defined(MCUBOOT_SWAP_USING_OFFSET)
        /*
         * This fixes an issue where an image might be erased, but a trailer
         * be left behind. It can happen if the image is in the secondary slot
         * and did not pass validation, in which case the whole slot is erased.
         * If during the erase operation, a reset occurs, parts of the slot
         * might have been erased while some did not. The concerning part is
         * the trailer because it might disable a new image from being loaded
         * through mcumgr; so we just get rid of the trailer here, if the header
         * is erased.
         */
        if (slot != BOOT_PRIMARY_SLOT) {
            swap_scramble_trailer_sectors(state, fap);

#if defined(MCUBOOT_SWAP_USING_MOVE)
            if (bs->swap_type == BOOT_SWAP_TYPE_REVERT ||
                boot_swap_type_multi(BOOT_CURR_IMG(state)) == BOOT_SWAP_TYPE_REVERT) {
                const struct flash_area *fap_pri = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);

                assert(fap_pri != NULL);

                if (swap_scramble_trailer_sectors(state, fap_pri) == 0) {
                    BOOT_LOG_INF("Cleared image %d primary slot trailer due to stuck revert",
                                 BOOT_CURR_IMG(state));
                }
            }
#endif
        }
#endif

        /* No bootable image in slot; continue booting from the primary slot. */
        fih_rc = FIH_NO_BOOTABLE_IMAGE;
        goto out;
    }

#if defined(MCUBOOT_SWAP_USING_OFFSET)
    if (slot != BOOT_PRIMARY_SLOT && boot_status_is_reset(bs) &&
        (expected_swap_type == BOOT_SWAP_TYPE_TEST || expected_swap_type == BOOT_SWAP_TYPE_PERM)) {
        /* Check first sector to see if there is a magic header here, if so the update has likely
         * been loaded to the wrong sector and cannot be used
         */
        struct image_header first_sector_hdr;

        if (flash_area_read(fap, 0, &first_sector_hdr, sizeof(first_sector_hdr))) {
            FIH_RET(fih_rc);
        }

        if (first_sector_hdr.ih_magic == IMAGE_MAGIC) {
            BOOT_LOG_ERR("Secondary header magic detected in first sector, wrong upload address?");
            fih_rc = FIH_NO_BOOTABLE_IMAGE;
            goto check_validity;
        }
    }
#endif

#if defined(MCUBOOT_OVERWRITE_ONLY) && defined(MCUBOOT_DOWNGRADE_PREVENTION)
    if (slot != BOOT_PRIMARY_SLOT) {
        int rc;

        /* Check if version of secondary slot is sufficient */

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE) \
    && defined(CONFIG_PCD_APP) && defined(CONFIG_PCD_READ_NETCORE_APP_VERSION)
        if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER) {
            rc = pcd_version_cmp_net(fap, boot_img_hdr(state, BOOT_SECONDARY_SLOT));
        } else {
             rc = boot_version_cmp(
                                 &boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_ver,
                                 &boot_img_hdr(state, BOOT_PRIMARY_SLOT)->ih_ver);

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
            if (rc >= 0 && BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
                /* Also check the new version of MCUboot against that of the current s0/s1 MCUboot
                 * trailer version to prevent downgrades
                 */
                int version_check;

                version_check = boot_version_cmp(&boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_ver,
                                                 &mcuboot_s0_s1_image_version);

                /* Only update rc if the currently running version is newer */
                if (version_check < rc) {
                    rc = version_check;
                }
            }
#endif
        }
#else
	rc = boot_version_cmp(
			&boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_ver,
			&boot_img_hdr(state, BOOT_PRIMARY_SLOT)->ih_ver);

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
        if (rc >= 0 && BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
            /* Also check the new version of MCUboot against that of the current s0/s1 MCUboot
             * trailer version to prevent downgrades
             */
            int version_check;

            version_check = boot_version_cmp(&boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_ver,
                                             &mcuboot_s0_s1_image_version);

            /* Only update rc if the currently running version is newer */
            if (version_check < rc) {
                rc = version_check;
            }
        }
#endif
#endif
        if (rc < 0 && boot_check_header_erased(state, BOOT_PRIMARY_SLOT)) {
            BOOT_LOG_ERR("insufficient version in secondary slot");
            boot_scramble_slot(fap, slot);
            /* Image in the secondary slot does not satisfy version requirement.
             * Erase the image and continue booting from the primary slot.
             */
            fih_rc = FIH_NO_BOOTABLE_IMAGE;
            goto out;
        }
    }
#endif
    if (!boot_is_header_valid(hdr, fap, state)) {
        fih_rc = FIH_FAILURE;
    } else {
        BOOT_HOOK_CALL_FIH(boot_image_check_hook, FIH_BOOT_HOOK_REGULAR,
                           fih_rc, BOOT_CURR_IMG(state), slot);
        if (FIH_EQ(fih_rc, FIH_BOOT_HOOK_REGULAR)) {
            FIH_CALL(boot_image_check, fih_rc, state, hdr, fap, bs);
        }
    }
#if defined(MCUBOOT_SWAP_USING_OFFSET)
check_validity:
#endif
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        if ((slot != BOOT_PRIMARY_SLOT) || ARE_SLOTS_EQUIVALENT()) {
            boot_scramble_slot(fap, slot);
            /* Image is invalid, erase it to prevent further unnecessary
             * attempts to validate and boot it.
             */
        }

#if !defined(__BOOTSIM__)
        BOOT_LOG_ERR("Image in the %s slot is not valid!",
                     (slot == BOOT_PRIMARY_SLOT) ? "primary" : "secondary");
#endif
        fih_rc = FIH_NO_BOOTABLE_IMAGE;
        goto out;
    }

#if MCUBOOT_IMAGE_NUMBER > 1 && !defined(MCUBOOT_ENC_IMAGES) && defined(MCUBOOT_VERIFY_IMG_ADDRESS)
    /* Verify that the image in the secondary slot has a reset address
     * located in the primary slot. This is done to avoid users incorrectly
     * overwriting an application written to the incorrect slot.
     * This feature is only supported by ARM platforms.
     */
#if MCUBOOT_IMAGE_NUMBER >= 3
    /* Currently the MCUboot can be configured for up to 3 image, where image number 2 is
     * designated for XIP, where it is the second part of image stored in slots of image
     * 0. This part of image is not bootable, as the XIP setup is done by the app in
     * image 0 slot, and it does not carry the reset vector.
     */
    if (fap == state->imgs[2][BOOT_SECONDARY_SLOT].area) {
        goto out;
    }
#endif
    if (fap == BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT)) {
        const struct flash_area *pri_fa = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);
        struct image_header *secondary_hdr = boot_img_hdr(state, slot);
        uint32_t reset_value = 0;
        uint32_t reset_addr = secondary_hdr->ih_hdr_size + sizeof(reset_value);
        uint32_t min_addr, max_addr;
        bool check_addresses = false;

        if (flash_area_read(fap, reset_addr, &reset_value, sizeof(reset_value)) != 0) {
            fih_rc = FIH_NO_BOOTABLE_IMAGE;
            goto out;
        }

#ifdef PM_CPUNET_APP_ADDRESS
        /* The primary slot for the network core is emulated in RAM.
         * Its flash_area hasn't got relevant boundaries.
         * Therfore need to override its boundaries for the check.
         */
        if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER) {
            min_addr = PM_CPUNET_APP_ADDRESS;
            max_addr = PM_CPUNET_APP_ADDRESS + PM_CPUNET_APP_SIZE;
            check_addresses = true;
        } else
#endif
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
        if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
#if (CONFIG_NCS_IS_VARIANT_IMAGE)
            min_addr = PM_S0_ADDRESS;
            max_addr = (PM_S0_ADDRESS + PM_S0_SIZE);
#else
            min_addr = PM_S1_ADDRESS;
            max_addr = (PM_S1_ADDRESS + PM_S1_SIZE);
#endif
            check_addresses = true;
        } else
#endif
        if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER) {
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
#if (CONFIG_NCS_IS_VARIANT_IMAGE)
            min_addr = MIN(pri_fa->fa_off, PM_S0_ADDRESS);
            max_addr = MAX((pri_fa->fa_off + pri_fa->fa_size), (PM_S0_ADDRESS + PM_S0_SIZE));
#else
            min_addr = MIN(pri_fa->fa_off, PM_S1_ADDRESS);
            max_addr = MAX((pri_fa->fa_off + pri_fa->fa_size), (PM_S1_ADDRESS + PM_S1_SIZE));
#endif
#else
            min_addr = pri_fa->fa_off;
            max_addr = pri_fa->fa_off + pri_fa->fa_size;
#endif
            check_addresses = true;
        }

        if (check_addresses == true && (reset_value < min_addr || reset_value > max_addr)) {
            BOOT_LOG_ERR("Reset address of image in secondary slot is not in the primary slot");
            BOOT_LOG_ERR("Erasing image from secondary slot");

            /* The vector table in the image located in the secondary
             * slot does not target the primary slot. This might
             * indicate that the image was loaded to the wrong slot.
             *
             * Erase the image and continue booting from the primary slot.
             */
            boot_scramble_slot(fap, slot);
            fih_rc = FIH_NO_BOOTABLE_IMAGE;
            goto out;
        }
    }
#endif

out:
    FIH_RET(fih_rc);
}

#ifdef MCUBOOT_HW_ROLLBACK_PROT
/**
 * Checks if the specified image should have a security counter present on it or not
 *
 * @param image_index   Index of the image to check.
 *
 * @return              true if security counter should be present; false if otherwise
 */
fih_ret boot_nv_image_should_have_security_counter(uint32_t image_index)
{
#if defined(PM_S1_ADDRESS)
    if (owner_nsib[image_index]) {
        /*
         * Downgrade prevention on S0/S1 image is managed by NSIB, which is a software (not
         * hardware) check
         */
        return FIH_FAILURE;
    }
#endif

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER != -1
    if (image_index == CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER) {
        /*
         * Downgrade prevention on network core image is managed by NSIB which is a software (not
         * hardware) check
         */
        return FIH_FAILURE;
    }
#endif

    return FIH_SUCCESS;
}

/**
 * Updates the stored security counter value with the image's security counter
 * value which resides in the given slot, only if it's greater than the stored
 * value.
 *
 * @param state         Boot state where the current image's security counter will
 *                      be updated.
 * @param slot          Slot number of the image.
 * @param hdr_slot_idx  Index of the header in the state current image variable
 *                      containing the pointer to the image header structure of the
 *                      image that is currently stored in the given slot.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_update_security_counter(struct boot_loader_state *state, int slot, int hdr_slot_idx)
{
    const struct flash_area *fap = NULL;
    uint32_t img_security_cnt;
    int rc;

#if defined(PM_S1_ADDRESS)
    if (owner_nsib[BOOT_CURR_IMG(state)]) {
        /*
         * Downgrade prevention on S0/S1 image is managed by NSIB which is a software (not
         * hardware) check
         */
        return 0;
    }
#endif

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER != -1
    if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER) {
        /*
         * Downgrade prevention on network core image is managed by NSIB which is a software (not
         * hardware) check
         */
        return 0;
    }
#endif

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    rc = bootutil_get_img_security_cnt(state, hdr_slot_idx, fap, &img_security_cnt);
    if (rc != 0) {
        goto done;
    }

    rc = boot_nv_security_counter_update(BOOT_CURR_IMG(state), img_security_cnt);
    if (rc != 0) {
        goto done;
    }

done:
    return rc;
}
#endif /* MCUBOOT_HW_ROLLBACK_PROT */

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)

#if defined(CONFIG_MCUBOOT_CLEANUP_UNUSABLE_SECONDARY) &&\
(defined(PM_S1_ADDRESS) || defined(CONFIG_SOC_NRF5340_CPUAPP))

#define SEC_SLOT_VIRGIN 0
#define SEC_SLOT_TOUCHED 1
#define SEC_SLOT_ASSIGNED 2

static uint8_t sec_slot_assignment[MCUBOOT_IMAGE_NUMBER] = {0};

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
static inline void sec_slot_untouch(struct boot_loader_state *state)
{
    sec_slot_assignment[CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER] = SEC_SLOT_VIRGIN;
    sec_slot_assignment[CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] = SEC_SLOT_VIRGIN;
}
#else
static inline void sec_slot_untouch(struct boot_loader_state *state)
{
}
#endif

static inline void sec_slot_touch(struct boot_loader_state *state)
{
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
    if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
        if (sec_slot_assignment[CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER] == SEC_SLOT_VIRGIN) {
            sec_slot_assignment[CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER] = SEC_SLOT_TOUCHED;
        }
    } else if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER) {
        if (sec_slot_assignment[CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] == SEC_SLOT_VIRGIN) {
            sec_slot_assignment[CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] = SEC_SLOT_TOUCHED;
        }
    }
#endif

    if (sec_slot_assignment[BOOT_CURR_IMG(state)] == SEC_SLOT_VIRGIN) {
        sec_slot_assignment[BOOT_CURR_IMG(state)] = SEC_SLOT_TOUCHED;
    }
}

static inline void sec_slot_mark_assigned(struct boot_loader_state *state)
{
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
    if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
        sec_slot_assignment[CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER] = SEC_SLOT_ASSIGNED;
    } else if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER) {
        sec_slot_assignment[CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER] = SEC_SLOT_ASSIGNED;
    }
#endif

    sec_slot_assignment[BOOT_CURR_IMG(state)] = SEC_SLOT_ASSIGNED;
}

/**
 * Cleanup up all secondary slot which couldn't be assigned to any primary slot.
 *
 * This function erases content of each secondary slot which contains valid
 * header but couldn't be assigned to any of supported primary images.
 *
 * This function is supposed to be called after boot_validated_swap_type()
 * iterates over all the images in context_boot_go().
 */
static void sec_slot_cleanup_if_unusable(void)
{
    uint8_t idx;

    for (idx = 0; idx < MCUBOOT_IMAGE_NUMBER; idx++) {
        if (SEC_SLOT_TOUCHED == sec_slot_assignment[idx]) {
            const struct flash_area *secondary_fa;
            int rc;

            rc = flash_area_open(flash_area_id_from_multi_image_slot(idx, BOOT_SECONDARY_SLOT),
                                 &secondary_fa);
            if (!rc) {
                rc = flash_area_erase(secondary_fa, 0, secondary_fa->fa_size);
                if (!rc) {
                    BOOT_LOG_ERR("Cleaned-up secondary slot of image %d", idx);
                }
            }

            if (rc) {
                BOOT_LOG_ERR("Failed to clean-up secondary slot of image %d: %d", idx, rc);
            }
        }
    }
}
#else
static inline void sec_slot_untouch(struct boot_loader_state *state)
{
}
static inline void sec_slot_touch(struct boot_loader_state *state)
{
}
static inline void sec_slot_mark_assigned(struct boot_loader_state *state)
{
}
static inline void sec_slot_cleanup_if_unusable(void)
{
}
#endif /* defined(CONFIG_MCUBOOT_CLEANUP_UNUSABLE_SECONDARY) &&\
          defined(PM_S1_ADDRESS) || defined(CONFIG_SOC_NRF5340_CPUAPP) */

/**
 * Determines which swap operation to perform, if any.  If it is determined
 * that a swap operation is required, the image in the secondary slot is checked
 * for validity.  If the image in the secondary slot is invalid, it is erased,
 * and a swap type of "none" is indicated.
 *
 * @return                      The type of swap to perform (BOOT_SWAP_TYPE...)
 */
static int
boot_validated_swap_type(struct boot_loader_state *state,
                         struct boot_status *bs)
{
    int swap_type;
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    bool upgrade_valid = false;
#if defined(PM_S1_ADDRESS)
    owner_nsib[BOOT_CURR_IMG(state)] = false;
#endif

#if defined(PM_S1_ADDRESS) || defined(PM_CPUNET_B0N_ADDRESS)
    const struct flash_area *secondary_fa =
        BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT);
    struct image_header *hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);
    uint32_t reset_addr = 0;
    int rc = 0;
    /* Patch needed for NCS. Since image 0 (the app) and image 1 (the other
     * B1 slot S0 or S1) share the same secondary slot, we need to check
     * whether the update candidate in the secondary slot is intended for
     * image 0 or image 1 primary by looking at the address of the reset
     * vector. Note that there are good reasons for not using img_num from
     * the swap info.
     */

    if (hdr->ih_magic == IMAGE_MAGIC) {
        rc = flash_area_read(secondary_fa, hdr->ih_hdr_size +
                             sizeof(uint32_t), &reset_addr,
                             sizeof(reset_addr));
        if (rc != 0) {
            return BOOT_SWAP_TYPE_FAIL;
        }

        sec_slot_touch(state);

#ifdef PM_S1_ADDRESS
#ifdef PM_CPUNET_B0N_ADDRESS
        if(!(reset_addr >= PM_CPUNET_APP_ADDRESS && reset_addr < PM_CPUNET_APP_END_ADDRESS))
#endif
        {
            const struct flash_area *primary_fa;
            rc = flash_area_open(flash_area_id_from_multi_image_slot(
                                 BOOT_CURR_IMG(state), BOOT_PRIMARY_SLOT),
                                 &primary_fa);
            if (rc != 0) {
                return BOOT_SWAP_TYPE_FAIL;
            }

            /* Check start and end of primary slot for current image */
#if (CONFIG_NCS_IS_VARIANT_IMAGE)
            if (reset_addr >= PM_S0_ADDRESS && reset_addr <= (PM_S0_ADDRESS + PM_S0_SIZE)) {
#else
            if (reset_addr >= PM_S1_ADDRESS && reset_addr <= (PM_S1_ADDRESS + PM_S1_SIZE)) {
#endif
                if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_APPLICATION_IMAGE_NUMBER) {
                    /* This is not the s0/s1 upgrade image but the application image, pretend
                     * there is no image so the NSIB update can be loaded
                     */
                    return BOOT_SWAP_TYPE_NONE;
                }

                owner_nsib[BOOT_CURR_IMG(state)] = true;
#if (CONFIG_NCS_IS_VARIANT_IMAGE)
            } else if (reset_addr >= PM_S1_ADDRESS && reset_addr <= (PM_S1_ADDRESS + PM_S1_SIZE)) {
#else
            } else if (reset_addr >= PM_S0_ADDRESS && reset_addr <= (PM_S0_ADDRESS + PM_S0_SIZE)) {
#endif
                /* NSIB upgrade but for the wrong slot, must be erased */
                BOOT_LOG_ERR("Image in slot is for wrong s0/s1 image");
                flash_area_erase(secondary_fa, 0, secondary_fa->fa_size);
                sec_slot_untouch(state);
                BOOT_LOG_ERR("Cleaned-up secondary slot of image %d", BOOT_CURR_IMG(state));
                return BOOT_SWAP_TYPE_FAIL;
            } else if (reset_addr < primary_fa->fa_off || reset_addr > (primary_fa->fa_off + primary_fa->fa_size)) {
                /* The image in the secondary slot is not intended for any */
                return BOOT_SWAP_TYPE_NONE;
            }

            if ((primary_fa->fa_off == PM_S0_ADDRESS) || (primary_fa->fa_off == PM_S1_ADDRESS)) {
                owner_nsib[BOOT_CURR_IMG(state)] = true;
            }
        }
#endif /* PM_S1_ADDRESS */
        sec_slot_mark_assigned(state);
    }

#endif /* PM_S1_ADDRESS || PM_CPUNET_B0N_ADDRESS */

    swap_type = boot_swap_type_multi(BOOT_CURR_IMG(state));
    if (BOOT_IS_UPGRADE(swap_type)) {
        /* Boot loader wants to switch to the secondary slot.
         * Ensure image is valid.
         */
        FIH_CALL(boot_validate_slot, fih_rc, state, BOOT_SECONDARY_SLOT, bs, swap_type);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            if (FIH_EQ(fih_rc, FIH_NO_BOOTABLE_IMAGE)) {
                swap_type = BOOT_SWAP_TYPE_NONE;
            } else {
                swap_type = BOOT_SWAP_TYPE_FAIL;
            }
        } else {
            upgrade_valid = true;
        }

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && defined(PM_CPUNET_B0N_ADDRESS) \
    && !defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE) && defined(CONFIG_PCD_APP)
        /* If the update is valid, and it targets the network core: perform the
         * update and indicate to the caller of this function that no update is
         * available
         */
        if (upgrade_valid && reset_addr >= PM_CPUNET_APP_ADDRESS &&
            reset_addr < PM_CPUNET_APP_END_ADDRESS) {
            struct image_header *hdr = (struct image_header *)secondary_fa->fa_off;
            uint32_t vtable_addr = (uint32_t)hdr + hdr->ih_hdr_size;
            uint32_t *net_core_fw_addr = (uint32_t *)(vtable_addr);
            uint32_t fw_size = hdr->ih_img_size;
            BOOT_LOG_INF("Starting network core update");
            rc = pcd_network_core_update(net_core_fw_addr, fw_size);

            if (rc != 0) {
                swap_type = BOOT_SWAP_TYPE_FAIL;
            } else {
                BOOT_LOG_INF("Done updating network core");
#if defined(MCUBOOT_SWAP_USING_SCRATCH) || defined(MCUBOOT_SWAP_USING_MOVE)
                /* swap_erase_trailer_sectors is undefined if upgrade only
                 * method is used. There is no need to erase sectors, because
                 * the image cannot be reverted.
                 */
                rc = swap_erase_trailer_sectors(state,
                        secondary_fa);
#endif
                swap_type = BOOT_SWAP_TYPE_NONE;
            }
        }
#endif /* CONFIG_SOC_NRF5340_CPUAPP && PM_CPUNET_B0N_ADDRESS &&
	  !CONFIG_NRF53_MULTI_IMAGE_UPDATE && CONFIG_PCD_APP */
    }

    return swap_type;
}
#endif

/**
 * Removes data from specified region either by writing erase value in place of
 * data or by doing erase, if device has such hardware requirement.
 * Note that function will fail if off or size are not aligned to device
 * write block size or erase block size.
 *
 * @param fa                    The flash_area containing the region to erase.
 * @param off                   The offset within the flash area to start the
 *                              erase.
 * @param size                  The number of bytes to erase.
 * @param backwards             If set to true will erase from end to start
 *                              addresses, otherwise erases from start to end
 *                              addresses.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
boot_scramble_region(const struct flash_area *fa, uint32_t off, uint32_t size, bool backwards)
{
    int rc = 0;

    BOOT_LOG_DBG("boot_scramble_region: %p %d %d %d", fa, off, size, (int)backwards);

    if (size == 0) {
        goto done;
    }

    if (device_requires_erase(fa)) {
        rc = boot_erase_region(fa, off, size, backwards);
    } else if (off >= flash_area_get_size(fa) || (flash_area_get_size(fa) - off) < size) {
        rc = -1;
        goto done;
    } else {
        uint8_t buf[BOOT_MAX_ALIGN];
        const size_t write_block = flash_area_align(fa);
        uint32_t end_offset;

        BOOT_LOG_DBG("boot_scramble_region: device without erase, overwriting");
        memset(buf, flash_area_erased_val(fa), sizeof(buf));

        if (backwards) {
            end_offset = ALIGN_DOWN(off, write_block);
            /* Starting at the last write block in range */
            off += size - write_block;
        } else {
            end_offset = ALIGN_DOWN((off + size), write_block);
        }
        BOOT_LOG_DBG("boot_scramble_region: start offset %u, end offset %u", off, end_offset);

        while (off != end_offset) {
            /* Write over the area to scramble data that is there */
            rc = flash_area_write(fa, off, buf, write_block);
            if (rc != 0) {
                BOOT_LOG_DBG("boot_scramble_region: error %d for %p %d %u",
                             rc, fa, off, (unsigned int)write_block);
                break;
            }

            MCUBOOT_WATCHDOG_FEED();

            if (backwards) {
                if (end_offset >= off) {
                    /* Reached the first offset in range and already scrambled it */
                    break;
                }

                off -= write_block;
            } else {
                off += write_block;

                if (end_offset <= off) {
                    /* Reached the end offset in range and already scrambled it */
                    break;
                }
            }
        }
    }

done:
    return rc;
}

/**
 * Removes data from specified region backwards either by writing erase_value
 * in place of data or by doing erase, if device has such hardware requirement.
 * Note that function will fail if off or size are not aligned to device
 * write block size or erase block size.
 *
 * @param fa                    The flash_area containing the region to erase.
 * @param off                   The offset within the flash area to start the
 *                              erase.
 * @param size                  The number of bytes to erase.
 *
 * @return                      0 on success; nonzero on failure.
 */

/**
 * Remove enough data from slot to mark is as unused
 * Assumption: header and trailer are not overlapping on write block or
 * erase block, if device has erase requirement.
 * Note that this function is intended for removing data not preparing device
 * for write.
 *
 * @param fa        Pointer to flash area object for slot
 * @param slot      Slot the @p fa represents
 *
 * @return          0 on success; nonzero on failure.
 */
int
boot_scramble_slot(const struct flash_area *fa, int slot)
{
    size_t size;
    int ret = 0;

    (void)slot;

    /* Without minimal entire area needs to be scrambled */
#if !defined(MCUBOOT_MINIMAL_SCRAMBLE)
    size = flash_area_get_size(fa);
    ret = boot_scramble_region(fa, 0, size, false);
#else
    size_t off = 0;

    ret = boot_header_scramble_off_sz(fa, slot, &off, &size);
    if (ret < 0) {
        return ret;
    }

    ret = boot_scramble_region(fa, off, size, false);
    if (ret < 0) {
        return ret;
    }

    ret = boot_trailer_scramble_offset(fa, 0, &off);
    if (ret < 0) {
        return ret;
    }

    ret = boot_scramble_region(fa, off, (flash_area_get_size(fa) - off), true);
#endif
    return ret;
}

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)

#if defined(MCUBOOT_ENC_IMAGES) || defined(MCUBOOT_SWAP_SAVE_ENCTLV)
/* Replacement for memset(p, 0, sizeof(*p) that does not get
 * optimized out.
 */
static void like_mbedtls_zeroize(void *p, size_t n)
{
    volatile unsigned char *v = (unsigned char *)p;

    for (size_t i = 0; i < n; i++) {
        v[i] = 0;
    }
}
#endif

/**
 * Copies the contents of one flash region to another.  You must erase the
 * destination region prior to calling this function.
 *
 * @param flash_area_id_src     The ID of the source flash area.
 * @param flash_area_id_dst     The ID of the destination flash area.
 * @param off_src               The offset within the source flash area to
 *                                  copy from.
 * @param off_dst               The offset within the destination flash area to
 *                                  copy to.
 * @param sz                    The number of bytes to copy.
 * @param sector_off            (Swap using offset with encryption only) the
 *                                  sector offset for encryption/decryption
 *
 * @return                      0 on success; nonzero on failure.
 */
int
#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_ENC_IMAGES)
boot_copy_region(struct boot_loader_state *state,
                 const struct flash_area *fap_src,
                 const struct flash_area *fap_dst,
                 uint32_t off_src, uint32_t off_dst, uint32_t sz, uint32_t sector_off)
#else
boot_copy_region(struct boot_loader_state *state,
                 const struct flash_area *fap_src,
                 const struct flash_area *fap_dst,
                 uint32_t off_src, uint32_t off_dst, uint32_t sz)
#endif
{
    uint32_t bytes_copied;
    int chunk_sz;
    int rc;
#ifdef MCUBOOT_ENC_IMAGES
    uint32_t off = off_dst;
    uint32_t tlv_off;
    size_t blk_off;
    struct image_header *hdr;
    uint16_t idx;
    uint32_t blk_sz;
    uint8_t image_index = BOOT_CURR_IMG(state);
    bool encrypted_src;
    bool encrypted_dst;
    /* Assuming the secondary slot is source; note that 0 here not only
     * means that primary slot is source, but also that there will be
     * encryption happening, if it is 1 then there is decryption from
     * secondary slot.
     */
    int source_slot = 1;
    /* In case of encryption enabled, we may have to do more work than
     * just copy bytes */
    bool only_copy = false;
#else
    (void)state;
#endif
#if defined(MCUBOOT_DECOMPRESS_IMAGES) && !defined(MCUBOOT_ENC_IMAGES)
    struct image_header *hdr;
#endif

    TARGET_STATIC uint8_t buf[BUF_SZ] __attribute__((aligned(4)));

#ifdef MCUBOOT_ENC_IMAGES
    encrypted_src = (flash_area_get_id(fap_src) != FLASH_AREA_IMAGE_PRIMARY(image_index));
    encrypted_dst = (flash_area_get_id(fap_dst) != FLASH_AREA_IMAGE_PRIMARY(image_index));

    if (encrypted_src != encrypted_dst) {
        if (encrypted_dst) {
            /* Need encryption, metadata from the primary slot */
            hdr = boot_img_hdr(state, BOOT_PRIMARY_SLOT);
            source_slot = 0;
        } else {
            /* Need decryption, metadata from the secondary slot */
            hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);
            source_slot = 1;
        }
    } else {
        /* In case when source and targe is the same area, this means that we
         * only have to copy bytes, no encryption or decryption.
         */
        only_copy = true;
    }
#endif

#ifdef MCUBOOT_DECOMPRESS_IMAGES
    hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);

    if (MUST_DECOMPRESS(fap_src, BOOT_CURR_IMG(state), hdr)) {
        /* Use alternative function for compressed images */
        return boot_copy_region_decompress(state, fap_src, fap_dst, off_src, off_dst, sz, buf,
                                           BUF_SZ);
    }
#endif

    bytes_copied = 0;
    while (bytes_copied < sz) {
        if (sz - bytes_copied > sizeof buf) {
            chunk_sz = sizeof buf;
        } else {
            chunk_sz = sz - bytes_copied;
        }

        rc = flash_area_read(fap_src, off_src + bytes_copied, buf, chunk_sz);
        if (rc != 0) {
            return BOOT_EFLASH;
        }

#ifdef MCUBOOT_ENC_IMAGES
        /* If only copy, then does not matter if header indicates need for
         * encryption/decryption, we just copy data. */
        if (!only_copy && IS_ENCRYPTED(hdr)) {
#if defined(MCUBOOT_SWAP_USING_OFFSET)
            uint32_t abs_off = off - sector_off + bytes_copied;
#else
            uint32_t abs_off = off + bytes_copied;
#endif
            if (abs_off < hdr->ih_hdr_size) {
                /* do not decrypt header */
                if (abs_off + chunk_sz > hdr->ih_hdr_size) {
                    /* The lower part of the chunk contains header data */
                    blk_off = 0;
                    blk_sz = chunk_sz - (hdr->ih_hdr_size - abs_off);
                    idx = hdr->ih_hdr_size  - abs_off;
                } else {
                    /* The chunk contains exclusively header data */
                    blk_sz = 0; /* nothing to decrypt */
                }
            } else {
                idx = 0;
                blk_sz = chunk_sz;
                blk_off = (abs_off - hdr->ih_hdr_size) & 0xf;
            }

            if (blk_sz > 0)
            {
                tlv_off = BOOT_TLV_OFF(hdr);
                if (abs_off + chunk_sz > tlv_off) {
                    /* do not decrypt TLVs */
                    if (abs_off >= tlv_off) {
                        blk_sz = 0;
                    } else {
                        blk_sz = tlv_off - abs_off;
                    }
                }
                if (source_slot == 0) {
                    boot_enc_encrypt(BOOT_CURR_ENC(state), source_slot,
                            (abs_off + idx) - hdr->ih_hdr_size, blk_sz,
                            blk_off, &buf[idx]);
                } else {
                    boot_enc_decrypt(BOOT_CURR_ENC(state), source_slot,
                            (abs_off + idx) - hdr->ih_hdr_size, blk_sz,
                            blk_off, &buf[idx]);
                }
            }
        }
#endif

        rc = flash_area_write(fap_dst, off_dst + bytes_copied, buf, chunk_sz);
        if (rc != 0) {
            return BOOT_EFLASH;
        }

        bytes_copied += chunk_sz;

        MCUBOOT_WATCHDOG_FEED();
    }

    return 0;
}

/**
 * Overwrite primary slot with the image contained in the secondary slot.
 * If a prior copy operation was interrupted by a system reset, this function
 * redos the copy.
 *
 * @param bs                    The current boot status.  This function reads
 *                                  this struct to determine if it is resuming
 *                                  an interrupted swap operation.  This
 *                                  function writes the updated status to this
 *                                  function on return.
 *
 * @return                      0 on success; nonzero on failure.
 */
#if defined(MCUBOOT_OVERWRITE_ONLY) || defined(MCUBOOT_BOOTSTRAP)
static int
boot_copy_image(struct boot_loader_state *state, struct boot_status *bs)
{
    size_t sect_count;
    size_t sect;
    int rc;
    size_t size;
    size_t this_size;
    size_t last_sector;
    const struct flash_area *fap_primary_slot;
    const struct flash_area *fap_secondary_slot;
    uint8_t image_index;

#if defined(MCUBOOT_OVERWRITE_ONLY_FAST)
    uint32_t sector;
    uint32_t trailer_sz;
    uint32_t off;
    uint32_t sz;
#endif

    (void)bs;

#if defined(MCUBOOT_OVERWRITE_ONLY_FAST)
    uint32_t src_size = 0;
    rc = boot_read_image_size(state, BOOT_SECONDARY_SLOT, &src_size);
    assert(rc == 0);
#endif

    image_index = BOOT_CURR_IMG(state);

    BOOT_LOG_INF("Image %d upgrade secondary slot -> primary slot", image_index);
    BOOT_LOG_INF("Erasing the primary slot");

    fap_primary_slot = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);
    assert(fap_primary_slot != NULL);

    fap_secondary_slot = BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT);
    assert(fap_secondary_slot != NULL);

    sect_count = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT);
    for (sect = 0, size = 0; sect < sect_count; sect++) {
        this_size = boot_img_sector_size(state, BOOT_PRIMARY_SLOT, sect);
        rc = boot_erase_region(fap_primary_slot, size, this_size, false);
        assert(rc == 0);

#if defined(MCUBOOT_OVERWRITE_ONLY_FAST)
        if ((size + this_size) >= src_size) {
            size += src_size - size;
            size += BOOT_WRITE_SZ(state) - (size % BOOT_WRITE_SZ(state));
            break;
        }
#endif

        size += this_size;
    }

#if defined(MCUBOOT_OVERWRITE_ONLY_FAST)
    trailer_sz = boot_trailer_sz(BOOT_WRITE_SZ(state));
    sector = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT) - 1;
    sz = 0;
    do {
        sz += boot_img_sector_size(state, BOOT_PRIMARY_SLOT, sector);
        off = boot_img_sector_off(state, BOOT_PRIMARY_SLOT, sector);
        sector--;
    } while (sz < trailer_sz);

    rc = boot_erase_region(fap_primary_slot, off, sz, false);
    assert(rc == 0);
#endif

#ifdef MCUBOOT_ENC_IMAGES
    if (IS_ENCRYPTED(boot_img_hdr(state, BOOT_SECONDARY_SLOT))) {
#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
        rc = boot_enc_load(state, BOOT_SECONDARY_SLOT,
                boot_img_hdr(state, BOOT_SECONDARY_SLOT),
                fap_secondary_slot, bs, 0);
#else
        rc = boot_enc_load(state, BOOT_SECONDARY_SLOT,
                boot_img_hdr(state, BOOT_SECONDARY_SLOT),
                fap_secondary_slot, bs);
#endif

        if (rc < 0) {
            return BOOT_EBADIMAGE;
        }
        if (rc == 0 && boot_enc_set_key(BOOT_CURR_ENC(state), 1, bs)) {
            return BOOT_EBADIMAGE;
        }
    }
#endif

    BOOT_LOG_INF("Image %d copying the secondary slot to the primary slot: 0x%zx bytes",
                 image_index, size);
#if defined(MCUBOOT_SWAP_USING_OFFSET)
    rc = BOOT_COPY_REGION(state, fap_secondary_slot, fap_primary_slot,
                          boot_img_sector_size(state, BOOT_SECONDARY_SLOT, 0), 0, size, 0);
#else
    rc = boot_copy_region(state, fap_secondary_slot, fap_primary_slot, 0, 0, size);
#endif
    if (rc != 0) {
        return rc;
    }

#if defined(MCUBOOT_OVERWRITE_ONLY_FAST)
    rc = boot_write_magic(fap_primary_slot);
    if (rc != 0) {
        return rc;
    }
#endif

    rc = BOOT_HOOK_CALL(boot_copy_region_post_hook, 0, BOOT_CURR_IMG(state),
                        BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT), size);
    if (rc != 0) {
        return rc;
    }

#ifdef MCUBOOT_HW_ROLLBACK_PROT
    /* Update the stored security counter with the new image's security counter
     * value. Both slots hold the new image at this point, but the secondary
     * slot's image header must be passed since the image headers in the
     * boot_data structure have not been updated yet.
     */
    rc = boot_update_security_counter(state, BOOT_PRIMARY_SLOT, BOOT_SECONDARY_SLOT);
    if (rc != 0) {
        BOOT_LOG_ERR("Security counter update failed after image upgrade.");
        return rc;
    }
#endif /* MCUBOOT_HW_ROLLBACK_PROT */

#ifndef MCUBOOT_OVERWRITE_ONLY_KEEP_BACKUP
    /*
     * Erases header and trailer. The trailer is erased because when a new
     * image is written without a trailer as is the case when using newt, the
     * trailer that was left might trigger a new upgrade.
     */
    BOOT_LOG_DBG("erasing secondary header");
    rc = boot_scramble_region(fap_secondary_slot,
                              boot_img_sector_off(state, BOOT_SECONDARY_SLOT, 0),
                              boot_img_sector_size(state, BOOT_SECONDARY_SLOT, 0), false);
    assert(rc == 0);
#endif

    last_sector = boot_img_num_sectors(state, BOOT_SECONDARY_SLOT) - 1;
    BOOT_LOG_DBG("erasing secondary trailer");
    rc = boot_scramble_region(fap_secondary_slot,
                              boot_img_sector_off(state, BOOT_SECONDARY_SLOT,
                                    last_sector),
                              boot_img_sector_size(state, BOOT_SECONDARY_SLOT,
                                    last_sector), false);
    assert(rc == 0);

    /* TODO: Perhaps verify the primary slot's signature again? */

    return 0;
}
#endif

#if !defined(MCUBOOT_OVERWRITE_ONLY)
/**
 * Swaps the two images in flash.  If a prior copy operation was interrupted
 * by a system reset, this function completes that operation.
 *
 * @param bs                    The current boot status.  This function reads
 *                                  this struct to determine if it is resuming
 *                                  an interrupted swap operation.  This
 *                                  function writes the updated status to this
 *                                  function on return.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
boot_swap_image(struct boot_loader_state *state, struct boot_status *bs)
{
    struct image_header *hdr;
    const struct flash_area *fap;
#ifdef MCUBOOT_ENC_IMAGES
    uint8_t slot;
    uint8_t i;
#endif
    uint32_t size;
    uint32_t copy_size;
    uint8_t image_index;
    int rc;

    /* FIXME: just do this if asked by user? */

    size = copy_size = 0;
    image_index = BOOT_CURR_IMG(state);

    if (boot_status_is_reset(bs)) {
        /*
         * No swap ever happened, so need to find the largest image which
         * will be used to determine the amount of sectors to swap.
         */
        hdr = boot_img_hdr(state, BOOT_PRIMARY_SLOT);
        if (hdr->ih_magic == IMAGE_MAGIC) {
            rc = boot_read_image_size(state, BOOT_PRIMARY_SLOT, &copy_size);
            assert(rc == 0);
        }

#ifdef MCUBOOT_ENC_IMAGES
        if (IS_ENCRYPTED(hdr)) {
            fap = BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT);
#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
            rc = boot_enc_load(state, 0, hdr, fap, bs, 0);
#else
            rc = boot_enc_load(state, 0, hdr, fap, bs);
#endif
            assert(rc >= 0);

            if (rc == 0) {
                rc = boot_enc_set_key(BOOT_CURR_ENC(state), 0, bs);
                assert(rc == 0);
            } else {
                rc = 0;
            }
        } else {
            memset(bs->enckey[0], 0xff, BOOT_ENC_KEY_ALIGN_SIZE);
        }
#endif

        hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);
        if (hdr->ih_magic == IMAGE_MAGIC) {
            rc = boot_read_image_size(state, BOOT_SECONDARY_SLOT, &size);
            assert(rc == 0);
        }

#ifdef MCUBOOT_ENC_IMAGES
        hdr = boot_img_hdr(state, BOOT_SECONDARY_SLOT);
        if (IS_ENCRYPTED(hdr)) {
            fap = BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT);
#if defined(MCUBOOT_SWAP_USING_OFFSET) && defined(MCUBOOT_SERIAL_RECOVERY)
            rc = boot_enc_load(state, 1, hdr, fap, bs, 0);
#else
            rc = boot_enc_load(state, 1, hdr, fap, bs);
#endif
            assert(rc >= 0);

            if (rc == 0) {
                rc = boot_enc_set_key(BOOT_CURR_ENC(state), 1, bs);
                assert(rc == 0);
            } else {
                rc = 0;
            }
        } else {
            memset(bs->enckey[1], 0xff, BOOT_ENC_KEY_ALIGN_SIZE);
        }
#endif

        if (size > copy_size) {
            copy_size = size;
        }

        bs->swap_size = copy_size;
    } else {
        /*
         * If a swap was under way, the swap_size should already be present
         * in the trailer...
         */

        fap = boot_find_status(state, image_index);
        assert(fap != NULL);
        rc = boot_read_swap_size(fap, &bs->swap_size);
        assert(rc == 0);

        copy_size = bs->swap_size;

#ifdef MCUBOOT_ENC_IMAGES
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            rc = boot_read_enc_key(fap, slot, bs);
            assert(rc == 0);

            for (i = 0; i < BOOT_ENC_KEY_SIZE; i++) {
                if (bs->enckey[slot][i] != 0xff) {
                    break;
                }
            }

            boot_enc_init(BOOT_CURR_ENC(state), slot);

            if (i != BOOT_ENC_KEY_SIZE) {
                boot_enc_set_key(BOOT_CURR_ENC(state), slot, bs);
            }
        }
#endif
        flash_area_close(fap);
    }

#if defined(PM_S1_ADDRESS) && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
    if (owner_nsib[BOOT_CURR_IMG(state)]) {
        if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER) {
            /* For NSIB, move the image instead of swapping it */
            nsib_swap_run(state, bs);

#if defined(CONFIG_REBOOT)
            /* Should also reboot at this point so the new S0/S1 update is applied */
            sys_reboot(SYS_REBOOT_COLD);
#endif
        }
    } else
#endif
    {
        swap_run(state, bs, copy_size);
    }

#ifdef MCUBOOT_VALIDATE_PRIMARY_SLOT
    extern int boot_status_fails;
    if (boot_status_fails > 0) {
        BOOT_LOG_WRN("%d status write fails performing the swap",
                     boot_status_fails);
    }
#endif
    rc = BOOT_HOOK_CALL(boot_copy_region_post_hook, 0, BOOT_CURR_IMG(state),
                        BOOT_IMG_AREA(state, BOOT_PRIMARY_SLOT), size);

    return 0;
}
#endif

/**
 * Performs a clean (not aborted) image update.
 *
 * @param bs                    The current boot status.
 *
 * @return                      0 on success; nonzero on failure.
 */
static int
boot_perform_update(struct boot_loader_state *state, struct boot_status *bs)
{
    int rc;
#ifndef MCUBOOT_OVERWRITE_ONLY
    uint8_t swap_type;
#endif

    /* At this point there are no aborted swaps. */
#if defined(MCUBOOT_OVERWRITE_ONLY)
    rc = boot_copy_image(state, bs);
#elif defined(MCUBOOT_BOOTSTRAP)
    /* Check if the image update was triggered by a bad image in the
     * primary slot (the validity of the image in the secondary slot had
     * already been checked).
     */
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    rc = boot_check_header_erased(state, BOOT_PRIMARY_SLOT);
    FIH_CALL(boot_validate_slot, fih_rc, state, BOOT_PRIMARY_SLOT, bs, 0);
    if (rc == 0 || FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        rc = boot_copy_image(state, bs);
    } else {
        rc = boot_swap_image(state, bs);
    }
#else
        rc = boot_swap_image(state, bs);
#endif
    assert(rc == 0);

#ifndef MCUBOOT_OVERWRITE_ONLY
    /* The following state needs image_ok be explicitly set after the
     * swap was finished to avoid a new revert.
     */
    swap_type = BOOT_SWAP_TYPE(state);
    if (swap_type == BOOT_SWAP_TYPE_REVERT ||
            swap_type == BOOT_SWAP_TYPE_PERM) {
        rc = swap_set_image_ok(BOOT_CURR_IMG(state));
        if (rc != 0) {
            BOOT_SWAP_TYPE(state) = swap_type = BOOT_SWAP_TYPE_PANIC;
        }
    }

#ifdef MCUBOOT_HW_ROLLBACK_PROT
    if (swap_type == BOOT_SWAP_TYPE_PERM) {
        /* Update the stored security counter with the new image's security
         * counter value. The primary slot holds the new image at this point,
         * but the secondary slot's image header must be passed since image
         * headers in the boot_data structure have not been updated yet.
         *
         * In case of a permanent image swap mcuboot will never attempt to
         * revert the images on the next reboot. Therefore, the security
         * counter must be increased right after the image upgrade.
         */
        rc = boot_update_security_counter(state, BOOT_PRIMARY_SLOT, BOOT_SECONDARY_SLOT);
        if (rc != 0) {
            BOOT_LOG_ERR("Security counter update failed after "
                         "image upgrade.");
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
        }
    }
#endif /* MCUBOOT_HW_ROLLBACK_PROT */

    if (BOOT_IS_UPGRADE(swap_type)) {
        rc = swap_set_copy_done(BOOT_CURR_IMG(state));
        if (rc != 0) {
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
        }
    }
#endif /* !MCUBOOT_OVERWRITE_ONLY */

    return rc;
}

/**
 * Completes a previously aborted image swap.
 *
 * @param bs                    The current boot status.
 *
 * @return                      0 on success; nonzero on failure.
 */
#if !defined(MCUBOOT_OVERWRITE_ONLY)
static int
boot_complete_partial_swap(struct boot_loader_state *state,
        struct boot_status *bs)
{
    int rc;

    /* Determine the type of swap operation being resumed from the
     * `swap-type` trailer field.
     */
    rc = boot_swap_image(state, bs);
    assert(rc == 0);

    BOOT_SWAP_TYPE(state) = bs->swap_type;

    /* The following states need image_ok be explicitly set after the
     * swap was finished to avoid a new revert.
     */
    if (bs->swap_type == BOOT_SWAP_TYPE_REVERT ||
        bs->swap_type == BOOT_SWAP_TYPE_PERM) {
        rc = swap_set_image_ok(BOOT_CURR_IMG(state));
        if (rc != 0) {
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
        }
    }

    if (BOOT_IS_UPGRADE(bs->swap_type)) {
        rc = swap_set_copy_done(BOOT_CURR_IMG(state));
        if (rc != 0) {
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
        }
    }

    if (BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_PANIC) {
        BOOT_LOG_ERR("panic!");
        assert(0);

        /* Loop forever... */
        while (1) {}
    }

    return rc;
}
#endif /* !MCUBOOT_OVERWRITE_ONLY */

#if (BOOT_IMAGE_NUMBER > 1)
/**
 * Review the validity of previously determined swap types of other images.
 *
 * @param aborted_swap          The current image upgrade is a
 *                              partial/aborted swap.
 */
static void
boot_review_image_swap_types(struct boot_loader_state *state,
                             bool aborted_swap)
{
    /* In that case if we rebooted in the middle of an image upgrade process, we
     * must review the validity of swap types, that were previously determined
     * for other images. The image_ok flag had not been set before the reboot
     * for any of the updated images (only the copy_done flag) and thus falsely
     * the REVERT swap type has been determined for the previous images that had
     * been updated before the reboot.
     *
     * There are two separate scenarios that we have to deal with:
     *
     * 1. The reboot has happened during swapping an image:
     *      The current image upgrade has been determined as a
     *      partial/aborted swap.
     * 2. The reboot has happened between two separate image upgrades:
     *      In this scenario we must check the swap type of the current image.
     *      In those cases if it is NONE or REVERT we cannot certainly determine
     *      the fact of a reboot. In a consistent state images must move in the
     *      same direction or stay in place, e.g. in practice REVERT and TEST
     *      swap types cannot be present at the same time. If the swap type of
     *      the current image is either TEST, PERM or FAIL we must review the
     *      already determined swap types of other images and set each false
     *      REVERT swap types to NONE (these images had been successfully
     *      updated before the system rebooted between two separate image
     *      upgrades).
     */

    if (BOOT_CURR_IMG(state) == 0) {
        /* Nothing to do */
        return;
    }

    if (!aborted_swap) {
        if ((BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_NONE) ||
            (BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_REVERT)) {
            /* Nothing to do */
            return;
        }
    }

    for (uint8_t i = 0; i < BOOT_CURR_IMG(state); i++) {
        if (state->swap_type[i] == BOOT_SWAP_TYPE_REVERT) {
            state->swap_type[i] = BOOT_SWAP_TYPE_NONE;
        }
    }
}
#endif

/**
 * Prepare image to be updated if required.
 *
 * Prepare image to be updated if required with completing an image swap
 * operation if one was aborted and/or determining the type of the
 * swap operation. In case of any error set the swap type to NONE.
 *
 * @param state                 TODO
 * @param bs                    Pointer where the read and possibly updated
 *                              boot status can be written to.
 */
static void
boot_prepare_image_for_update(struct boot_loader_state *state,
                              struct boot_status *bs)
{
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO) || defined(MCUBOOT_DATA_SHARING)
    int max_size;
#endif

    /* Determine the sector layout of the image slots and scratch area. */
    rc = boot_read_sectors(state);
    if (rc != 0) {
        BOOT_LOG_WRN("Failed reading sectors; BOOT_MAX_IMG_SECTORS=%d"
                     " - too small?", BOOT_MAX_IMG_SECTORS);
        /* Unable to determine sector layout, continue with next image
         * if there is one.
         */
        BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
        if (rc == BOOT_EFLASH)
        {
            /* Only return on error from the primary image flash */
            return;
        }
    }

    /* Attempt to read an image header from each slot. */
    rc = boot_read_image_headers(state, false, NULL);
    if (rc != 0) {
        /* Continue with next image if there is one. */
        BOOT_LOG_WRN("Failed reading image headers; Image=%u",
                BOOT_CURR_IMG(state));
        BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
        return;
    }

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO) || defined(MCUBOOT_DATA_SHARING)
    /* Fetch information on maximum sizes for later usage, if needed */
    max_size = app_max_size(state);

    if (max_size > 0) {
        image_max_sizes[BOOT_CURR_IMG(state)].calculated = true;
        image_max_sizes[BOOT_CURR_IMG(state)].max_size = max_size;
    }
#endif

    /* If the current image's slots aren't compatible, no swap is possible.
     * Just boot into primary slot.
     */
    if (boot_slots_compatible(state)) {
        boot_status_reset(bs);

#ifndef MCUBOOT_OVERWRITE_ONLY
        rc = swap_read_status(state, bs);
        if (rc != 0) {
            BOOT_LOG_WRN("Failed reading boot status; Image=%u",
                    BOOT_CURR_IMG(state));
            /* Continue with next image if there is one. */
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
            return;
        }
#endif

#if defined(MCUBOOT_SWAP_USING_SCRATCH) || defined(MCUBOOT_SWAP_USING_MOVE) || defined(MCUBOOT_SWAP_USING_OFFSET)
        /*
         * Must re-read image headers because the boot status might
         * have been updated in the previous function call.
         */
        rc = boot_read_image_headers(state, !boot_status_is_reset(bs), bs);
#ifdef MCUBOOT_BOOTSTRAP
        /* When bootstrapping it's OK to not have image magic in the primary slot */
        if (rc != 0 && boot_check_header_erased(state, BOOT_PRIMARY_SLOT) != 0) {
#else
        if (rc != 0) {
#endif

            /* Continue with next image if there is one. */
            BOOT_LOG_WRN("Failed reading image headers; Image=%u",
                    BOOT_CURR_IMG(state));
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
            return;
        }
#endif

        /* Determine if we rebooted in the middle of an image swap
         * operation. If a partial swap was detected, complete it.
         */
        if (!boot_status_is_reset(bs)) {

#if (BOOT_IMAGE_NUMBER > 1)
            boot_review_image_swap_types(state, true);
#endif

#ifdef MCUBOOT_OVERWRITE_ONLY
            /* Should never arrive here, overwrite-only mode has
             * no swap state.
             */
            assert(0);
#else
            /* Determine the type of swap operation being resumed from the
             * `swap-type` trailer field.
             */
            rc = boot_complete_partial_swap(state, bs);
            assert(rc == 0);
#endif
            /* Attempt to read an image header from each slot. Ensure that image headers in slots
             * are aligned with headers in boot_data.
             *
             * The boot status (last param) is used to figure out in which slot the header of each
             * image is currently located. This is useful as in the middle of an upgrade process,
             * the header of a given image could have already been moved to the other slot. However,
             * providing it at the end of the upgrade, as it is the case here, would cause the
             * reading of the header of the primary image from the secondary slot and the secondary
             * image from the primary slot, since the images have been swapped. That's not what we
             * want here, since the goal is to upgrade the bootloader state to reflect the new state
             * of the slots: the image headers in the primary and secondary slots must now
             * respectively be the headers of the new and previous active image. So NULL is provided
             * as boot status.
             */
            rc = boot_read_image_headers(state, false, NULL);
            assert(rc == 0);

            /* Swap has finished set to NONE */
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
        } else {
            /* There was no partial swap, determine swap type. */
            if (bs->swap_type == BOOT_SWAP_TYPE_NONE) {
                BOOT_SWAP_TYPE(state) = boot_validated_swap_type(state, bs);
            } else {
                FIH_CALL(boot_validate_slot, fih_rc,
                         state, BOOT_SECONDARY_SLOT, bs, 0);
                if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
                    BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_FAIL;
                } else {
                    BOOT_SWAP_TYPE(state) = bs->swap_type;
                }
            }

#if (BOOT_IMAGE_NUMBER > 1)
            boot_review_image_swap_types(state, false);
#endif

#ifdef MCUBOOT_BOOTSTRAP
            if (BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_NONE) {
                /* Header checks are done first because they are
                 * inexpensive. Since overwrite-only copies starting from
                 * offset 0, if interrupted, it might leave a valid header
                 * magic, so also run validation on the primary slot to be
                 * sure it's not OK.
                 */
                rc = boot_check_header_erased(state, BOOT_PRIMARY_SLOT);
                FIH_CALL(boot_validate_slot, fih_rc,
                         state, BOOT_PRIMARY_SLOT, bs, 0);

                if (rc == 0 || FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {

                    rc = (boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_magic == IMAGE_MAGIC) ? 1: 0;
                    FIH_CALL(boot_validate_slot, fih_rc,
                             state, BOOT_SECONDARY_SLOT, bs, 0);

                    if (rc == 1 && FIH_EQ(fih_rc, FIH_SUCCESS)) {
                        /* Set swap type to REVERT to overwrite the primary
                         * slot with the image contained in secondary slot
                         * and to trigger the explicit setting of the
                         * image_ok flag.
                         */
                        BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_REVERT;
                    }
                }
            }
#endif
        }
    } else {
        /* In that case if slots are not compatible. */
        BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
    }
}

/**
 * Updates the security counter for the current image.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_update_hw_rollback_protection(struct boot_loader_state *state)
{
#ifdef MCUBOOT_HW_ROLLBACK_PROT
    int rc;
    uint8_t image_index;
    struct boot_swap_state swap_state;

    image_index = BOOT_CURR_IMG(state);

    rc = boot_read_swap_state_by_id(FLASH_AREA_IMAGE_PRIMARY(image_index), &swap_state);
    if (rc != 0) {
        return rc;
    }

    /* Update the stored security counter with the active image's security
     * counter value. It will only be updated if the new security counter is
     * greater than the stored value.
     *
     * In case of a successful image swapping when the swap type is TEST the
     * security counter can be increased only after a reset, when the image has
     * marked itself "OK" (the image_ok flag has been set). This way a "revert"
     * can be performed when it's necessary.
     */
    if (swap_state.magic != BOOT_MAGIC_GOOD || swap_state.image_ok == BOOT_FLAG_SET) {
        rc = boot_update_security_counter(state, BOOT_PRIMARY_SLOT, BOOT_PRIMARY_SLOT);
        if (rc != 0) {
            BOOT_LOG_ERR("Security counter update failed after image "
                            "validation.");
            return rc;
        }
    }

    return 0;

#else /* MCUBOOT_HW_ROLLBACK_PROT */
    (void) (state);

    return 0;
#endif
}

/**
 * Checks test swap downgrade prevention conditions.
 *
 * Function called only for swap upgrades test run.  It may prevent
 * swap if slot 1 image has <= version number or < security counter
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 - image can be swapped, -1 downgrade prevention
 */
static int
check_downgrade_prevention(struct boot_loader_state *state)
{
#if defined(MCUBOOT_DOWNGRADE_PREVENTION) && \
    (defined(MCUBOOT_SWAP_USING_MOVE) || defined(MCUBOOT_SWAP_USING_SCRATCH) || defined(MCUBOOT_SWAP_USING_OFFSET))
    uint32_t security_counter[2];
    int rc;

#if defined(PM_S1_ADDRESS)
    if (owner_nsib[BOOT_CURR_IMG(state)]) {
        /*
         * Downgrade prevention on S0/S1 image is managed by NSIB which is a software (not
         * hardware) check
         */
        return 0;
    }
#endif

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER != -1
    if (BOOT_CURR_IMG(state) == CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER) {
        /*
         * Downgrade prevention on network core image is managed by NSIB which is a software (not
         * hardware) check
         */
        return 0;
    }
#endif

    if (MCUBOOT_DOWNGRADE_PREVENTION_SECURITY_COUNTER) {
        /* If there was security no counter in slot 0, allow swap */
        rc = bootutil_get_img_security_cnt(state, BOOT_PRIMARY_SLOT,
                                           BOOT_IMG_AREA(state, 0),
                                           &security_counter[0]);
        if (rc != 0) {
            return 0;
        }
        /* If there is no security counter in slot 1, or it's lower than
         * that of slot 0, prevent downgrade */
        rc = bootutil_get_img_security_cnt(state, BOOT_SECONDARY_SLOT,
                                           BOOT_IMG_AREA(state, 1),
                                           &security_counter[1]);
        if (rc != 0 || security_counter[0] > security_counter[1]) {
            rc = -1;
        }
    }
    else {
        rc = boot_version_cmp(&boot_img_hdr(state, BOOT_SECONDARY_SLOT)->ih_ver,
                              &boot_img_hdr(state, BOOT_PRIMARY_SLOT)->ih_ver);
    }
    if (rc < 0) {
        /* Image in slot 0 prevents downgrade, delete image in slot 1 */
        BOOT_LOG_INF("Image %d in slot 1 erased due to downgrade prevention", BOOT_CURR_IMG(state));
        boot_scramble_slot(BOOT_IMG_AREA(state, 1), BOOT_SECONDARY_SLOT);
    } else {
        rc = 0;
    }
    return rc;
#else
    (void)state;
    return 0;
#endif
}

fih_ret
context_boot_go(struct boot_loader_state *state, struct boot_rsp *rsp)
{
    size_t slot;
    struct boot_status bs;
    int rc = -1;
    FIH_DECLARE(fih_rc, FIH_FAILURE);
    int fa_id;
    int image_index;
    bool has_upgrade;
    volatile int fih_cnt;

    BOOT_LOG_DBG("context_boot_go");

#if defined(__BOOTSIM__)
    /* The array of slot sectors are defined here (as opposed to file scope) so
     * that they don't get allocated for non-boot-loader apps.  This is
     * necessary because the gcc option "-fdata-sections" doesn't seem to have
     * any effect in older gcc versions (e.g., 4.8.4).
     */
    TARGET_STATIC boot_sector_t primary_slot_sectors[BOOT_IMAGE_NUMBER][BOOT_MAX_IMG_SECTORS];
    TARGET_STATIC boot_sector_t secondary_slot_sectors[BOOT_IMAGE_NUMBER][BOOT_MAX_IMG_SECTORS];
#if MCUBOOT_SWAP_USING_SCRATCH
    TARGET_STATIC boot_sector_t scratch_sectors[BOOT_MAX_IMG_SECTORS];
#endif
#endif

    has_upgrade = false;

#if (BOOT_IMAGE_NUMBER == 1)
    (void)has_upgrade;
#endif

    /* Iterate over all the images. By the end of the loop the swap type has
     * to be determined for each image and all aborted swaps have to be
     * completed.
     */
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif
#if defined(MCUBOOT_ENC_IMAGES) && (BOOT_IMAGE_NUMBER > 1)
        /* The keys used for encryption may no longer be valid (could belong to
         * another images). Therefore, mark them as invalid to force their reload
         * by boot_enc_load().
         */
        boot_enc_zeroize(BOOT_CURR_ENC(state));
#endif

        image_index = BOOT_CURR_IMG(state);

#if !defined(__BOOTSIM__)
        BOOT_IMG(state, BOOT_PRIMARY_SLOT).sectors =
            sector_buffers.primary[image_index];
        BOOT_IMG(state, BOOT_SECONDARY_SLOT).sectors =
            sector_buffers.secondary[image_index];
#if MCUBOOT_SWAP_USING_SCRATCH
        state->scratch.sectors = sector_buffers.scratch;
#endif
#else
        BOOT_IMG(state, BOOT_PRIMARY_SLOT).sectors =
            primary_slot_sectors[image_index];
        BOOT_IMG(state, BOOT_SECONDARY_SLOT).sectors =
            secondary_slot_sectors[image_index];
#if MCUBOOT_SWAP_USING_SCRATCH
        state->scratch.sectors = scratch_sectors;
#endif
#endif

        /* Open primary and secondary image areas for the duration
         * of this call.
         */
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            fa_id = flash_area_id_from_multi_image_slot(image_index, slot);
            rc = flash_area_open(fa_id, &BOOT_IMG_AREA(state, slot));
            assert(rc == 0);

            if (rc != 0) {
                BOOT_LOG_ERR("Failed to open flash area ID %d (image %d slot %d): %d, "
                             "cannot continue", fa_id, image_index, (int8_t)slot, rc);
                FIH_PANIC;
            }
        }
#if MCUBOOT_SWAP_USING_SCRATCH
        rc = flash_area_open(FLASH_AREA_IMAGE_SCRATCH,
                             &BOOT_SCRATCH_AREA(state));
        assert(rc == 0);

        if (rc != 0) {
            BOOT_LOG_ERR("Failed to open scratch flash area: %d, cannot continue", rc);
            FIH_PANIC;
        }
#endif

        /* Determine swap type and complete swap if it has been aborted. */
        boot_prepare_image_for_update(state, &bs);

        if (BOOT_IS_UPGRADE(BOOT_SWAP_TYPE(state))) {
            has_upgrade = true;
        }
    }

    /* cleanup secondary slots which were recognized unusable*/
    sec_slot_cleanup_if_unusable();

#if (BOOT_IMAGE_NUMBER > 1)
    if (has_upgrade) {
        /* Iterate over all the images and verify whether the image dependencies
         * are all satisfied and update swap type if necessary.
         */
        rc = boot_verify_dependencies(state);
        if (rc != 0) {
            /*
             * It was impossible to upgrade because the expected dependency version
             * was not available. Here we already changed the swap_type so that
             * instead of asserting the bootloader, we continue and no upgrade is
             * performed.
             */
            rc = 0;
        }
    }
#endif

    /* Trigger status change callback with upgrading status */
    if (has_upgrade) {
        mcuboot_status_change(MCUBOOT_STATUS_UPGRADING);
    }

    /* Iterate over all the images. At this point there are no aborted swaps
     * and the swap types are determined for each image. By the end of the loop
     * all required update operations will have been finished.
     */
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if (BOOT_IMAGE_NUMBER > 1)
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }

#ifdef MCUBOOT_ENC_IMAGES
        /* The keys used for encryption may no longer be valid (could belong to
         * another images). Therefore, mark them as invalid to force their reload
         * by boot_enc_load().
         */
        boot_enc_zeroize(BOOT_CURR_ENC(state));
#endif /* MCUBOOT_ENC_IMAGES */

        /* Indicate that swap is not aborted */
        boot_status_reset(&bs);
#endif /* (BOOT_IMAGE_NUMBER > 1) */

        /* Set the previously determined swap type */
        bs.swap_type = BOOT_SWAP_TYPE(state);

        switch (BOOT_SWAP_TYPE(state)) {
        case BOOT_SWAP_TYPE_NONE:
            break;

        case BOOT_SWAP_TYPE_TEST:
            /* fallthrough */
        case BOOT_SWAP_TYPE_PERM:
            if (check_downgrade_prevention(state) != 0) {
                /* Downgrade prevented */
                BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_NONE;
                break;
            }
            /* fallthrough */
        case BOOT_SWAP_TYPE_REVERT:
            rc = BOOT_HOOK_CALL(boot_perform_update_hook, BOOT_HOOK_REGULAR,
                                BOOT_CURR_IMG(state), &(BOOT_IMG(state, 1).hdr),
                                BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT));
            if (rc == BOOT_HOOK_REGULAR)
            {
                rc = boot_perform_update(state, &bs);
            }
            assert(rc == 0);
            break;

        case BOOT_SWAP_TYPE_FAIL:
            /* The image in secondary slot was invalid and is now erased. Ensure
             * we don't try to boot into it again on the next reboot. Do this by
             * pretending we just reverted back to primary slot.
             */
#ifndef MCUBOOT_OVERWRITE_ONLY
            /* image_ok needs to be explicitly set to avoid a new revert. */
            rc = swap_set_image_ok(BOOT_CURR_IMG(state));
            if (rc != 0) {
                BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
            }
#endif /* !MCUBOOT_OVERWRITE_ONLY */
            break;

        default:
            BOOT_SWAP_TYPE(state) = BOOT_SWAP_TYPE_PANIC;
        }

        if (BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_PANIC) {
            BOOT_LOG_ERR("panic!");
            assert(0);

            /* Loop forever... */
            FIH_PANIC;
        }
    }

#if defined(CONFIG_BOOT_KEYS_REVOCATION)
    if (BOOT_SWAP_TYPE(state) == BOOT_SWAP_TYPE_NONE) {
        allow_revoke();
    }
#endif
    /* Iterate over all the images. At this point all required update operations
     * have finished. By the end of the loop each image in the primary slot will
     * have been re-validated.
     */
    FIH_SET(fih_cnt, 0);
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        /* Hardenned to prevent from skipping check of a given image,
         * tmp_img_mask is declared volatile
         */
        volatile bool tmp_img_mask;
        FIH_SET(tmp_img_mask, state->img_mask[BOOT_CURR_IMG(state)]);
        if (FIH_EQ(tmp_img_mask, true)) {
            ++fih_cnt;
            continue;
        }
#endif
        if (BOOT_SWAP_TYPE(state) != BOOT_SWAP_TYPE_NONE) {
            /* Attempt to read an image header from each slot. Ensure that image
             * headers in slots are aligned with headers in boot_data.
	     * Note: Quite complicated internal logic of boot_read_image_headers
	     * uses boot state, the last parm, to figure out in which slot which
	     * header is located; when boot state is not provided, then it
	     * is assumed that headers are at proper slots (we are not in
	     * the middle of moving images, etc).
             */
            rc = boot_read_image_headers(state, false, NULL);
            if (rc != 0) {
                FIH_SET(fih_rc, FIH_FAILURE);
                goto out;
            }
            /* Since headers were reloaded, it can be assumed we just performed
             * a swap or overwrite. Now the header info that should be used to
             * provide the data for the bootstrap, which previously was at
             * secondary slot, was updated to primary slot.
             */
        }

#ifdef PM_S1_ADDRESS
        /* Patch needed for NCS. Image 1 primary is the currently
         * executing MCUBoot image, and is therefore already validated by NSIB and
         * does not need to also be validated by MCUBoot.
         */
        bool image_validated_by_nsib = BOOT_CURR_IMG(state) ==
                                       CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER;
#endif

#ifdef MCUBOOT_VALIDATE_PRIMARY_SLOT
#ifdef PM_S1_ADDRESS
        if (!image_validated_by_nsib)
#endif
        {
            FIH_CALL(boot_validate_slot, fih_rc, state, BOOT_PRIMARY_SLOT, NULL, 0);
            /* Check for all possible values is redundant in normal operation it
             * is meant to prevent FI attack.
             */
            if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS) ||
                FIH_EQ(fih_rc, FIH_FAILURE) ||
                FIH_EQ(fih_rc, FIH_NO_BOOTABLE_IMAGE)) {
                FIH_SET(fih_rc, FIH_FAILURE);
                goto out;
            }
        }
#else
        /* Even if we're not re-validating the primary slot, we could be booting
         * onto an empty flash chip. At least do a basic sanity check that
         * the magic number on the image is OK.
         */
        if (BOOT_IMG(state, BOOT_PRIMARY_SLOT).hdr.ih_magic != IMAGE_MAGIC) {
            BOOT_LOG_ERR("bad image magic 0x%lx; Image=%u", (unsigned long)
                         BOOT_IMG(state, BOOT_PRIMARY_SLOT).hdr.ih_magic,
                         BOOT_CURR_IMG(state));
            rc = BOOT_EBADIMAGE;
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }
#endif /* MCUBOOT_VALIDATE_PRIMARY_SLOT */

#ifdef PM_S1_ADDRESS
        if (!image_validated_by_nsib)
#endif
        {
        rc = boot_update_hw_rollback_protection(state);
        if (rc != 0) {
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }
        }

        rc = boot_add_shared_data(state, BOOT_PRIMARY_SLOT);
        if (rc != 0) {
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }
        ++fih_cnt;
    }
    /*
     * fih_cnt should be equal to BOOT_IMAGE_NUMBER now.
     * If this is not the case, at least one iteration of the loop
     * has been skipped.
     */
    if(FIH_NOT_EQ(fih_cnt, BOOT_IMAGE_NUMBER)) {
        FIH_PANIC;
    }

    fill_rsp(state, rsp);

    fih_rc = FIH_SUCCESS;
#if defined(CONFIG_BOOT_KEYS_REVOCATION)
    rc = revoke();
    if (rc != BOOT_KEY_REVOKE_OK &&
        rc != BOOT_KEY_REVOKE_NOT_READY) {
        FIH_SET(fih_rc, FIH_FAILURE);
    }
#endif /* CONFIG_BOOT_KEYS_REVOCATION */
out:
    /*
     * Since the boot_status struct stores plaintext encryption keys, reset
     * them here to avoid the possibility of jumping into an image that could
     * easily recover them.
     */
#if defined(MCUBOOT_ENC_IMAGES) || defined(MCUBOOT_SWAP_SAVE_ENCTLV)
    like_mbedtls_zeroize(&bs, sizeof(bs));
#else
    memset(&bs, 0, sizeof(struct boot_status));
#endif

    close_all_flash_areas(state);
    FIH_RET(fih_rc);
}

fih_ret
split_go(int loader_slot, int split_slot, void **entry)
{
    boot_sector_t *sectors;
    uintptr_t entry_val;
    int loader_flash_id;
    int split_flash_id;
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    sectors = malloc(BOOT_MAX_IMG_SECTORS * 2 * sizeof *sectors);
    if (sectors == NULL) {
        FIH_RET(FIH_FAILURE);
    }
    BOOT_IMG(&boot_data, loader_slot).sectors = sectors + 0;
    BOOT_IMG(&boot_data, split_slot).sectors = sectors + BOOT_MAX_IMG_SECTORS;

    loader_flash_id = flash_area_id_from_image_slot(loader_slot);
    rc = flash_area_open(loader_flash_id,
                         &BOOT_IMG_AREA(&boot_data, loader_slot));
    assert(rc == 0);
    split_flash_id = flash_area_id_from_image_slot(split_slot);
    rc = flash_area_open(split_flash_id,
                         &BOOT_IMG_AREA(&boot_data, split_slot));
    assert(rc == 0);

    /* Determine the sector layout of the image slots and scratch area. */
    rc = boot_read_sectors(&boot_data);
    if (rc != 0) {
        rc = SPLIT_GO_ERR;
        goto done;
    }

    rc = boot_read_image_headers(&boot_data, true, NULL);
    if (rc != 0) {
        goto done;
    }

    /* Don't check the bootable image flag because we could really call a
     * bootable or non-bootable image.  Just validate that the image check
     * passes which is distinct from the normal check.
     */
    FIH_CALL(split_image_check, fih_rc,
             boot_img_hdr(&boot_data, split_slot),
             BOOT_IMG_AREA(&boot_data, split_slot),
             boot_img_hdr(&boot_data, loader_slot),
             BOOT_IMG_AREA(&boot_data, loader_slot));
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        goto done;
    }

    entry_val = boot_img_slot_off(&boot_data, split_slot) +
                boot_img_hdr(&boot_data, split_slot)->ih_hdr_size;
    *entry = (void *) entry_val;
    rc = SPLIT_GO_OK;

done:
    flash_area_close(BOOT_IMG_AREA(&boot_data, split_slot));
    flash_area_close(BOOT_IMG_AREA(&boot_data, loader_slot));
    free(sectors);

    if (rc) {
        FIH_SET(fih_rc, FIH_FAILURE);
    }

    FIH_RET(fih_rc);
}

#else /* MCUBOOT_DIRECT_XIP || MCUBOOT_RAM_LOAD */

/**
 * Opens all flash areas and checks which contain an image with a valid header.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_get_slot_usage(struct boot_loader_state *state)
{
    uint32_t slot;
    int fa_id;
    int rc;
    struct image_header *hdr = NULL;

    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif
        /* Open all the slots */
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            fa_id = flash_area_id_from_multi_image_slot(
                                                BOOT_CURR_IMG(state), slot);
            rc = flash_area_open(fa_id, &BOOT_IMG_AREA(state, slot));
            assert(rc == 0);
        }

        /* Attempt to read an image header from each slot. */
        rc = boot_read_image_headers(state, false, NULL);
        if (rc != 0) {
            BOOT_LOG_WRN("Failed reading image headers.");
            return rc;
        }

        /* Check headers in all slots */
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            hdr = boot_img_hdr(state, slot);

            if (boot_is_header_valid(hdr, BOOT_IMG_AREA(state, slot), state)) {
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = true;
                BOOT_LOG_IMAGE_INFO(slot, hdr);
            } else {
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
                BOOT_LOG_INF("Image %d %s slot: Image not found",
                             BOOT_CURR_IMG(state),
                             (slot == BOOT_PRIMARY_SLOT)
                             ? "Primary" : "Secondary");
            }
        }

        state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
    }

    return 0;
}

/**
 * Finds the slot containing the image with the highest version number for the
 * current image.
 *
 * @param  state        Boot loader status information.
 *
 * @return              NO_ACTIVE_SLOT if no available slot found, number of
 *                      the found slot otherwise.
 */
static uint32_t
find_slot_with_highest_version(struct boot_loader_state *state)
{
    uint32_t slot;
    uint32_t candidate_slot = NO_ACTIVE_SLOT;
    int rc;

    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
        if (state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot]) {
            if (candidate_slot == NO_ACTIVE_SLOT) {
                candidate_slot = slot;
            } else {
                rc = boot_version_cmp(
                            &boot_img_hdr(state, slot)->ih_ver,
                            &boot_img_hdr(state, candidate_slot)->ih_ver);
                if (rc == 1) {
                    /* The version of the image being examined is greater than
                     * the version of the current candidate.
                     */
                    candidate_slot = slot;
                }
            }
        }
    }

    return candidate_slot;
}

#ifdef MCUBOOT_HAVE_LOGGING
/**
 * Prints the state of the loaded images.
 *
 * @param  state        Boot loader status information.
 */
static void
print_loaded_images(struct boot_loader_state *state)
{
    uint32_t active_slot;

    (void)state;

    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif
        active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

        BOOT_LOG_INF("Image %d loaded from the %s slot",
                     BOOT_CURR_IMG(state),
                     (active_slot == BOOT_PRIMARY_SLOT) ?
                     "primary" : "secondary");
    }
}
#endif

#if defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
/**
 * Checks whether the active slot of the current image was previously selected
 * to run. Erases the image if it was selected but its execution failed,
 * otherwise marks it as selected if it has not been before.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_select_or_erase(struct boot_loader_state *state)
{
    const struct flash_area *fap = NULL;
    int rc;
    uint32_t active_slot;
    struct boot_swap_state* active_swap_state;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

    fap = BOOT_IMG_AREA(state, active_slot);
    assert(fap != NULL);

    active_swap_state = &(state->slot_usage[BOOT_CURR_IMG(state)].swap_state);

    memset(active_swap_state, 0, sizeof(struct boot_swap_state));
    rc = boot_read_swap_state(fap, active_swap_state);
    assert(rc == 0);

    if (active_swap_state->magic != BOOT_MAGIC_GOOD ||
        (active_swap_state->copy_done == BOOT_FLAG_SET &&
         active_swap_state->image_ok  != BOOT_FLAG_SET)) {
        /*
         * A reboot happened without the image being confirmed at
         * runtime or its trailer is corrupted/invalid. Erase the image
         * to prevent it from being selected again on the next reboot.
         */
        BOOT_LOG_DBG("Erasing faulty image in the %s slot.",
                     (active_slot == BOOT_PRIMARY_SLOT) ? "primary" : "secondary");
        rc = boot_scramble_region(fap, 0, flash_area_get_size(fap), false);
        assert(rc == 0);
        rc = -1;
    } else {
        if (active_swap_state->copy_done != BOOT_FLAG_SET) {
            if (active_swap_state->copy_done == BOOT_FLAG_BAD) {
                BOOT_LOG_DBG("The copy_done flag had an unexpected value. Its "
                             "value was neither 'set' nor 'unset', but 'bad'.");
            }
            /*
             * Set the copy_done flag, indicating that the image has been
             * selected to boot. It can be set in advance, before even
             * validating the image, because in case the validation fails, the
             * entire image slot will be erased (including the trailer).
             */
            rc = boot_write_copy_done(fap);
            if (rc != 0) {
                BOOT_LOG_WRN("Failed to set copy_done flag of the image in "
                             "the %s slot.", (active_slot == BOOT_PRIMARY_SLOT) ?
                             "primary" : "secondary");
                rc = 0;
            }
        }
    }

    return rc;
}
#endif /* MCUBOOT_DIRECT_XIP && MCUBOOT_DIRECT_XIP_REVERT */

#ifdef MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER
/**
 * Tries to load a slot for all the images with validation.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
fih_ret
boot_load_and_validate_images(struct boot_loader_state *state)
{
    uint32_t active_slot;
    int rc;
    fih_ret fih_rc;
    uint32_t slot;

    /* Go over all the images and all slots and validate them */
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
#if BOOT_IMAGE_NUMBER > 1
            if (state->img_mask[BOOT_CURR_IMG(state)]) {
                continue;
            }
#endif

            /* Save the number of the active slot. */
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = slot;

#ifdef MCUBOOT_DIRECT_XIP
            rc = boot_rom_address_check(state);
            if (rc != 0) {
                /* The image is placed in an unsuitable slot. */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }

#ifdef MCUBOOT_DIRECT_XIP_REVERT
            rc = boot_select_or_erase(state);
            if (rc != 0) {
                /* The selected image slot has been erased. */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }
#endif /* MCUBOOT_DIRECT_XIP_REVERT */
#endif /* MCUBOOT_DIRECT_XIP */

#ifdef MCUBOOT_RAM_LOAD
            /* Image is first loaded to RAM and authenticated there in order to
             * prevent TOCTOU attack during image copy. This could be applied
             * when loading images from external (untrusted) flash to internal
             * (trusted) RAM and image is authenticated before copying.
             */
            rc = boot_load_image_to_sram(state);
            if (rc != 0 ) {
                /* Image cannot be ramloaded. */
                boot_remove_image_from_flash(state, slot);
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }
#endif /* MCUBOOT_RAM_LOAD */

            FIH_CALL(boot_validate_slot, fih_rc, state, slot, NULL, 0);
            if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
                /* Image is invalid. */
#ifdef MCUBOOT_RAM_LOAD
                boot_remove_image_from_sram(state);
#endif /* MCUBOOT_RAM_LOAD */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }

            /* Valid image loaded from a slot, go to the next slot. */
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
        }
    }

    /* Go over all the images and all slots and validate them */
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
        /* All slots tried until a valid image found. Breaking from this loop
         * means that a valid image found or already loaded. If no slot is
         * found the function returns with error code. */
        while (true) {
            /* Go over all the slots and try to load one */
            active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
            if (active_slot != NO_ACTIVE_SLOT){
                /* A slot is already active, go to next image. */
                break;
            }

            active_slot = find_slot_with_highest_version(state);
            if (active_slot == NO_ACTIVE_SLOT) {
                BOOT_LOG_INF("No slot to load for image %d",
                             BOOT_CURR_IMG(state));
                FIH_RET(FIH_FAILURE);
            }

            /* Save the number of the active slot. */
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = active_slot;

            /* Valid image loaded from a slot, go to the next image. */
            break;
        }
    }

    FIH_RET(FIH_SUCCESS);
}

#else /* MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER */

/**
 * Tries to load a slot for all the images with validation.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
fih_ret
boot_load_and_validate_images(struct boot_loader_state *state)
{
    uint32_t active_slot;
    int rc;
    fih_ret fih_rc;

    /* Go over all the images and try to load one */
    IMAGES_ITER(BOOT_CURR_IMG(state)) {
        /* All slots tried until a valid image found. Breaking from this loop
         * means that a valid image found or already loaded. If no slot is
         * found the function returns with error code. */
        while (true) {
            /* Go over all the slots and try to load one */
            active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
            if (active_slot != NO_ACTIVE_SLOT){
                /* A slot is already active, go to next image. */
                break;
            }

            active_slot = find_slot_with_highest_version(state);
            if (active_slot == NO_ACTIVE_SLOT) {
                BOOT_LOG_INF("No slot to load for image %d",
                             BOOT_CURR_IMG(state));
                FIH_RET(FIH_FAILURE);
            }

            /* Save the number of the active slot. */
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = active_slot;

#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif

#ifdef MCUBOOT_DIRECT_XIP
            rc = boot_rom_address_check(state);
            if (rc != 0) {
                /* The image is placed in an unsuitable slot. */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }

#ifdef MCUBOOT_DIRECT_XIP_REVERT
            rc = boot_select_or_erase(state);
            if (rc != 0) {
                /* The selected image slot has been erased. */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }
#endif /* MCUBOOT_DIRECT_XIP_REVERT */
#endif /* MCUBOOT_DIRECT_XIP */

#ifdef MCUBOOT_RAM_LOAD
            /* Image is first loaded to RAM and authenticated there in order to
             * prevent TOCTOU attack during image copy. This could be applied
             * when loading images from external (untrusted) flash to internal
             * (trusted) RAM and image is authenticated before copying.
             */
            rc = boot_load_image_to_sram(state);
            if (rc != 0 ) {
                /* Image cannot be ramloaded. */
                boot_remove_image_from_flash(state, active_slot);
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }
#endif /* MCUBOOT_RAM_LOAD */

            FIH_CALL(boot_validate_slot, fih_rc, state, active_slot, NULL, 0);
            if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
                /* Image is invalid. */
#ifdef MCUBOOT_RAM_LOAD
                boot_remove_image_from_sram(state);
#endif /* MCUBOOT_RAM_LOAD */
                state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
                state->slot_usage[BOOT_CURR_IMG(state)].active_slot = NO_ACTIVE_SLOT;
                continue;
            }

            /* Valid image loaded from a slot, go to next image. */
            break;
        }
    }

    FIH_RET(FIH_SUCCESS);
}
#endif /* MCUBOOT_VERSION_CMP_USE_SLOT_NUMBER */

/**
 * Updates the security counter for the current image.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
static int
boot_update_hw_rollback_protection(struct boot_loader_state *state)
{
#ifdef MCUBOOT_HW_ROLLBACK_PROT
    int rc;

    /* Update the stored security counter with the newer (active) image's
     * security counter value.
     */
#if defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
    /* When the 'revert' mechanism is enabled in direct-xip mode, the
     * security counter can be increased only after reboot, if the image
     * has been confirmed at runtime (the image_ok flag has been set).
     * This way a 'revert' can be performed when it's necessary.
     */
    if (state->slot_usage[BOOT_CURR_IMG(state)].swap_state.image_ok == BOOT_FLAG_SET) {
#endif
        rc = boot_update_security_counter(state,
                                          state->slot_usage[BOOT_CURR_IMG(state)].active_slot,
                                          state->slot_usage[BOOT_CURR_IMG(state)].active_slot);
        if (rc != 0) {
            BOOT_LOG_ERR("Security counter update failed after image %d validation.", BOOT_CURR_IMG(state));
            return rc;
        }
#if defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
    }
#endif

    return 0;

#else /* MCUBOOT_HW_ROLLBACK_PROT */
    (void) (state);
    return 0;
#endif
}

fih_ret
context_boot_go(struct boot_loader_state *state, struct boot_rsp *rsp)
{
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    rc = boot_get_slot_usage(state);
    if (rc != 0) {
        goto out;
    }

#if (BOOT_IMAGE_NUMBER > 1)
    while (true) {
#endif
        FIH_CALL(boot_load_and_validate_images, fih_rc, state);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }

#if (BOOT_IMAGE_NUMBER > 1)
        rc = boot_verify_dependencies(state);
        if (rc != 0) {
            /* Dependency check failed for an image, it has been removed from
             * SRAM in case of MCUBOOT_RAM_LOAD strategy, and set to
             * unavailable. Try to load an image from another slot.
             */
            continue;
        }
        /* Dependency check was successful. */
        break;
    }
#endif

    IMAGES_ITER(BOOT_CURR_IMG(state)) {
#if BOOT_IMAGE_NUMBER > 1
        if (state->img_mask[BOOT_CURR_IMG(state)]) {
            continue;
        }
#endif
        rc = boot_update_hw_rollback_protection(state);
        if (rc != 0) {
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }

        rc = boot_add_shared_data(state, (uint8_t)state->slot_usage[BOOT_CURR_IMG(state)].active_slot);
        if (rc != 0) {
            FIH_SET(fih_rc, FIH_FAILURE);
            goto out;
        }
    }

    /* All image loaded successfully. */
#ifdef MCUBOOT_HAVE_LOGGING
    print_loaded_images(state);
#endif

    fill_rsp(state, rsp);

out:
    close_all_flash_areas(state);

    if (rc != 0) {
        FIH_SET(fih_rc, FIH_FAILURE);
    }

    FIH_RET(fih_rc);
}
#endif /* MCUBOOT_DIRECT_XIP || MCUBOOT_RAM_LOAD */

/**
 * Prepares the booting process. This function moves images around in flash as
 * appropriate, and tells you what address to boot from.
 *
 * @param rsp                   On success, indicates how booting should occur.
 *
 * @return                      FIH_SUCCESS on success; nonzero on failure.
 */
fih_ret
boot_go(struct boot_rsp *rsp)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    boot_state_clear(NULL);

    FIH_CALL(context_boot_go, fih_rc, &boot_data, rsp);
    FIH_RET(fih_rc);
}

/**
 * Prepares the booting process, considering only a single image. This function
 * moves images around in flash as appropriate, and tells you what address to
 * boot from.
 *
 * @param rsp                   On success, indicates how booting should occur.
 *
 * @param image_id              The image ID to prepare the boot process for.
 *
 * @return                      FIH_SUCCESS on success; nonzero on failure.
 */
fih_ret
boot_go_for_image_id(struct boot_rsp *rsp, uint32_t image_id)
{
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    if (image_id >= BOOT_IMAGE_NUMBER) {
        FIH_RET(FIH_FAILURE);
    }

#if BOOT_IMAGE_NUMBER > 1
    memset(&boot_data.img_mask, 1, BOOT_IMAGE_NUMBER);
    boot_data.img_mask[image_id] = 0;
#endif

    FIH_CALL(context_boot_go, fih_rc, &boot_data, rsp);
    FIH_RET(fih_rc);
}

/**
 * Clears the boot state, so that previous operations have no effect on new
 * ones.
 *
 * @param state                 The state that should be cleared. If the value
 *                              is NULL, the default bootloader state will be
 *                              cleared.
 */
void boot_state_clear(struct boot_loader_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(struct boot_loader_state));
    } else {
        memset(&boot_data, 0, sizeof(struct boot_loader_state));
    }
}

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO)
/**
 * Reads image data to find out the maximum application sizes. Only needs to
 * be called in serial recovery mode, as the state information is unpopulated
 * at that time
 */
static void boot_fetch_slot_state_sizes(void)
{
    size_t slot;
    int rc = -1;
    int fa_id;
    int image_index;

    IMAGES_ITER(BOOT_CURR_IMG(&boot_data)) {
        int max_size = 0;

        image_index = BOOT_CURR_IMG(&boot_data);

        BOOT_IMG(&boot_data, BOOT_PRIMARY_SLOT).sectors =
            sector_buffers.primary[image_index];
        BOOT_IMG(&boot_data, BOOT_SECONDARY_SLOT).sectors =
            sector_buffers.secondary[image_index];
#if MCUBOOT_SWAP_USING_SCRATCH
        boot_data.scratch.sectors = sector_buffers.scratch;
#endif

        /* Open primary and secondary image areas for the duration
         * of this call.
         */
        for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
            fa_id = flash_area_id_from_multi_image_slot(image_index, slot);
            rc = flash_area_open(fa_id, &BOOT_IMG_AREA(&boot_data, slot));
            assert(rc == 0);

            if (rc != 0) {
                BOOT_LOG_DBG("boot_fetch_slot_state_sizes: error %d for %d",
                             rc, fa_id);
                goto finish;
            }
        }

#if MCUBOOT_SWAP_USING_SCRATCH
        rc = flash_area_open(FLASH_AREA_IMAGE_SCRATCH,
                             &BOOT_SCRATCH_AREA(&boot_data));
        assert(rc == 0);

        if (rc != 0) {
            goto finish;
        }
#endif

        /* Determine the sector layout of the image slots and scratch area. */
        rc = boot_read_sectors_recovery(&boot_data);

        if (rc == 0) {
            max_size = app_max_size(&boot_data);

            if (max_size > 0) {
                image_max_sizes[image_index].calculated = true;
                image_max_sizes[image_index].max_size = max_size;
            }
        }
    }

finish:
    close_all_flash_areas(&boot_data);
    memset(&boot_data, 0x00, sizeof(boot_data));
}
#endif

#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO) || defined(MCUBOOT_DATA_SHARING)
/**
 * Fetches the maximum allowed size of the image
 */
const struct image_max_size *boot_get_max_app_size(void)
{
#if defined(MCUBOOT_SERIAL_IMG_GRP_SLOT_INFO)
    uint8_t i = 0;

    while (i < BOOT_IMAGE_NUMBER) {
        if (image_max_sizes[i].calculated == true) {
            break;
        }

        ++i;
    }

    if (i == BOOT_IMAGE_NUMBER) {
        /* Information not available, need to fetch it */
        boot_fetch_slot_state_sizes();
    }
#endif

    return image_max_sizes;
}
#endif

#if defined(MCUBOOT_SWAP_USING_OFFSET)
uint32_t boot_get_state_secondary_offset(struct boot_loader_state *state,
                                         const struct flash_area *fap)
{
    if (state != NULL && BOOT_IMG_AREA(state, BOOT_SECONDARY_SLOT) == fap) {
        return state->secondary_offset[BOOT_CURR_IMG(state)];
    }

    return 0;
}
#endif
