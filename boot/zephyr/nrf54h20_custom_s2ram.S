/*
 * Copyright (c) 2025, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM Cortex-M suspend-to-RAM resume intermediary code (S2RAM)
 */



/*
 *             resume or
 *              reboot                                                  reboot==T
 *[S2RAM sleep]---------->[ boot (reset.S) ]---->[soc_early_reset_hook]---------->[regular boot]
 *                                                       |
 *                                                       |   resume==T
 *                                                       \/
 *                                                  [jump to well-known]
 *                                                  [ App reset vector ]
 */



#include <zephyr/toolchain.h>
#include <zephyr/arch/cpu.h>

GTEXT(pm_s2ram_mark_check_and_mediate)

GTEXT(soc_early_reset_hook)
SECTION_FUNC(TEXT, soc_early_reset_hook)
#if DT_NODE_EXISTS(DT_NODELABEL(pm_s2ram_stack)) &&\
	DT_NODE_HAS_COMPAT(DT_NODELABEL(pm_s2ram_stack), zephyr_memory_region)
     ldr r0, =DT_REG_ADDR(DT_NODELABEL(pm_s2ram_stack)) + DT_REG_SIZE(DT_NODELABEL(pm_s2ram_stack))
#else
#error "The support of bridge for S2RAM resume requires dedicated small stack"
#endif
     msr msp, r0
     push {r0, lr}
     bl pm_s2ram_mark_check_and_mediate
     pop {r0, pc}
