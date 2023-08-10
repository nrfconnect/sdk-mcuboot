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

#ifndef CONFIG_SINGLE_APPLICATION_SLOT

#if (MCUBOOT_IMAGE_NUMBER == 1)

#define FLASH_AREA_IMAGE_PRIMARY(x)     PM_MCUBOOT_PRIMARY_ID
#define FLASH_AREA_IMAGE_SECONDARY(x)   PM_MCUBOOT_SECONDARY_ID

#elif (MCUBOOT_IMAGE_NUMBER == 2)

/* If B0 is present then two bootloaders are present, and we must use
 * a single secondary slot for both primary slots.
 */
#if defined(PM_B0_ADDRESS)
extern uint32_t _image_1_primary_slot_id[];
#endif
#if defined(PM_B0_ADDRESS) && defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE)
#define FLASH_AREA_IMAGE_PRIMARY(x)             \
        ((x == 0) ?                             \
           PM_MCUBOOT_PRIMARY_ID :              \
         (x == 1) ?                             \
           PM_MCUBOOT_PRIMARY_1_ID :            \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x)           \
        ((x == 0) ?                             \
           PM_MCUBOOT_SECONDARY_ID:             \
        (x == 1) ?                              \
           PM_MCUBOOT_SECONDARY_1_ID:           \
           255 )
#elif defined(PM_B0_ADDRESS)

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
#else

#define FLASH_AREA_IMAGE_PRIMARY(x)             \
        ((x == 0) ?                             \
           PM_MCUBOOT_PRIMARY_ID :              \
         (x == 1) ?                             \
           PM_MCUBOOT_PRIMARY_1_ID :            \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x)           \
        ((x == 0) ?                             \
           PM_MCUBOOT_SECONDARY_ID:             \
        (x == 1) ?                              \
           PM_MCUBOOT_SECONDARY_1_ID:           \
           255 )

#endif /* PM_B0_ADDRESS */

#endif
#define FLASH_AREA_IMAGE_SCRATCH        PM_MCUBOOT_SCRATCH_ID

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
