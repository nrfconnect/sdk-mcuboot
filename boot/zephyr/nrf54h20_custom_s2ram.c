/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <zephyr/arch/common/pm_s2ram.h>
#include <hal/nrf_resetinfo.h>
#include "pm_s2ram.h"
#include "power.h"

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>

int soc_s2ram_suspend(pm_s2ram_system_off_fn_t system_off)
{
	(void)(system_off);
	return -1;
}

void pm_s2ram_mark_set(void)
{
	/* empty */
}

struct arm_vector_table {
    uint32_t msp;
    uint32_t reset;
};

bool pm_s2ram_mark_check_and_clear(void)
{
	uint32_t reset_reason = nrf_resetinfo_resetreas_local_get(NRF_RESETINFO);

	if (reset_reason != NRF_RESETINFO_RESETREAS_LOCAL_UNRETAINED_MASK) {
		// normal boot
		return false;
	}

	// s2ram boot
    struct arm_vector_table *vt;
    vt = (struct arm_vector_table *)(FIXED_PARTITION_OFFSET(slot0_partition) + 0x800);

	// Jump to application
    __asm__ volatile (
        /* vt->reset -> r0 */
        "   mov     r0, %0\n"
        /* vt->msp -> r1 */
        "   mov     r1, %1\n"
        /* set stack pointer */
        "   msr     msp, r1\n"
        /* jump to reset vector of an app */
        "   bx      r0\n"
        :
        : "r" (vt->reset), "r" (vt->msp)
        : "r0", "r1", "memory"
    );

	while(1)
	{
	}

	return true;
}
