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

/* RWX protection regions, MCUboot is protecting itself */
#if CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
#define PROTECTED_REGION_START  FIXED_PARTITION_OFFSET(boot_partition)
#define PROTECTED_REGION_SIZE   FIXED_PARTITION_SIZE(boot_partition)
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

#endif /* __SYSFLASH_H__ */
