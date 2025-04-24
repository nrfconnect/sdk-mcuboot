/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __NSIB_SYSFLASH_H__
#define __NSIB_SYSFLASH_H__
/* Blocking the __SYSFLASH_H__ */
#define __SYSFLASH_H__

#include <bootutil/nrf_partitions.h>
#include <mcuboot_config/mcuboot_config.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util_macro.h>

#ifndef CONFIG_SINGLE_APPLICATION_SLOT

/* Each pair of slots is separated by , and there is no terminating character */
#define FLASH_AREA_IMAGE_0_SLOTS    slot0_partition, slot1_partition,
#define FLASH_AREA_IMAGE_1_SLOTS    slot2_partition, slot3_partition,
#define FLASH_AREA_IMAGE_2_SLOTS    slot4_partition, slot5_partition,

#if CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1
#ifdef CONFIG_NCS_IS_VARIANT_IMAGE
#define MCUBOOT_S0_S1_SLOTS boot_partition, slot1_partition
#else
#define MCUBOOT_S0_S1_SLOTS s1_partition, slot1_partition
#endif
#else
#define MCUBOOT_S0_S1_SLOTS
#endif

#if (MCUBOOT_IMAGE_NUMBER == 1) || (MCUBOOT_IMAGE_NUMBER == 2 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            MCUBOOT_S0_S1_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 2) || (MCUBOOT_IMAGE_NUMBER == 3 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS \
                            MCUBOOT_S0_S1_SLOTS
#elif (MCUBOOT_IMAGE_NUMBER == 3) || (MCUBOOT_IMAGE_NUMBER == 4 && CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER != -1)
#define ALL_AVAILABLE_SLOTS FLASH_AREA_IMAGE_0_SLOTS \
                            FLASH_AREA_IMAGE_1_SLOTS \
                            FLASH_AREA_IMAGE_2_SLOTS \
                            MCUBOOT_S0_S1_SLOTS
#else
#error Unsupported number of images
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
#undef MCUBOOT_S0_S1_SLOTS
#undef ALL_AVAILABLE_SLOTS

#define FLASH_AREA_IMAGE_PRIMARY(x) __flash_area_ids_for_slot(x, 0)
#define FLASH_AREA_IMAGE_SECONDARY(x) __flash_area_ids_for_slot(x, 1)

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE)
#define FLASH_AREA_IMAGE_SCRATCH    PM_MCUBOOT_SCRATCH_ID
#endif

#else /* CONFIG_SINGLE_APPLICATION_SLOT */

#define FLASH_AREA_IMAGE_PRIMARY(x)     boot_partition
#define FLASH_AREA_IMAGE_SECONDARY(x)   boot_partition
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

#endif /* __NSIB_SYSFLASH_H__ */
