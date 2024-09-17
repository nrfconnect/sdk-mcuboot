/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __PM_SYSFLASH_H__
#define __PM_SYSFLASH_H__
/* Blocking the __SYSFLASH_H__ */
#define __SYSFLASH_H__

#include <pm_config.h>
#include <mcuboot_config/mcuboot_config.h>
#include <flash_map_pm.h>

#ifndef CONFIG_SINGLE_APPLICATION_SLOT

/* Each pair of slots is separated by , and there is no terminating character */
#define FLASH_AREA_IMAGE_0_SLOTS    PM_MCUBOOT_PRIMARY_ID, PM_MCUBOOT_SECONDARY_ID,
#define FLASH_AREA_IMAGE_1_SLOTS    PM_MCUBOOT_PRIMARY_1_ID, PM_MCUBOOT_SECONDARY_1_ID,
#define FLASH_AREA_IMAGE_2_SLOTS    PM_MCUBOOT_PRIMARY_2_ID, PM_MCUBOOT_SECONDARY_2_ID,
#define FLASH_AREA_IMAGE_3_SLOTS    PM_MCUBOOT_PRIMARY_3_ID, PM_MCUBOOT_SECONDARY_3_ID,

#ifndef LEGACY_CHILD_PARENT_BUILD
/* Sysbuild */
#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
#ifdef CONFIG_NCS_IS_VARIANT_IMAGE
#define MCUBOOT_S0_S1_SLOTS PM_S0_ID, PM_MCUBOOT_SECONDARY_ID,
#else
#define MCUBOOT_S0_S1_SLOTS PM_S1_ID, PM_MCUBOOT_SECONDARY_ID,
#endif
#else
#define MCUBOOT_S0_S1_SLOTS
#endif
#else
/* Legacy child/parent image */
#if defined(PM_B0_ADDRESS)
/* If B0 is present then two bootloaders are present, and we must use
 * a single secondary slot for both primary slots.
 */
extern uint32_t _image_1_primary_slot_id[];
#define MCUBOOT_S0_S1_SLOTS (uint32_t)_image_1_primary_slot_id, PM_MCUBOOT_SECONDARY_ID,
#define LEGACY_CHILD_PARENT_S0_S1_UPDATE_ENABLED 1
#define LEGACY_CHILD_PARENT_S0_S1_UPDATE_IMAGE_ID 1
#else
#define MCUBOOT_S0_S1_SLOTS
#endif  /* defined(PM_B0_ADDRESS) */
#if defined(PM_CPUNET_APP_ADDRESS)
#define LEGACY_CHILD_PARENT_NETWORK_CORE_UPDATE_ENABLED 1
#define LEGACY_CHILD_PARENT_NETWORK_CORE_UPDATE_IMAGE_ID 1
#endif /* defined(PM_CPUNET_APP_ADDRESS) */
#endif

#if ((MCUBOOT_IMAGE_NUMBER == 1) || (MCUBOOT_IMAGE_NUMBER == 2 && (CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 || (defined(LEGACY_CHILD_PARENT_BUILD) && defined(PM_B0_ADDRESS)))))
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS
#elif ((MCUBOOT_IMAGE_NUMBER == 2) || (MCUBOOT_IMAGE_NUMBER == 3 && (CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 || (defined(LEGACY_CHILD_PARENT_BUILD) && defined(PM_B0_ADDRESS)))))
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS
#elif ((MCUBOOT_IMAGE_NUMBER == 3) || (MCUBOOT_IMAGE_NUMBER == 4 && (CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1 || (defined(LEGACY_CHILD_PARENT_BUILD) && defined(PM_B0_ADDRESS)))))
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS \
                            FLASH_AREA_IMAGE_2_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 4)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS \
                            FLASH_AREA_IMAGE_2_SLOTS \
                            FLASH_AREA_IMAGE_3_SLOTS
#else
#error Unsupported number of images
#endif

static inline uint32_t __flash_area_ids_for_slot(int img, int slot)
{
    static const int all_slots[] = {
	ALL_AVAILABLE_SLOTS
	MCUBOOT_S0_S1_SLOTS
    };
    return all_slots[img * 2 + slot];
};

#undef FLASH_AREA_IMAGE_0_SLOTS
#undef FLASH_AREA_IMAGE_1_SLOTS
#undef FLASH_AREA_IMAGE_2_SLOTS
#undef FLASH_AREA_IMAGE_3_SLOTS
#undef MCUBOOT_S0_S1_SLOTS
#undef ALL_AVAILABLE_SLOTS

#define FLASH_AREA_IMAGE_PRIMARY(x) __flash_area_ids_for_slot(x, 0)
#define FLASH_AREA_IMAGE_SECONDARY(x) __flash_area_ids_for_slot(x, 1)

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE)
#define FLASH_AREA_IMAGE_SCRATCH    PM_MCUBOOT_SCRATCH_ID
#endif

#else /* CONFIG_SINGLE_APPLICATION_SLOT */

#define FLASH_AREA_IMAGE_PRIMARY(x)     PM_MCUBOOT_PRIMARY_ID
#define FLASH_AREA_IMAGE_SECONDARY(x)   PM_MCUBOOT_PRIMARY_ID
/* NOTE: Scratch parition is not used by single image DFU but some of
 * functions in common files reference it, so the definitions has been
 * provided to allow compilation of common units.
 */
#define FLASH_AREA_IMAGE_SCRATCH       0

#endif /* CONFIG_SINGLE_APPLICATION_SLOT */

#endif /* __PM_SYSFLASH_H__ */
