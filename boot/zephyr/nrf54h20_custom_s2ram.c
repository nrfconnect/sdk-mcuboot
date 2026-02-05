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

#include "bootutil/fault_injection_hardening.h"

#if DT_NODE_EXISTS(DT_NODELABEL(mcuboot_s2ram)) &&\
    DT_NODE_HAS_COMPAT(DT_NODELABEL(mcuboot_s2ram), zephyr_memory_region)
/* Linker section name is given by `zephyr,memory-region` property of
 * `zephyr,memory-region` compatible DT node with nodelabel `mcuboot_s2ram`.
 */
__attribute__((section(DT_PROP(DT_NODELABEL(mcuboot_s2ram), zephyr_memory_region))))
volatile struct mcuboot_resume_s mcuboot_resume;
#else
#error  "mcuboot resume support section not defined in dts"
#endif

#define FIXED_PARTITION_ADDR(node_label) DT_REG_ADDR(DT_NODELABEL(node_label))

#define S2RAM_SLOT_INFO_A 0x37
#define S2RAM_SLOT_INFO_B 0xA4

#ifdef CONFIG_BOOT_DIRECT_XIP
/* Called by the image manager when setting the image as active for current boot. */
void s2ram_designate_slot(uint8_t slot)
{
    if (slot == 0) {
        mcuboot_resume.slot_info = S2RAM_SLOT_INFO_A;
    } else {
        mcuboot_resume.slot_info = S2RAM_SLOT_INFO_B;
    }
}
#endif

struct arm_vector_table {
    uint32_t msp;
    uint32_t reset;
};

/* This could be read from slot's image_header.ih_hdr_size, but immediate value
 * is much faster to reach
 */
#define APP_EXE_START_OFFSET 0x800 /* nRF54H20 */

void pm_s2ram_mark_check_and_mediate(void)
{
    uint32_t reset_reason = nrf_resetinfo_resetreas_local_get(NRF_RESETINFO);

    if (reset_reason != NRF_RESETINFO_RESETREAS_LOCAL_UNRETAINED_MASK) {
        /* Normal boot */
        return;
    }

    /* S2RAM resume expected, do doublecheck */
    if (mcuboot_resume.magic == MCUBOOT_S2RAM_RESUME_MAGIC) {
        /* clear magic to avoid accidental reuse */
        mcuboot_resume.magic = 0;
    } else {
        /* magic not valid, normal boot */
        goto resume_failed;
    }

    /* s2ram boot */
    struct arm_vector_table *vt;

#ifdef CONFIG_BOOT_DIRECT_XIP
    if (mcuboot_resume.slot_info == S2RAM_SLOT_INFO_A) {
        vt = (struct arm_vector_table *)
                 (FIXED_PARTITION_ADDR(slot0_partition) + APP_EXE_START_OFFSET);
    } else if (mcuboot_resume.slot_info == S2RAM_SLOT_INFO_B) {
        vt = (struct arm_vector_table *)
                 (FIXED_PARTITION_ADDR(slot1_partition) + APP_EXE_START_OFFSET);
    }  else {
        /* invalid slot info */
        goto resume_failed;
    }
#else
    vt = (struct arm_vector_table *)
            (FIXED_PARTITION_ADDR(slot0_partition) + APP_EXE_START_OFFSET);
#endif

    /* Jump to application */
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

resume_failed:
    FIH_PANIC;
}
