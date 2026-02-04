/*
 * Copyright (c) 2023-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef CONFIG_FPROTECT
/* Round up to next CONFIG_FPROTECT_BLOCK_SIZE boundary.
 * This is used for backwards compatibility, as some applications
 * use MCUBoot size unaligned to CONFIG_FPROTECT_BLOCK_SIZE.
 * However, even in these cases, the start of the next area
 * was still aligned to CONFIG_FPROTECT_BLOCK_SIZE and the
 * remaining space was filled by an EMPTY section by partition manager.
 */
#define FPROTECT_ALIGN_UP(x) \
    ((((x) + CONFIG_FPROTECT_BLOCK_SIZE - 1) / CONFIG_FPROTECT_BLOCK_SIZE) * \
     CONFIG_FPROTECT_BLOCK_SIZE)
#endif

#if USE_PARTITION_MANAGER
#include <pm_config.h>
/* Blocking the rest of the file */
#define __SYSFLASH_H__
#include <sysflash/pm_sysflash.h>

/* Is this upgradeable MCUuboot in NSIB configuration */
#if MCUBOOT_IS_SECOND_STAGE
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

/* Defined when building MCUboot for S1 */
#ifdef CONFIG_NCS_IS_VARIANT_IMAGE
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S1
#else
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S0
#endif

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

#endif /* MCUBOOT_IS_SECOND_STAGE */

#endif

/* Here starts non-Partition Manager configuration */

#ifndef __SYSFLASH_H__
#define __SYSFLASH_H__

#include <mcuboot_config/mcuboot_config.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util_macro.h>

#ifndef SOC_FLASH_0_ID
#define SOC_FLASH_0_ID 0
#endif

#ifndef SPI_FLASH_0_ID
#define SPI_FLASH_0_ID 1
#endif

#if !defined(CONFIG_SINGLE_APPLICATION_SLOT) && !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP)

/* Each pair of slots is separated by , and there is no terminating character */
#define FLASH_AREA_IMAGE_0_SLOTS    slot0_partition, slot1_partition
#define FLASH_AREA_IMAGE_1_SLOTS    slot2_partition, slot3_partition
#define FLASH_AREA_IMAGE_2_SLOTS    slot4_partition, slot5_partition

#if (MCUBOOT_IMAGE_NUMBER == 1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 2)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS, \
                            FLASH_AREA_IMAGE_1_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 3)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS, \
                            FLASH_AREA_IMAGE_1_SLOTS, \
                            FLASH_AREA_IMAGE_2_SLOTS
#endif

static inline uint32_t __flash_area_ids_for_slot(int img, int slot)
{
    static const int all_slots[] = {
        FOR_EACH_NONEMPTY_TERM(FIXED_PARTITION_ID, (,), ALL_AVAILABLE_SLOTS)
    };
    return all_slots[img * 2 + slot];
};

#undef FLASH_AREA_IMAGE_0_SLOTS
#undef FLASH_AREA_IMAGE_1_SLOTS
#undef FLASH_AREA_IMAGE_2_SLOTS
#undef ALL_AVAILABLE_SLOTS

#define FLASH_AREA_IMAGE_PRIMARY(x) __flash_area_ids_for_slot(x, 0)
#define FLASH_AREA_IMAGE_SECONDARY(x) __flash_area_ids_for_slot(x, 1)

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE) && !defined(CONFIG_BOOT_SWAP_USING_OFFSET)
#define FLASH_AREA_IMAGE_SCRATCH    FIXED_PARTITION_ID(scratch_partition)
#endif

#else /* !CONFIG_SINGLE_APPLICATION_SLOT && !CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP */

#define FLASH_AREA_IMAGE_PRIMARY(x)	FIXED_PARTITION_ID(slot0_partition)
#define FLASH_AREA_IMAGE_SECONDARY(x)	FIXED_PARTITION_ID(slot0_partition)

#endif /* CONFIG_SINGLE_APPLICATION_SLOT */

/* Protecting MCUboot partition */
#ifdef CONFIG_FPROTECT
#define FPROTECT_REGION_OFFSET  FIXED_PARTITION_OFFSET(boot_partition)
#define FPROTECT_REGION_SIZE    FIXED_PARTITION_SIZE(boot_partition)
#endif

#endif /* __SYSFLASH_H__ */
