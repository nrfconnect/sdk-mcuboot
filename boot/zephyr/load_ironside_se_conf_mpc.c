/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "flash_map_backend/flash_map_backend.h"
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include <ironside/se/api.h>
#include <ironside/se/mpcconf.h>

BOOT_LOG_MODULE_DECLARE(mcuboot);

BUILD_ASSERT(MCUBOOT_IMAGE_NUMBER <= 2,
	     "The MPC override configuration used to provide write protection for the image "
	     "partitions does not currently support more than two images.");

/* Required alignment for OVERRIDE addresses in MPC110. */
#define OVERRIDE_ALIGNMENT (4UL * 1024UL)

/* clang-format off */
#define MIN_START_ADDR(_label0, _label1)                                                           \
	MIN(FIXED_PARTITION_ADDRESS(_label0),                                                    \
	    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(_label1)),                            \
	        (FIXED_PARTITION_ADDRESS(_label1)),                                              \
	        (UINTPTR_MAX)))

#define MIN_END_ADDR(_label0, _label1)                                                             \
	MIN((FIXED_PARTITION_ADDRESS(_label0) + FIXED_PARTITION_SIZE(_label0)),                    \
	    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(_label1)),                            \
	        ((FIXED_PARTITION_ADDRESS(_label1) + FIXED_PARTITION_SIZE(_label1))),              \
	        (UINTPTR_MAX)))

#define MAX_START_ADDR(_label0, _label1)                                                           \
	MAX(FIXED_PARTITION_ADDRESS(_label0),                                                      \
	    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(_label1)),                            \
	        (FIXED_PARTITION_ADDRESS(_label1)),                                                \
	        (0)))

#define MAX_END_ADDR(_label0, _label1)                                                             \
	MAX((FIXED_PARTITION_ADDRESS(_label0) + FIXED_PARTITION_SIZE(_label0)),                    \
	    COND_CODE_1(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(_label1)),                            \
	        ((FIXED_PARTITION_ADDRESS(_label1) + FIXED_PARTITION_SIZE(_label1))),              \
	        (0)))
/* clang-format on */

#define ACCESSIBLE_MRAM_START FIXED_PARTITION_NODE_ADDRESS(DT_CHOSEN(zephyr_code_partition))
BUILD_ASSERT((ACCESSIBLE_MRAM_START % OVERRIDE_ALIGNMENT) == 0);

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(secure_storage_partition))
BUILD_ASSERT((FIXED_PARTITION_ADDRESS(secure_storage_partition) +
	      FIXED_PARTITION_SIZE(secure_storage_partition)) ==
		     (DT_REG_ADDR(DT_NODELABEL(mram1x)) + DT_REG_SIZE(DT_NODELABEL(mram1x))),
	     "The MPC override configuration used to provide write protection for the image "
	     "partitions currently requires that the secure storage partitions are placed at the "
	     "end of MRAM.");

#define ACCESSIBLE_MRAM_END FIXED_PARTITION_ADDRESS(secure_storage_partition)
#else
#define ACCESSIBLE_MRAM_END (DT_REG_ADDR(DT_NODELABEL(mram1x)) + DT_REG_SIZE(DT_NODELABEL(mram1x)))
#endif
BUILD_ASSERT((ACCESSIBLE_MRAM_END % OVERRIDE_ALIGNMENT) == 0);

#define BOOT_PARTITION_END                                                                         \
	(FIXED_PARTITION_NODE_ADDRESS(DT_CHOSEN(zephyr_code_partition)) +                          \
	 CONFIG_NCS_MCUBOOT_MPCCONF_STATIC_WRITE_PROTECTION_INITIAL_REGION_SIZE)
BUILD_ASSERT((BOOT_PARTITION_END % OVERRIDE_ALIGNMENT) == 0);

/* Address ranges of the "active" images when booting slot 0.
 * We define:
 * - "active 0" as the slot 0 image with the lowest address.
 * - "active 1" as the slot 0 image with the highest address (if it exists).
 *
 * The final 4kB of each image area is left writable, to allow the booted firmware to write to the
 * end of the image trailer. If the the image trailer is less than 4kB, this means that the end of
 * the image firmware is also left writable.
 */
#define SLOT0_ACTIVE0_START MIN_START_ADDR(slot0_partition, slot2_partition)
BUILD_ASSERT((SLOT0_ACTIVE0_START % OVERRIDE_ALIGNMENT) == 0);

#define SLOT0_ACTIVE0_END (MIN_END_ADDR(slot0_partition, slot2_partition) - OVERRIDE_ALIGNMENT)
BUILD_ASSERT((SLOT0_ACTIVE0_END > SLOT0_ACTIVE0_START) &&
	     (SLOT0_ACTIVE0_END % OVERRIDE_ALIGNMENT) == 0);

#if MCUBOOT_IMAGE_NUMBER > 1
#define SLOT0_ACTIVE1_START MAX_START_ADDR(slot0_partition, slot2_partition)
BUILD_ASSERT((SLOT0_ACTIVE1_START % OVERRIDE_ALIGNMENT) == 0);

#define SLOT0_ACTIVE1_END (MAX_END_ADDR(slot0_partition, slot2_partition) - OVERRIDE_ALIGNMENT)
BUILD_ASSERT((SLOT0_ACTIVE1_END > SLOT0_ACTIVE1_START) &&
	     (SLOT0_ACTIVE1_END % OVERRIDE_ALIGNMENT) == 0);
#endif

/* Address ranges of the "active" images when booting slot 1.
 * We define:
 * - "active 0" as the slot 1 image with the lowest address.
 * - "active 1" as the slot 1 image with the highest address (if it exists).
 *
 * The final 4kB of each image area is left writable, to allow the booted firmware to write to the
 * end of the image trailer. If the the image trailer is less than 4kB, this means that the end of
 * the image firmware is also left writable.
 */
#define SLOT1_ACTIVE0_START MIN_START_ADDR(slot1_partition, slot3_partition)
BUILD_ASSERT((SLOT1_ACTIVE0_START % OVERRIDE_ALIGNMENT) == 0);

#define SLOT1_ACTIVE0_END (MIN_END_ADDR(slot1_partition, slot3_partition) - OVERRIDE_ALIGNMENT)
BUILD_ASSERT((SLOT1_ACTIVE0_END > SLOT1_ACTIVE0_START) &&
	     (SLOT1_ACTIVE0_END % OVERRIDE_ALIGNMENT) == 0);

#if MCUBOOT_IMAGE_NUMBER > 1
#define SLOT1_ACTIVE1_START MAX_START_ADDR(slot1_partition, slot3_partition)
BUILD_ASSERT((SLOT1_ACTIVE1_START % OVERRIDE_ALIGNMENT) == 0);

#define SLOT1_ACTIVE1_END (MAX_END_ADDR(slot1_partition, slot3_partition) - OVERRIDE_ALIGNMENT)
BUILD_ASSERT((SLOT1_ACTIVE1_END > SLOT1_ACTIVE1_START) &&
	     (SLOT1_ACTIVE1_END % OVERRIDE_ALIGNMENT) == 0);
#endif

/* MPC overrides used to implement image write protection.
 *
 * See the explanations on the various tables below to understand how they are used.
 * The main basis for selecting these is that they all accept OWNERID = None, meaning
 * that we can use them to implement write protection without forcing owner based isolation.
 * Overrides 1 and 2 are those used by IronSide SE to give full RWX access to MRAM in the default
 * configuration - therefore overwriting those overwrites the defaults.
 */

/* Override used for assigning R_X perms as a default. */
#define MPC110_OVERRIDE_DEFAULT_RX (uintptr_t)&NRF_MPC110->OVERRIDE[1]

/* Override used for assigning RWX perms to the range between the first write protected region
 * (bootloader code area by default, but can be extended) to the start of the first active image
 * area.
 */
#define MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[2]

/* Override used for assigning RWX perms to the range between the end of the first active image
 * area and the start of the second active image area, only used if there are two images in the
 * slot.
 */
#define MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[6]

/* Override used for assigning RWX perms to the range between the end of the last active
 * image area and the end of the available MRAM.
 */
#define MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX                                 \
	(uintptr_t)&NRF_MPC110->OVERRIDE[7]

/* Default masterport settings for the above MPC110 overrides. */
#define MASTERPORT_DEFAULT (BIT(2) | BIT(3) | BIT(6))

/* The 3 (2 if !DIRECT_XIP) tables below this implement the access permissions at the different
 * stages of the boot:
 *
 * - UICR.MPCCONF is loaded by IronSide SE before starting MCUboot.
 * - Slot 0 entries are loaded by MCUboot before starting the slot 0 application.
 *   This overwrites the UICR.MPCCONF configuration.
 * - (DIRECT_XIP only) Slot 1 entries are loaded by MCUboot before starting the slot 1 application.
 *   This overwrites the UICR.MPCCONF configuration.
 */

/* MPC override configuration set in UICR MPCCONF, which is loaded before MCUboot is started,
 * and is active until MCUboot loads the final configuration via IronSide SE IPC.
 * This configuration is overwritten again by MCUboot before jumping to the application, so
 * it is only active between when MCUboot is started at cold boot and before jumping to the
 * application. The goal of this configuration is to write protect the bootloader code partition
 * from before the Application core is booted by IronSide SE:
 *
 * Override 1: R_X | [boot partition start - boot partition end]
 * Override 2: RW  | [boot partition end - end of accessible MRAM]
 *
 * The '.mpcconf_entry' section is where the UICR generator image looks for static MPCCONF
 * entries to be extracted to UICR MPCCONF.
 */
static const struct mpcconf_entry uicr_entries[] __used Z_GENERIC_DOT_SECTION(mpcconf_entry) = {
	/* R_X | [boot start - boot end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ false,
					    /* ENABLE */ true, MPC110_OVERRIDE_DEFAULT_RX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ false,
					    /* X */ true,
					    /* S */ false, ACCESSIBLE_MRAM_START),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#if ACCESSIBLE_MRAM_END > BOOT_PARTITION_END
	/* RW | [boot end - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ false, /* ENABLE */ true,
			MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ false,
					    /* S */ false, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
	/* UICR.MPCCONF.MAXCOUNT may be larger than this array. We expect the UICR generator tool
	 * to fill the rest of the MPCCONF blob with 0xFFFF_FFFF.
	 */
};

#if MCUBOOT_IMAGE_NUMBER > 1
#define SLOT0_RX_END SLOT0_ACTIVE1_END
#define SLOT1_RX_END SLOT1_ACTIVE1_END
#else
#define SLOT0_RX_END SLOT0_ACTIVE0_END
#define SLOT1_RX_END SLOT1_ACTIVE0_END
#endif

/* The table(s) below contain the MPC110 override configuration loaded by MCUboot right booting
 * the images, and represents the final MPC configuration (overwriting the one set through UICR).
 *
 * The MPC override setup assumes a partition layout as described below.
 * Empty columns represent either inactive images or other space between the adjacent partitions.
 * Parentheses represent optional components.
 *
 * |---------------------------------------------------------------------|
 * | MRAM                                                                |
 * | Bootloader |    | Active 0 |    | (Active 1) |    | (SECURESTORAGE) |
 * |---------------------------------------------------------------------|
 * |                   RX                         |                      |
 * |---------------------------------------------------------------------|
 * |            | RW |          | RW |            | RW |                 |
 * |---------------------------------------------------------------------|
 *
 * The goal of the override configuration is to disable write access to the active firmware regions
 * (bootloader + images contained in the slot) and keep write access enabled for other parts of
 * MRAM.
 *
 * Override 1: R_X | [boot partition start - active 0 (active 1) partition end]
 * Override 2: RWX | [boot partition end - active 0 partition start]
 * Override 6: RWX | [active 0 partition end - active 1 partition start]
 * Override 7: RWX | [active 0 (active 1) partition end - end of accessible MRAM]
 *
 * Note that the effect of overlapping the R_X with RWX is RWX (perms are OR-ed).
 */
static const struct mpcconf_entry slot0_entries[] = {
	/* R_X | [boot start - active 1 end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true, MPC110_OVERRIDE_DEFAULT_RX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ false,
					    /* X */ true,
					    /* S */ false, ACCESSIBLE_MRAM_START),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT0_RX_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#if SLOT0_ACTIVE0_START > BOOT_PARTITION_END
	/* RWX | [boot end - active 0 start] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT0_ACTIVE0_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#if MCUBOOT_IMAGE_NUMBER > 1
#if SLOT0_ACTIVE1_START > SLOT0_ACTIVE0_END
	/* RWX | [active 0 end - active 1 start] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true,
					    MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, SLOT0_ACTIVE0_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT0_ACTIVE1_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif
#if ACCESSIBLE_MRAM_END > SLOT0_RX_END
	/* RWX | [active 1 end - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, SLOT0_RX_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
};

#if defined(MCUBOOT_DIRECT_XIP)
static const struct mpcconf_entry slot1_entries[] = {
	/* R_X | [boot start - active 1 end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true, /* ENABLE */ true,
					    MPC110_OVERRIDE_DEFAULT_RX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ false,
					    /* X */ true,
					    /* S */ false, ACCESSIBLE_MRAM_START),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT1_RX_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#if SLOT1_ACTIVE0_START > BOOT_PARTITION_END
	/* RWX | [boot end - active 0 start] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT1_ACTIVE0_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#if MCUBOOT_IMAGE_NUMBER > 1
#if SLOT1_ACTIVE1_START > SLOT1_ACTIVE0_END
	/* RWX | [active 0 end - active 1 start] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true,
			/* ENABLE */ true, MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(
			/* R */ true, /* W */ true, /* X */ true,
			/* S */ false, SLOT1_ACTIVE0_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SLOT1_ACTIVE1_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif
#if ACCESSIBLE_MRAM_END > SLOT1_RX_END
	/* RWX | [active 1 end - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, SLOT1_RX_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
};
#endif

#ifdef MCUBOOT_DIRECT_XIP
/* Slot to load MPCCONF for. */
static enum boot_slot mpcconf_slot = BOOT_SLOT_PRIMARY;

int nrf_mpcconf_update_active_slot(const struct boot_rsp *rsp)
{
	int rc;
	uintptr_t flash_base;

	rc = flash_device_base(rsp->br_flash_dev_id, &flash_base);
	if (rc != 0) {
		return -1;
	}

	const uintptr_t abs_addr = flash_base + rsp->br_image_off;

	if (IN_RANGE(abs_addr, SLOT1_ACTIVE0_START, SLOT1_ACTIVE0_END - 1)
#if MCUBOOT_IMAGE_NUMBER > 1
	    || IN_RANGE(abs_addr, SLOT1_ACTIVE1_START, SLOT1_ACTIVE1_END - 1)
#endif
	) {
		mpcconf_slot = BOOT_SLOT_SECONDARY;
	} else {
		mpcconf_slot = BOOT_SLOT_PRIMARY;
	}

	return 0;
}
#endif

/* @warning This function should always be called while executing from an area in
 * memory where permissions are not managed through the global domain MPCs (e.g. local RAM).
 * In the future this function may remove MCUboot's access to global domain memory.
 * Therefore, do not access any global memory during or after the call to
 * ironside_se_mpcconf_write().
 */
int __ramfunc nrf_load_mpcconf(void)
{
	int rc;
	struct ironside_se_mpcconf_status status = {0};

	size_t num_entries;
	const struct mpcconf_entry *entries = NULL;

#ifdef MCUBOOT_DIRECT_XIP
	switch (mpcconf_slot) {
	case BOOT_SLOT_PRIMARY:
		entries = slot0_entries;
		num_entries = ARRAY_SIZE(slot0_entries);
		break;
	case BOOT_SLOT_SECONDARY:
		entries = slot1_entries;
		num_entries = ARRAY_SIZE(slot1_entries);
		break;
	default:
		/* Should not be possible. */
		return -1;
	}
#else
	entries = slot0_entries;
	num_entries = ARRAY_SIZE(slot0_entries);
#endif /* MCUBOOT_DIRECT_XIP */

	status = ironside_se_mpcconf_write(entries, num_entries);

	/* Finalization should be done regardless of the write result. */
	rc = ironside_se_mpcconf_finish_init();

	if (rc != 0 || status.status != 0) {
		return -1;
	}

	return 0;
}
