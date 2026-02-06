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

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
#ifdef CONFIG_NCS_IS_VARIANT_IMAGE
#define MCUBOOT_S0_S1_SLOTS PM_S0_ID, PM_MCUBOOT_SECONDARY_ID,
#else
#define MCUBOOT_S0_S1_SLOTS PM_S1_ID, PM_MCUBOOT_SECONDARY_ID,
#endif
#else
#define MCUBOOT_S0_S1_SLOTS
#endif

#if (MCUBOOT_IMAGE_NUMBER == 1) || (MCUBOOT_IMAGE_NUMBER == 2 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 2) || (MCUBOOT_IMAGE_NUMBER == 3 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 3) || (MCUBOOT_IMAGE_NUMBER == 4 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
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

#ifndef SOC_FLASH_0_ID
#define SOC_FLASH_0_ID 0
#endif

#ifndef SPI_FLASH_0_ID
#define SPI_FLASH_0_ID 1
#endif

/* Support for NETCPU application image updates */
#if CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER != -1
#define NETCPU_APP_SLOT_OFFSET  PM_CPUNET_APP_ADDRESS
#define NETCPU_APP_SLOT_SIZE    (PM_CPUNET_APP_END_ADDRESS - PM_CPUNET_APP_ADDRESS)
#define NETCPU_APP_SLOT_END     PM_CPUNET_APP_END_ADDRESS
#endif


/* Is this upgradeable MCUuboot in NSIB configuration */
#ifdef MCUBOOT_IS_SECOND_STAGE
/* Note: when NSIB is running it will boot MCUboot from one of
 * slots designated as S0 or S1. This MCUboot will be in charge
 * of handling update and/or boot of an application image and MCUboot in
 * opposite Sx slot, so if it is running from S0 it will update S1
 * and opposite.
 * In both above situations the slot where the update comes from
 * is the SECONDARY slot, in MCUboot nomenclature. MCUboot natively
 * has not been able to process more than two slots for every image,
 * so there is additional logic to target slot outside of the
 * PRIMARY and SECONDARY slot, which would be slot of the Sx variant
 * of MCUboot that is not currently running.
 * The MCUboot update is always built for specific Sx slot and will
 * not work from the opposite Sx slot.
 */

/* If defined then this build is for S1 instead of S0, which is default */
#ifdef CONFIG_NCS_IS_VARIANT_IMAGE
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S1
#else
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S0
#endif

/* Header size within MCUboot bootable application image */
#define APP_IMAGE_HEADER_SIZE CONFIG_PM_PARTITION_SIZE_MCUBOOT_PAD

#ifdef SECOND_STAGE_MCUBOOT_RUNNING_FROM_S0
#define SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET     PM_S0_OFFSET
#define SECOND_STAGE_ACTIVE_MCUBOOT_SIZE       PM_S0_SIZE
#define SECOND_STAGE_ACTIVE_MCUBOOT_ID         PM_S0_ID
#define SECOND_STAGE_INACTIVE_MCUBOOT_OFFSET   PM_S1_OFFSET
#define SECOND_STAGE_INACTIVE_MCUBOOT_SIZE     PM_S1_SIZE
#define SECOND_STAGE_INACTIVE_MCUBOOT_ID       PM_S1_ID
#endif

#ifdef SECOND_STAGE_MCUBOOT_RUNNING_FROM_S1
#define SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET     PM_S1_OFFSET
#define SECOND_STAGE_ACTIVE_MCUBOOT_SIZE       PM_S1_SIZE
#define SECOND_STAGE_ACTIVE_MCUBOOT_ID         PM_S1_ID
#define SECOND_STAGE_INACTIVE_MCUBOOT_OFFSET   PM_S0_OFFSET
#define SECOND_STAGE_INACTIVE_MCUBOOT_SIZE     PM_S0_SIZE
#define SECOND_STAGE_INACTIVE_MCUBOOT_ID       PM_S0_ID
#endif

/* FPROTECT region covers both S0 and S1 slots. Assumption here is
 * that they precede PRIMARY application image partition, in flash
 * layout.
 */
#ifdef CONFIG_FPROTECT
#define FPROTECT_REGION_OFFSET  (PM_S0_ADDRESS)
#define FPROTECT_REGION_SIZE    (PM_MCUBOOT_PRIMARY_ADDRESS - FPROTECT_REGION_OFFSET)
#endif

/* RWX protection regions: the currently executing MCUboot is protecting itself */
#if CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
/* Note: The APP_IMAGE_HEADER_SIZE is removed to save RWX bits on space
 * that do not require protection. When MCUboot is Second Stage bootloader
 * it does have header added, but it is not used by the NSIB.
 */
#define PROTECTED_REGION_START \
    (SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET + APP_IMAGE_HEADER_SIZE)
#define PROTECTED_REGION_SIZE  \
    (SECOND_STAGE_ACTIVE_MCUBOOT_SIZE - APP_IMAGE_HEADER_SIZE)
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

#else /* MCUBOOT_IS_SECOND_STAGE */

/* These are not used. They are set to 0 to avoid compiler seeing
 * undefined variables in code that gets thrown out in the end.
 */
#define SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET     0
#define SECOND_STAGE_ACTIVE_MCUBOOT_SIZE       0
#define SECOND_STAGE_ACTIVE_MCUBOOT_ID         0
#define SECOND_STAGE_INACTIVE_MCUBOOT_OFFSET   0
#define SECOND_STAGE_INACTIVE_MCUBOOT_SIZE     0
#define SECOND_STAGE_INACTIVE_MCUBOOT_ID       0

/* FPROTECT region covers MCUboot only */
#ifdef CONFIG_FPROTECT
#define FPROTECT_REGION_OFFSET  PM_MCUBOOT_ADDRESS
#define FPROTECT_REGION_SIZE    FPROTECT_ALIGN_UP(PM_MCUBOOT_SIZE)
#endif

/* RWX protection regions, MCUboot is protecting itself */
#if CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
#define PROTECTED_REGION_START  PM_MCUBOOT_ADDRESS
#define PROTECTED_REGION_SIZE   PM_MCUBOOT_SIZE
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

#endif /* MCUBOOT_IS_SECOND_STAGE */

#endif /* __PM_SYSFLASH_H__ */
