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

#if (MCUBOOT_IMAGE_NUMBER == 2) && defined(PM_B0_ADDRESS)
/* If B0 is present then two bootloaders are present, and we must use
 * a single secondary slot for both primary slots.
 */
extern uint32_t _image_1_primary_slot_id[];
#endif /* (MCUBOOT_IMAGE_NUMBER == 2 && defined(PM_B0_ADDRESS) */

#if (MCUBOOT_IMAGE_NUMBER == 2) && defined(PM_B0_ADDRESS) && \
      !defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE)

#define FLASH_AREA_IMAGE_PRIMARY(x)             \
        ((x == 0) ?                             \
           PM_MCUBOOT_PRIMARY_ID :              \
         (x == 1) ?                             \
          (uint32_t)_image_1_primary_slot_id :  \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x)           \
        ((x == 0) ?                             \
            PM_MCUBOOT_SECONDARY_ID:            \
        (x == 1) ?                              \
           PM_MCUBOOT_SECONDARY_ID:             \
           255 )

#else  /* MCUBOOT_IMAGE_NUMBER == 2) && defined(PM_B0_ADDRESS) && \
        * !defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE)
        */

/* Each pair of slots is separated by , and there is no terminating character */
#define FLASH_AREA_IMAGE_0_SLOTS    PM_MCUBOOT_PRIMARY_ID, PM_MCUBOOT_SECONDARY_ID
#define FLASH_AREA_IMAGE_1_SLOTS    PM_MCUBOOT_PRIMARY_1_ID, PM_MCUBOOT_SECONDARY_1_ID
#define FLASH_AREA_IMAGE_2_SLOTS    PM_MCUBOOT_PRIMARY_2_ID, PM_MCUBOOT_SECONDARY_2_ID

#if (MCUBOOT_IMAGE_NUMBER == 1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 2)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS, \
                            FLASH_AREA_IMAGE_1_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 3)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS, \
                            FLASH_AREA_IMAGE_1_SLOTS, \
                            FLASH_AREA_IMAGE_2_SLOTS
#else
#error Unsupported number of images
#endif

static inline uint32_t __flash_area_ids_for_slot(int img, int slot)
{
    static const int all_slots[] = {
	ALL_AVAILABLE_SLOTS
    };
    return all_slots[img * 2 + slot];
};

#undef FLASH_AREA_IMAGE_0_SLOTS
#undef FLASH_AREA_IMAGE_1_SLOTS
#undef FLASH_AREA_IMAGE_2_SLOTS
#undef ALL_AVAILABLE_SLOTS

#define FLASH_AREA_IMAGE_PRIMARY(x) __flash_area_ids_for_slot(x, 0)
#define FLASH_AREA_IMAGE_SECONDARY(x) __flash_area_ids_for_slot(x, 1)

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE)
#define FLASH_AREA_IMAGE_SCRATCH    PM_MCUBOOT_SCRATCH_ID
#endif

#endif /* MCUBOOT_IMAGE_NUMBER == 2) && defined(PM_B0_ADDRESS) && \
        * !defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE)
        */

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
