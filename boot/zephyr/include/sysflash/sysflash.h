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

/* This is workaround because of bootutil public interface also requiring this
 * file. In reality application is supposed to maintain flash area access,
 * but bootutil uses own definitions, taken directly from DTS, which includes
 * this file. So every logic that is MCUboot specific, and will not apply
 * to an application has to be filtered out; therefore, if we are MCUboot,
 * for a build, only then we care to be a second stage bootloader.
 */
#if !defined(CONFIG_MCUBOOT)
#undef MCUBOOT_IS_SECOND_STAGE
#define MCUBOOT_IS_SECOND_STAGE 0
#endif

#if !defined(CONFIG_SINGLE_APPLICATION_SLOT) && !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP)

#if MCUBOOT_IS_SECOND_STAGE
/* Defined when building MCUboot for S1 */
/* We should be able to figure out which partition MCUboot is running from by
 * comparing zephyr,code-partition to either of recognized mcuboot_sx image.
 * It becomes problematic when variant image logic is used, because iformation
 * from DT will be the same for both images, unless overlay that changes this
 * is somehow applied; which does not happen currently.
 * We are left with comparing CONFIG_FLASH_LOAD_OFFSET against offset of a partition.
 * This will not work with external memory, so to be save comparison is done
 * against both partitions each time, to find situation when we can not find
 * a match.
 */
#if CONFIG_FLASH_LOAD_OFFSET == FIXED_PARTITION_OFFSET(mcuboot_s0)
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S0
#define SECOND_STAGE_ACTIVE_PARTITION           mcuboot_s0
#define SECOND_STAGE_INACTIVE_PARTITION         mcuboot_s1
#elif CONFIG_FLASH_LOAD_OFFSET == FIXED_PARTITION_OFFSET(mcuboot_s1)
#define SECOND_STAGE_MCUBOOT_RUNNING_FROM_S1
#define SECOND_STAGE_ACTIVE_PARTITION           mcuboot_s1
#define SECOND_STAGE_INACTIVE_PARTITION         mcuboot_s0
#else
#error "Failed to match MCUboot partition"
#endif

/* Header size within MCUboot bootable application image */
#define PROTECTED_REGION_START_SKIP CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX_SKIP_SIZE

#define SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET      \
    FIXED_PARTITION_OFFSET(SECOND_STAGE_ACTIVE_PARTITION)

#define SECOND_STAGE_ACTIVE_MCUBOOT_SIZE        \
    FIXED_PARTITION_SIZE(SECOND_STAGE_ACTIVE_PARTITION)

#define SECOND_STAGE_ACTIVE_MCUBOOT_ID          \
    FIXED_PARTITION_ID(SECOND_STAGE_ACTIVE_PARTITION)

#define SECOND_STAGE_INACTIVE_MCUBOOT_OFFSET      \
    FIXED_PARTITION_OFFSET(SECOND_STAGE_INACTIVE_PARTITION)

#define SECOND_STAGE_INACTIVE_MCUBOOT_SIZE        \
    FIXED_PARTITION_SIZE(SECOND_STAGE_INACTIVE_PARTITION)

#define SECOND_STAGE_INACTIVE_MCUBOOT_ID          \
    FIXED_PARTITION_ID(SECOND_STAGE_INACTIVE_PARTITION)

/* FPROTECT region covers both S0 and S1 slots. Assumption here is
 * that they precede PRIMARY application image partition, in flash
 * layout.
 */
#ifdef CONFIG_FPROTECT
#define FPROTECT_REGION_OFFSET  FIXED_PARTITION_OFFSET(mcuboot_s0)
#define FPROTECT_REGION_SIZE    \
    (FIXED_PARTITION_SIZE(mcuboot_s0) + FIXED_PARTITION_SIZE(mcuboot_s1))
#endif

/* RWX protection regions: the currently executing MCUboot is protecting itself */
#if CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
/* Note: The CONFIG_ROM_START_OFFSET is removed to save RWX bits on space
 * that do not require protection. When MCUboot is Second Stage bootloader
 * it does have header added, but it is not used by the NSIB.
 */
#define PROTECTED_REGION_START \
    (SECOND_STAGE_ACTIVE_MCUBOOT_OFFSET + CONFIG_ROM_START_OFFSET)
#define PROTECTED_REGION_SIZE  \
    (SECOND_STAGE_ACTIVE_MCUBOOT_SIZE - CONFIG_ROM_START_OFFSET)
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

/* Normally MCUboot needs one pair of slots per image it is able to process,
 * to be able to pre-allocate proper number of support objects;
 * same number is used to define number of slot pairs, one per image.
 *
 * Exception here is MCUBoot that serves as second stage image, because
 * in this configuration it brings one own pair and requires one set
 * of objects for it, so we set it one less to than number of images,
 * to bring one less default pair.
 */
#define MCUBOOT_NEEDED_SLOT_PARIRS      (MCUBOOT_IMAGE_NUMBER - 1)

#else /* MCUBOOT_IS_SECOND_STAGE != 0 */

/* We need one pair of slots per image */
#define MCUBOOT_NEEDED_SLOT_PARIRS      MCUBOOT_IMAGE_NUMBER

#endif /* MCUBOOT_IS_SECOND_STAGE */

static inline uint32_t __flash_area_ids_for_slot(int img, int slot)
{
    static const int all_slots[] = {
        FIXED_PARTITION_ID(slot0_partition), FIXED_PARTITION_ID(slot1_partition),
#if MCUBOOT_NEEDED_SLOT_PARIRS > 1
        FIXED_PARTITION_ID(slot2_partition), FIXED_PARTITION_ID(slot3_partition),
#endif
#if MCUBOOT_NEEDED_SLOT_PARIRS > 2
        FIXED_PARTITION_ID(slot4_partition), FIXED_PARTITION_ID(slot5_partition),
#endif
#if MCUBOOT_IS_SECOND_STAGE == 1
/* MCUboot as a second stage bootloader brings in two additional slots;
 * one slot is slot that it can update itself to and the other is source
 * of update. At this point source slot is, by default, secondary slot
 * of primary image.
 */
        SECOND_STAGE_INACTIVE_MCUBOOT_ID, FIXED_PARTITION_ID(slot1_partition)
#endif
    };
    return all_slots[img * 2 + slot];
};

#define FLASH_AREA_IMAGE_PRIMARY(x) __flash_area_ids_for_slot(x, 0)
#define FLASH_AREA_IMAGE_SECONDARY(x) __flash_area_ids_for_slot(x, 1)

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE) && !defined(CONFIG_BOOT_SWAP_USING_OFFSET)
#define FLASH_AREA_IMAGE_SCRATCH    FIXED_PARTITION_ID(scratch_partition)
#endif

#else /* !CONFIG_SINGLE_APPLICATION_SLOT && !CONFIG_MCUBOOT_BOOTLOADER_MODE_SINGLE_APP */

#define FLASH_AREA_IMAGE_PRIMARY(x)	FIXED_PARTITION_ID(slot0_partition)
#define FLASH_AREA_IMAGE_SECONDARY(x)	FIXED_PARTITION_ID(slot0_partition)

#endif /* CONFIG_SINGLE_APPLICATION_SLOT */

/* Here are protection ranges for primary stage MCUboot */
#if MCUBOOT_IS_SECOND_STAGE == 0
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
#endif

#endif /* __SYSFLASH_H__ */
