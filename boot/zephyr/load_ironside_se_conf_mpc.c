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

BUILD_ASSERT(MCUBOOT_IMAGE_NUMBER <= 4,
	     "The MPC override configuration used to provide write protection for the image "
	     "partitions does not currently support more than four images.");

/* Required alignment for OVERRIDE addresses in MPC110. */
#define OVERRIDE_ALIGNMENT_KB 4
#define OVERRIDE_ALIGNMENT    KB(OVERRIDE_ALIGNMENT_KB)

#define ACCESSIBLE_MRAM_START FIXED_PARTITION_NODE_ADDRESS(DT_CHOSEN(zephyr_code_partition))

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

#define BOOT_PARTITION_END                                                                         \
	(FIXED_PARTITION_NODE_ADDRESS(DT_CHOSEN(zephyr_code_partition)) +                          \
	 CONFIG_NCS_MCUBOOT_MPCCONF_STATIC_WRITE_PROTECTION_INITIAL_REGION_SIZE)

/* Partition slot addresses (MCUboot index order, not address-sorted)  */
#define PRIMARY_P0_START   FIXED_PARTITION_ADDRESS(slot0_partition)
#define PRIMARY_P0_END     (PRIMARY_P0_START + FIXED_PARTITION_SIZE(slot0_partition))
#define SECONDARY_P0_START FIXED_PARTITION_ADDRESS(slot1_partition)
#define SECONDARY_P0_END   (SECONDARY_P0_START + FIXED_PARTITION_SIZE(slot1_partition))

#if MCUBOOT_IMAGE_NUMBER > 1
#define PRIMARY_P1_START   FIXED_PARTITION_ADDRESS(slot2_partition)
#define PRIMARY_P1_END     (PRIMARY_P1_START + FIXED_PARTITION_SIZE(slot2_partition))
#define SECONDARY_P1_START FIXED_PARTITION_ADDRESS(slot3_partition)
#define SECONDARY_P1_END   (SECONDARY_P1_START + FIXED_PARTITION_SIZE(slot3_partition))
#endif

#if MCUBOOT_IMAGE_NUMBER > 2
#define PRIMARY_P2_START   FIXED_PARTITION_ADDRESS(slot4_partition)
#define PRIMARY_P2_END     (PRIMARY_P2_START + FIXED_PARTITION_SIZE(slot4_partition))
#define SECONDARY_P2_START FIXED_PARTITION_ADDRESS(slot5_partition)
#define SECONDARY_P2_END   (SECONDARY_P2_START + FIXED_PARTITION_SIZE(slot5_partition))
#endif

#if MCUBOOT_IMAGE_NUMBER > 3
#define PRIMARY_P3_START   FIXED_PARTITION_ADDRESS(slot6_partition)
#define PRIMARY_P3_END     (PRIMARY_P3_START + FIXED_PARTITION_SIZE(slot6_partition))
#define SECONDARY_P3_START FIXED_PARTITION_ADDRESS(slot7_partition)
#define SECONDARY_P3_END   (SECONDARY_P3_START + FIXED_PARTITION_SIZE(slot7_partition))
#endif

/* Median of three values. */
#define MEDIAN3(a, b, c) MAX(MIN((a), (b)), MIN(MAX((a), (b)), (c)))

/*
 * Ordering for four distinct values.
 * SORT4_2ND/3RD require all four inputs to be distinct.
 */
#define SORT4_1ST(a, b, c, d) MIN(MIN((a), (b)), MIN((c), (d)))
#define SORT4_4TH(a, b, c, d) MAX(MAX((a), (b)), MAX((c), (d)))
#define SORT4_2ND(a, b, c, d)                                                                      \
	MIN(MIN(MIN(MAX((a), (b)), MAX((a), (c))), MAX((a), (d))),                                 \
	    MIN(MIN(MAX((b), (c)), MAX((b), (d))), MAX((c), (d))))
#define SORT4_3RD(a, b, c, d)                                                                      \
	MAX(MAX(MAX(MIN((a), (b)), MIN((a), (c))), MIN((a), (d))),                                 \
	    MAX(MAX(MIN((b), (c)), MIN((b), (d))), MIN((c), (d))))

/*
 * Map a sorted ACTIVE start back to its partition end. First match wins,
 * so duplicates from unused-slot aliasing are safe.
 */
#define END_FOR_START(s0, e0, s1, e1, s2, e2, s3, e3, key)                                         \
	(((s0) == (key)) ? (e0) : ((s1) == (key)) ? (e1) : ((s2) == (key)) ? (e2) : (e3))

/*
 * MCUboot image indices (P0..P3) are not necessarily in address order in MRAM.
 * The macros below this are used to sort them to be able to add write permissions to the gaps
 * before, between, or after the partitions.
 *
 * When MCUBOOT_IMAGE_NUMBER < 4, unused ACTIVE_* values repeat and the inter-image gaps
 * have zero size. This is done just to simplify certain parts below, like the BUILD_ASSERTs.
 */
#if MCUBOOT_IMAGE_NUMBER == 1

#define PRIMARY_ACTIVE_0_START PRIMARY_P0_START
#define PRIMARY_ACTIVE_1_START PRIMARY_P0_START
#define PRIMARY_ACTIVE_2_START PRIMARY_P0_START
#define PRIMARY_ACTIVE_3_START PRIMARY_P0_START
#define PRIMARY_ACTIVE_0_END   PRIMARY_P0_END
#define PRIMARY_ACTIVE_1_END   PRIMARY_P0_END
#define PRIMARY_ACTIVE_2_END   PRIMARY_P0_END
#define PRIMARY_ACTIVE_3_END   PRIMARY_P0_END

#define SECONDARY_ACTIVE_0_START SECONDARY_P0_START
#define SECONDARY_ACTIVE_1_START SECONDARY_P0_START
#define SECONDARY_ACTIVE_2_START SECONDARY_P0_START
#define SECONDARY_ACTIVE_3_START SECONDARY_P0_START
#define SECONDARY_ACTIVE_0_END   SECONDARY_P0_END
#define SECONDARY_ACTIVE_1_END   SECONDARY_P0_END
#define SECONDARY_ACTIVE_2_END   SECONDARY_P0_END
#define SECONDARY_ACTIVE_3_END   SECONDARY_P0_END

#elif MCUBOOT_IMAGE_NUMBER == 2

#define PRIMARY_ACTIVE_0_START MIN(PRIMARY_P0_START, PRIMARY_P1_START)
#define PRIMARY_ACTIVE_1_START MAX(PRIMARY_P0_START, PRIMARY_P1_START)
#define PRIMARY_ACTIVE_2_START PRIMARY_ACTIVE_1_START
#define PRIMARY_ACTIVE_3_START PRIMARY_ACTIVE_1_START
#define PRIMARY_ACTIVE_0_END                                                                       \
	((PRIMARY_P0_START <= PRIMARY_P1_START) ? PRIMARY_P0_END : PRIMARY_P1_END)
#define PRIMARY_ACTIVE_1_END                                                                       \
	((PRIMARY_P0_START >= PRIMARY_P1_START) ? PRIMARY_P0_END : PRIMARY_P1_END)
#define PRIMARY_ACTIVE_2_END PRIMARY_ACTIVE_1_END
#define PRIMARY_ACTIVE_3_END PRIMARY_ACTIVE_1_END

#define SECONDARY_ACTIVE_0_START MIN(SECONDARY_P0_START, SECONDARY_P1_START)
#define SECONDARY_ACTIVE_1_START MAX(SECONDARY_P0_START, SECONDARY_P1_START)
#define SECONDARY_ACTIVE_2_START SECONDARY_ACTIVE_1_START
#define SECONDARY_ACTIVE_3_START SECONDARY_ACTIVE_1_START
#define SECONDARY_ACTIVE_0_END                                                                     \
	((SECONDARY_P0_START <= SECONDARY_P1_START) ? SECONDARY_P0_END : SECONDARY_P1_END)
#define SECONDARY_ACTIVE_1_END                                                                     \
	((SECONDARY_P0_START >= SECONDARY_P1_START) ? SECONDARY_P0_END : SECONDARY_P1_END)
#define SECONDARY_ACTIVE_2_END SECONDARY_ACTIVE_1_END
#define SECONDARY_ACTIVE_3_END SECONDARY_ACTIVE_1_END

#elif MCUBOOT_IMAGE_NUMBER == 3

#define PRIMARY_ACTIVE_0_START MIN(MIN(PRIMARY_P0_START, PRIMARY_P1_START), PRIMARY_P2_START)
#define PRIMARY_ACTIVE_1_START MEDIAN3(PRIMARY_P0_START, PRIMARY_P1_START, PRIMARY_P2_START)
#define PRIMARY_ACTIVE_2_START MAX(MAX(PRIMARY_P0_START, PRIMARY_P1_START), PRIMARY_P2_START)
#define PRIMARY_ACTIVE_3_START PRIMARY_ACTIVE_2_START
#define PRIMARY_ACTIVE_0_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P2_START, PRIMARY_P2_END,          \
		      PRIMARY_ACTIVE_0_START)
#define PRIMARY_ACTIVE_1_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P2_START, PRIMARY_P2_END,          \
		      PRIMARY_ACTIVE_1_START)
#define PRIMARY_ACTIVE_2_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P2_START, PRIMARY_P2_END,          \
		      PRIMARY_ACTIVE_2_START)
#define PRIMARY_ACTIVE_3_END PRIMARY_ACTIVE_2_END

#define SECONDARY_ACTIVE_0_START                                                                   \
	MIN(MIN(SECONDARY_P0_START, SECONDARY_P1_START), SECONDARY_P2_START)
#define SECONDARY_ACTIVE_1_START MEDIAN3(SECONDARY_P0_START, SECONDARY_P1_START, SECONDARY_P2_START)
#define SECONDARY_ACTIVE_2_START                                                                   \
	MAX(MAX(SECONDARY_P0_START, SECONDARY_P1_START), SECONDARY_P2_START)
#define SECONDARY_ACTIVE_3_START SECONDARY_ACTIVE_2_START
#define SECONDARY_ACTIVE_0_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P2_START, SECONDARY_P2_END,  \
		      SECONDARY_ACTIVE_0_START)
#define SECONDARY_ACTIVE_1_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P2_START, SECONDARY_P2_END,  \
		      SECONDARY_ACTIVE_1_START)
#define SECONDARY_ACTIVE_2_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P2_START, SECONDARY_P2_END,  \
		      SECONDARY_ACTIVE_2_START)
#define SECONDARY_ACTIVE_3_END SECONDARY_ACTIVE_2_END

#else /* MCUBOOT_IMAGE_NUMBER == 4 */

#define PRIMARY_ACTIVE_0_START                                                                     \
	SORT4_1ST(PRIMARY_P0_START, PRIMARY_P1_START, PRIMARY_P2_START, PRIMARY_P3_START)
#define PRIMARY_ACTIVE_1_START                                                                     \
	SORT4_2ND(PRIMARY_P0_START, PRIMARY_P1_START, PRIMARY_P2_START, PRIMARY_P3_START)
#define PRIMARY_ACTIVE_2_START                                                                     \
	SORT4_3RD(PRIMARY_P0_START, PRIMARY_P1_START, PRIMARY_P2_START, PRIMARY_P3_START)
#define PRIMARY_ACTIVE_3_START                                                                     \
	SORT4_4TH(PRIMARY_P0_START, PRIMARY_P1_START, PRIMARY_P2_START, PRIMARY_P3_START)
#define PRIMARY_ACTIVE_0_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P3_START, PRIMARY_P3_END,          \
		      PRIMARY_ACTIVE_0_START)
#define PRIMARY_ACTIVE_1_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P3_START, PRIMARY_P3_END,          \
		      PRIMARY_ACTIVE_1_START)
#define PRIMARY_ACTIVE_2_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P3_START, PRIMARY_P3_END,          \
		      PRIMARY_ACTIVE_2_START)
#define PRIMARY_ACTIVE_3_END                                                                       \
	END_FOR_START(PRIMARY_P0_START, PRIMARY_P0_END, PRIMARY_P1_START, PRIMARY_P1_END,          \
		      PRIMARY_P2_START, PRIMARY_P2_END, PRIMARY_P3_START, PRIMARY_P3_END,          \
		      PRIMARY_ACTIVE_3_START)

#define SECONDARY_ACTIVE_0_START                                                                   \
	SORT4_1ST(SECONDARY_P0_START, SECONDARY_P1_START, SECONDARY_P2_START, SECONDARY_P3_START)
#define SECONDARY_ACTIVE_1_START                                                                   \
	SORT4_2ND(SECONDARY_P0_START, SECONDARY_P1_START, SECONDARY_P2_START, SECONDARY_P3_START)
#define SECONDARY_ACTIVE_2_START                                                                   \
	SORT4_3RD(SECONDARY_P0_START, SECONDARY_P1_START, SECONDARY_P2_START, SECONDARY_P3_START)
#define SECONDARY_ACTIVE_3_START                                                                   \
	SORT4_4TH(SECONDARY_P0_START, SECONDARY_P1_START, SECONDARY_P2_START, SECONDARY_P3_START)
#define SECONDARY_ACTIVE_0_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P3_START, SECONDARY_P3_END,  \
		      SECONDARY_ACTIVE_0_START)
#define SECONDARY_ACTIVE_1_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P3_START, SECONDARY_P3_END,  \
		      SECONDARY_ACTIVE_1_START)
#define SECONDARY_ACTIVE_2_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P3_START, SECONDARY_P3_END,  \
		      SECONDARY_ACTIVE_2_START)
#define SECONDARY_ACTIVE_3_END                                                                     \
	END_FOR_START(SECONDARY_P0_START, SECONDARY_P0_END, SECONDARY_P1_START, SECONDARY_P1_END,  \
		      SECONDARY_P2_START, SECONDARY_P2_END, SECONDARY_P3_START, SECONDARY_P3_END,  \
		      SECONDARY_ACTIVE_3_START)

#endif

#define CHECK_MPC_ADDRESS_ALIGNMENT(_name_str, _addr)                                              \
	BUILD_ASSERT(((_addr) % OVERRIDE_ALIGNMENT) == 0,                                          \
		     _name_str " is not aligned to " STRINGIFY(OVERRIDE_ALIGNMENT_KB) "KiB")

CHECK_MPC_ADDRESS_ALIGNMENT("Bootloader partition", ACCESSIBLE_MRAM_START);
CHECK_MPC_ADDRESS_ALIGNMENT("MRAM end / secure storage", ACCESSIBLE_MRAM_END);
CHECK_MPC_ADDRESS_ALIGNMENT("Bootloader partition", BOOT_PARTITION_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_0_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_0_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_1_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_1_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_2_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_2_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_3_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one primary image partition", PRIMARY_ACTIVE_3_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_0_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_0_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_1_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_1_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_2_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_2_END);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_3_START);
CHECK_MPC_ADDRESS_ALIGNMENT("At least one secondary image partition", SECONDARY_ACTIVE_3_END);

/* MPC overrides used to implement image write protection.
 *
 * See the explanations on the various tables below to understand how they are used.
 * The main basis for selecting these is that they all accept OWNERID = None, meaning
 * that we can use them to implement write protection without forcing owner based isolation.
 * Overrides 1 and 2 are those used by IronSide SE to give full RWX access to MRAM in the
 * default configuration - therefore overwriting those overwrites the defaults.
 */

/* Override used for assigning R_X perms as a default. */
#define MPC110_OVERRIDE_DEFAULT_RX (uintptr_t)&NRF_MPC110->OVERRIDE[1]

/* Override used for assigning RWX perms to the range between the first write protected
 * region (bootloader code area by default, but can be extended) to the start of the first
 * active image area.
 */
#define MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[2]

/* Override used for assigning RWX perms between the end of the lower image and the start of
 * the next image by address (first inter-image gap when MCUBOOT_IMAGE_NUMBER > 1).
 */
#define MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[6]

/* Override used for assigning RWX perms between the end of the middle image and the start
 * of the next image by address (second inter-image gap when MCUBOOT_IMAGE_NUMBER > 2).
 */
#define MPC110_OVERRIDE_ACTIVE1_END_TO_ACTIVE2_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[7]

/* Override used for assigning RWX perms between the end of the third image by address and the
 * start of the fourth (third inter-image gap when MCUBOOT_IMAGE_NUMBER > 3).
 */
#define MPC110_OVERRIDE_ACTIVE2_END_TO_ACTIVE3_START_RWX (uintptr_t)&NRF_MPC110->OVERRIDE[10]

/* Override used for assigning RWX perms to the range between the end of the last active
 * image area and the end of the available MRAM.
 */
#define MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX                                 \
	(uintptr_t)&NRF_MPC110->OVERRIDE[11]

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
 * |----------------------------------------------------------------------------------------|
 * | MRAM                                                                                   |
 * | Bootloader |                                                         | (SECURESTORAGE) |
 * |----------------------------------------------------------------------------------------|
 * |    RX      |                                                         |                 |
 * |----------------------------------------------------------------------------------------|
 * |            |                          RW                             |                 |
 * |----------------------------------------------------------------------------------------|
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

/* The table(s) below contain the MPC110 override configuration loaded by MCUboot right before
 * booting the images, and represents the final MPC configuration (overwriting that set using UICR).
 *
 * The MPC override setup assumes a partition layout as described below.
 * Empty columns represent either inactive images or other space between the adjacent partitions.
 * Parentheses represent optional components.
 *
 * |----------------------------------------------------------------------------------------|
 * | MRAM                                                                                   |
 * | Bootloader |   | Act 0 |   | (Act 1) |   | (Act 2) |   | (Act 3) |   | (SECURESTORAGE) |
 * |----------------------------------------------------------------------------------------|
 * |                   RX                                                 |                 |
 * |----------------------------------------------------------------------------------------|
 * |            |RWX|       |RWX|         |RWX|         |RWX|         |RWX|                 |
 * |----------------------------------------------------------------------------------------|
 *
 * The goal of the override configuration is to disable write access to the active firmware regions
 * (bootloader + images contained in the slot) and keep write access enabled for other parts of
 * MRAM.
 *
 * Override 1:  R_X | [boot partition start - end of accessible MRAM]
 * Override 2:  RWX | [boot partition end - ACTIVE_0 partition start]
 * Override 6:  RWX | [ACTIVE_0 end - ACTIVE_1 start] (first inter-image gap when N > 1)
 * Override 7:  RWX | [ACTIVE_1 end - ACTIVE_2 start] (second inter-image gap when N > 2)
 * Override 10: RWX | [ACTIVE_2 end - ACTIVE_3 start] (third inter-image gap when N > 3)
 * Override 11: RWX | [last active partition end - end of accessible MRAM]
 *
 * Note that the effect of overlapping the R_X with RWX is RWX (perms are OR-ed).
 */
static const struct mpcconf_entry primary_entries[] = {
	/* R_X | [boot start - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true, MPC110_OVERRIDE_DEFAULT_RX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ false,
					    /* X */ true,
					    /* S */ false, ACCESSIBLE_MRAM_START),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},

#if PRIMARY_ACTIVE_0_START > BOOT_PARTITION_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, PRIMARY_ACTIVE_0_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif

#if MCUBOOT_IMAGE_NUMBER > 1
#if PRIMARY_ACTIVE_1_START > PRIMARY_ACTIVE_0_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true,
					    MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, PRIMARY_ACTIVE_0_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, PRIMARY_ACTIVE_1_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 1 */

#if MCUBOOT_IMAGE_NUMBER > 2
#if PRIMARY_ACTIVE_2_START > PRIMARY_ACTIVE_1_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true,
					    MPC110_OVERRIDE_ACTIVE1_END_TO_ACTIVE2_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, PRIMARY_ACTIVE_1_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, PRIMARY_ACTIVE_2_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 2 */

#if MCUBOOT_IMAGE_NUMBER > 3
#if PRIMARY_ACTIVE_3_START > PRIMARY_ACTIVE_2_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true,
					    /* ENABLE */ true,
					    MPC110_OVERRIDE_ACTIVE2_END_TO_ACTIVE3_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, PRIMARY_ACTIVE_2_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, PRIMARY_ACTIVE_3_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 3 */

#if ACCESSIBLE_MRAM_END > PRIMARY_ACTIVE_3_END
	/* RWX | [ACTIVE_3 end - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, PRIMARY_ACTIVE_3_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
};

#if defined(MCUBOOT_DIRECT_XIP)
static const struct mpcconf_entry secondary_entries[] = {
	/* R_X | [boot start - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(/* LOCK */ true, /* ENABLE */ true,
					    MPC110_OVERRIDE_DEFAULT_RX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ false,
					    /* X */ true,
					    /* S */ false, ACCESSIBLE_MRAM_START),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},

#if SECONDARY_ACTIVE_0_START > BOOT_PARTITION_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_INITIAL_END_TO_ACTIVE0_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, BOOT_PARTITION_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SECONDARY_ACTIVE_0_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif

#if MCUBOOT_IMAGE_NUMBER > 1
#if SECONDARY_ACTIVE_1_START > SECONDARY_ACTIVE_0_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true,
			/* ENABLE */ true, MPC110_OVERRIDE_ACTIVE0_END_TO_ACTIVE1_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(
			/* R */ true, /* W */ true, /* X */ true,
			/* S */ false, SECONDARY_ACTIVE_0_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SECONDARY_ACTIVE_1_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 1 */

#if MCUBOOT_IMAGE_NUMBER > 2
#if SECONDARY_ACTIVE_2_START > SECONDARY_ACTIVE_1_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true,
			/* ENABLE */ true, MPC110_OVERRIDE_ACTIVE1_END_TO_ACTIVE2_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(
			/* R */ true, /* W */ true, /* X */ true,
			/* S */ false, SECONDARY_ACTIVE_1_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SECONDARY_ACTIVE_2_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 2 */

#if MCUBOOT_IMAGE_NUMBER > 3
#if SECONDARY_ACTIVE_3_START > SECONDARY_ACTIVE_2_END
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true,
			/* ENABLE */ true, MPC110_OVERRIDE_ACTIVE2_END_TO_ACTIVE3_START_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(
			/* R */ true, /* W */ true, /* X */ true,
			/* S */ false, SECONDARY_ACTIVE_2_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, SECONDARY_ACTIVE_3_START),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
#endif /* MCUBOOT_IMAGE_NUMBER > 3 */

#if ACCESSIBLE_MRAM_END > SECONDARY_ACTIVE_3_END
	/* RWX | [ACTIVE_3 end - user end] */
	{
		MPCCONF_ENTRY_CONFIG0_VALUE(
			/* LOCK */ true, /* ENABLE */ true,
			MPC110_OVERRIDE_LAST_ACTIVE_END_TO_ACCESSIBLE_MRAM_END_RWX),
		MPCCONF_ENTRY_CONFIG1_VALUE(/* R */ true, /* W */ true,
					    /* X */ true,
					    /* S */ false, SECONDARY_ACTIVE_3_END),
		MPCCONF_ENTRY_CONFIG2_VALUE(NRF_OWNER_NONE, ACCESSIBLE_MRAM_END),
		MPCCONF_ENTRY_CONFIG3_VALUE(MASTERPORT_DEFAULT),
	},
#endif
};
#endif /* defined(MCUBOOT_DIRECT_XIP) */

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

	if (IN_RANGE(abs_addr, SECONDARY_P0_START, SECONDARY_P0_END - 1)
#if MCUBOOT_IMAGE_NUMBER > 1
	    || IN_RANGE(abs_addr, SECONDARY_P1_START, SECONDARY_P1_END - 1)
#endif
#if MCUBOOT_IMAGE_NUMBER > 2
	    || IN_RANGE(abs_addr, SECONDARY_P2_START, SECONDARY_P2_END - 1)
#endif
#if MCUBOOT_IMAGE_NUMBER > 3
	    || IN_RANGE(abs_addr, SECONDARY_P3_START, SECONDARY_P3_END - 1)
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
		entries = primary_entries;
		num_entries = ARRAY_SIZE(primary_entries);
		break;
	case BOOT_SLOT_SECONDARY:
		entries = secondary_entries;
		num_entries = ARRAY_SIZE(secondary_entries);
		break;
	default:
		/* Should not be possible. */
		return -1;
	}
#else
	entries = primary_entries;
	num_entries = ARRAY_SIZE(primary_entries);
#endif /* MCUBOOT_DIRECT_XIP */

	status = ironside_se_mpcconf_write(entries, num_entries);

	/* Finalization should be done regardless of the write result. */
	rc = ironside_se_mpcconf_finish_init();

	if (rc != 0 || status.status != 0) {
		return -1;
	}

	return 0;
}
