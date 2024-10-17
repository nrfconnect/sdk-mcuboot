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

void nsib_swap_run(struct boot_loader_state *state, struct boot_status *bs)
{
    uint32_t sector_sz;
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
