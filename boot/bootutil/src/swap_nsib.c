/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "bootutil/bootutil.h"
#include "bootutil_priv.h"
#include "swap_priv.h"
#include "bootutil/bootutil_log.h"

#include "mcuboot_config/mcuboot_config.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#if 0
uint32_t
find_last_idx(struct boot_loader_state *state, uint32_t swap_size)
{
    uint32_t sector_sz;
    uint32_t sz;
    uint32_t last_idx;

    sector_sz = boot_img_sector_size(state, BOOT_PRIMARY_SLOT, 0);
    sz = 0;
    last_idx = 0;
    while (1) {
        sz += sector_sz;
        last_idx++;
        if (sz >= swap_size) {
            break;
        }
    }

    return last_idx;
}
#endif

#if 0
static int app_max_sectors(struct boot_loader_state *state)
{
    uint32_t sz = 0;
    uint32_t sector_sz;
    uint32_t trailer_sz;
    uint32_t first_trailer_idx;

    sector_sz = boot_img_sector_size(state, BOOT_PRIMARY_SLOT, 0);
    trailer_sz = boot_trailer_sz(BOOT_WRITE_SZ(state));
    first_trailer_idx = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT) - 1;

    while (1) {
        sz += sector_sz;
        if  (sz >= trailer_sz) {
            break;
        }
        first_trailer_idx--;
    }

    return first_trailer_idx;
}
#endif

void nsib_swap_run(struct boot_loader_state *state, struct boot_status *bs)
{
//    uint32_t sz;
    uint32_t sector_sz;
//    uint32_t idx;
//    uint32_t trailer_sz;
//    uint32_t first_trailer_idx;
//    uint32_t last_idx;
    uint8_t image_index;
    const struct flash_area *fap_pri;
    const struct flash_area *fap_sec;
    int rc;

    BOOT_LOG_INF("Starting swap using nsib algorithm.");

    sector_sz = boot_img_sector_size(state, BOOT_SECONDARY_SLOT, 0);

#if (CONFIG_NCS_IS_VARIANT_IMAGE)
    rc = flash_area_open(PM_S0_ID, &fap_pri);
#else
    rc = flash_area_open(PM_S1_ID, &fap_pri);
#endif
    assert (rc == 0);

    /*
     * When starting a new swap upgrade, check that there is enough space.
     */
#if 0
    if (boot_status_is_reset(bs)) {
        sz = 0;
        trailer_sz = boot_trailer_sz(BOOT_WRITE_SZ(state));
        first_trailer_idx = boot_img_num_sectors(state, BOOT_PRIMARY_SLOT) - 1;

        while (1) {
            sz += sector_sz;
            if  (sz >= trailer_sz) {
                break;
            }
            first_trailer_idx--;
        }

        if (last_idx >= first_trailer_idx) {
            BOOT_LOG_WRN("Not enough free space to run swap upgrade");
            BOOT_LOG_WRN("required %d bytes but only %d are available",
                         (last_idx + 1) * sector_sz,
                         first_trailer_idx * sector_sz);
            bs->swap_type = BOOT_SWAP_TYPE_NONE;
            return;
        }
    }
#endif

    image_index = BOOT_CURR_IMG(state);

    rc = flash_area_open(FLASH_AREA_IMAGE_SECONDARY(image_index), &fap_sec);
    assert (rc == 0);

    rc = boot_erase_region(fap_pri, 0, fap_pri->fa_size);
    assert(rc == 0);

    rc = boot_copy_region(state, fap_sec, fap_pri, 0, 0, fap_pri->fa_size);
    assert(rc == 0);

    rc = swap_erase_trailer_sectors(state, fap_sec);
    assert(rc == 0);

    rc = boot_erase_region(fap_sec, 0, MIN((fap_pri->fa_size + sector_sz), fap_sec->fa_size));
    assert(rc == 0);

    flash_area_close(fap_pri);
    flash_area_close(fap_sec);
}
