/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
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

#include <assert.h>
#include <zephyr.h>
#include <device.h>
#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"
#include "flash_map_backend/flash_map_backend.h"

#ifdef CONFIG_FLASH_SIMULATOR
#define DT_DRV_COMPAT zephyr_sim_flash
#define SOC_NV_FLASH_NODE DT_CHILD(DT_DRV_INST(0), flash_sim_0)
#define FLASH_SIMULATOR_FLASH_SIZE DT_REG_SIZE(SOC_NV_FLASH_NODE)

extern uint8_t mock_flash[FLASH_SIMULATOR_FLASH_SIZE];
#endif

#define SECONDARY_SLOT 1


#ifdef CONFIG_SOC_NRF5340_CPUAPP
#include <dfu/pcd.h> 
#endif

/* @retval 0: header was read/populated
 *         FIH_FAILURE: image is invalid,
 *         BOOT_HOOK_REGULAR if hook not implemented for the image-slot,
 *         othervise an error-code value.
 */
int boot_read_image_header_hook(int img_index, int slot,
                                struct image_header *img_hed)
{
    if (img_index == 1 && slot == 0) {
            img_hed->ih_magic = IMAGE_MAGIC;
            return 0;
    }

    return BOOT_HOOK_REGULAR;
}

/* @retval FIH_SUCCESS: image is valid,
 *         FIH_FAILURE: image is invalid,
 *         fih encoded BOOT_HOOK_REGULAR if hook not implemented for
 *         the image-slot.
 */
fih_int boot_image_check_hook(int img_index, int slot)
{
    if (img_index == 1 && slot == 0) {
        FIH_RET(FIH_SUCCESS);
    }

    FIH_RET(fih_int_encode(BOOT_HOOK_REGULAR));
}

int boot_perform_update_hook(int img_index, struct image_header *img_head,
                             const struct flash_area *area)
{
    return BOOT_HOOK_REGULAR;
}

int boot_read_swap_state_primary_slot_hook(int image_index,
                                           struct boot_swap_state *state)
{
    if (image_index == 1) {
	/* Populate with fake data */
        state->magic = BOOT_MAGIC_UNSET;
        state->swap_type = BOOT_SWAP_TYPE_NONE;
        state->image_num = image_index;
        state->copy_done = BOOT_FLAG_UNSET;
        state->image_ok = BOOT_FLAG_UNSET;

	/* 
	 * Skip more handling of the primary slot for Image 1 as the slot
	 * exsists in RAM and is empty.
	 */
        return 0;
    }

    return BOOT_HOOK_REGULAR;
}

int network_core_update(int img_index, const struct flash_area *primary_fa)
{
#if CONFIG_FLASH_SIMULATOR
	uint32_t vtable_addr = 0;
	uint32_t *vtable = 0;
	uint32_t reset_addr = 0;
	
	const struct flash_area *secondary_fa; 
        int rc = flash_area_open(flash_area_id_from_multi_image_slot(
                    img_index,
                    SECONDARY_SLOT),
                &secondary_fa);
	if (rc != 0) {
		/* Failed to open flash area*/
		return rc;
	}

	struct image_header *hdr = (struct image_header *) mock_flash;
	if (hdr->ih_magic == IMAGE_MAGIC) {
		uint32_t fw_size = hdr->ih_img_size;
		vtable_addr = (uint32_t)hdr + hdr->ih_hdr_size;
		vtable = (uint32_t *)(vtable_addr);
		reset_addr = vtable[1];
		if (reset_addr > PM_CPUNET_B0N_ADDRESS) {
			int rc = pcd_network_core_update(vtable, fw_size);
			return rc;
		}
	} 
#endif

	/* No IMAGE_MAGIC no valid image */
	return -ENODATA;
}

int boot_copy_region_post_hook(int img_index, const struct flash_area *area,
                               size_t size)
{

    if (img_index == 1) {
	    network_core_update(img_index, area);
    }
    return 0;
}

int boot_serial_uploaded_hook(int img_index, const struct flash_area *area,
                               size_t size)
{
	if (img_index == 1) {
		return network_core_update(img_index, area);
	}

	return BOOT_HOOK_REGULAR;
}
