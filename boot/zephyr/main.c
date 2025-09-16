/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2020 Arm Limited
 * Copyright (c) 2021-2023 Nordic Semiconductor ASA
 * Copyright (c) 2025 Aerlync Labs Inc.
 * Copyright (c) 2025 Siemens Mobility GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/usb/usb_device.h>
#include <soc.h>
#include <zephyr/linker/linker-defs.h>

#if defined(CONFIG_BOOT_DISABLE_CACHES)
#include <zephyr/cache.h>
#endif

#if defined(CONFIG_CPU_CORTEX_M)
#include <cmsis_core.h>
#endif

#include "io/io.h"
#include "target.h"

#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/mcuboot_status.h"
#include "flash_map_backend/flash_map_backend.h"
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST
#include <bootutil/boot_request.h>

/** Number of image slots. */
#define BOOT_REQUEST_NUM_SLOTS 2
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST */

#if defined(CONFIG_MCUBOOT_UUID_VID) || defined(CONFIG_MCUBOOT_UUID_CID)
#include "bootutil/mcuboot_uuid.h"
#endif /* CONFIG_MCUBOOT_UUID_VID || CONFIG_MCUBOOT_UUID_CID */

/* Check if Espressif target is supported */
#ifdef CONFIG_SOC_FAMILY_ESPRESSIF_ESP32

#include <bootloader_init.h>
#include <esp_image_loader.h>

#define IMAGE_INDEX_0   0
#define IMAGE_INDEX_1   1

#define PRIMARY_SLOT    0
#define SECONDARY_SLOT  1

#define IMAGE0_PRIMARY_START_ADDRESS \
          DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_0), reg, 0)
#define IMAGE0_PRIMARY_SIZE \
          DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_0), reg, 1)

#define IMAGE1_PRIMARY_START_ADDRESS \
          DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_1), reg, 0)
#define IMAGE1_PRIMARY_SIZE \
          DT_PROP_BY_IDX(DT_NODE_BY_FIXED_PARTITION_LABEL(image_1), reg, 1)

#endif /* CONFIG_SOC_FAMILY_ESPRESSIF_ESP32 */

#ifdef CONFIG_FW_INFO
#include <fw_info.h>
#endif

#ifdef CONFIG_MCUBOOT_SERIAL
#include "boot_serial/boot_serial.h"
#include "serial_adapter/serial_adapter.h"

const struct boot_uart_funcs boot_funcs = {
    .read = console_read,
    .write = console_write
};
#endif

#if defined(CONFIG_BOOT_USB_DFU_WAIT) || defined(CONFIG_BOOT_USB_DFU_GPIO)
#include <zephyr/usb/class/usb_dfu.h>
#endif

#if CONFIG_MCUBOOT_CLEANUP_ARM_CORE
#include <arm_cleanup.h>
#endif

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && defined(PM_CPUNET_B0N_ADDRESS) && defined(CONFIG_PCD_APP)
#include <dfu/pcd.h>
#endif

/* CONFIG_LOG_MINIMAL is the legacy Kconfig property,
 * replaced by CONFIG_LOG_MODE_MINIMAL.
 */
#if (defined(CONFIG_LOG_MODE_MINIMAL) || defined(CONFIG_LOG_MINIMAL))
#define ZEPHYR_LOG_MODE_MINIMAL 1
#endif

/* CONFIG_LOG_IMMEDIATE is the legacy Kconfig property,
 * replaced by CONFIG_LOG_MODE_IMMEDIATE.
 */
#if (defined(CONFIG_LOG_MODE_IMMEDIATE) || defined(CONFIG_LOG_IMMEDIATE))
#define ZEPHYR_LOG_MODE_IMMEDIATE 1
#endif

#if defined(CONFIG_LOG) && !defined(ZEPHYR_LOG_MODE_IMMEDIATE) && \
    !defined(ZEPHYR_LOG_MODE_MINIMAL)
#ifdef CONFIG_LOG_PROCESS_THREAD
#warning "The log internal thread for log processing can't transfer the log"\
         "well for MCUBoot."
#else
#include <zephyr/logging/log_ctrl.h>

#define BOOT_LOG_PROCESSING_INTERVAL K_MSEC(30) /* [ms] */

/* log are processing in custom routine */
K_THREAD_STACK_DEFINE(boot_log_stack, CONFIG_MCUBOOT_LOG_THREAD_STACK_SIZE);
struct k_thread boot_log_thread;
volatile bool boot_log_stop = false;
K_SEM_DEFINE(boot_log_sem, 1, 1);

/* log processing need to be initalized by the application */
#define ZEPHYR_BOOT_LOG_START() zephyr_boot_log_start()
#define ZEPHYR_BOOT_LOG_STOP() zephyr_boot_log_stop()
#endif /* CONFIG_LOG_PROCESS_THREAD */
#else
/* synchronous log mode doesn't need to be initalized by the application */
#define ZEPHYR_BOOT_LOG_START() do { } while (false)
#define ZEPHYR_BOOT_LOG_STOP() do { } while (false)
#endif /* defined(CONFIG_LOG) && !defined(CONFIG_LOG_MODE_IMMEDIATE) && \
        * !defined(CONFIG_LOG_MODE_MINIMAL)
	*/

#include "nrf_protect.h"

#if CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL
#include <nrf_cleanup.h>
#endif

#include <zephyr/linker/linker-defs.h>
#define CLEANUP_RAM_GAP_START ((int)__ramfunc_region_start)
#define CLEANUP_RAM_GAP_SIZE ((int) (__ramfunc_end - __ramfunc_region_start))

#if defined(CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX)
/* Disabling R_X has to be done while running from RAM for obvious reasons.
 * Moreover as a last step before jumping to application it must work even after
 * RAM has been cleared, therefore these operations are performed while executing from RAM.
 * RAM cleanup ommits portion of the memory where code lives.
 */
#include <hal/nrf_rramc.h>

#define RRAMC_REGION_RWX_LSB 0
#define RRAMC_REGION_RWX_WIDTH 3
#define RRAMC_REGION_TO_LOCK_ADDR NRF_RRAMC->REGION[4].CONFIG
#define RRAMC_REGION_TO_LOCK_ADDR_H (((uint32_t)(&(RRAMC_REGION_TO_LOCK_ADDR))) >> 16)
#define RRAMC_REGION_TO_LOCK_ADDR_L (((uint32_t)(&(RRAMC_REGION_TO_LOCK_ADDR))) & 0x0000fffful)
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

BOOT_LOG_MODULE_REGISTER(mcuboot);

void os_heap_init(void);

#if defined(CONFIG_ARM)

#ifdef CONFIG_SW_VECTOR_RELAY
extern void *_vector_table_pointer;
#endif

struct arm_vector_table {
#ifdef CONFIG_CPU_CORTEX_M
    uint32_t msp;
    uint32_t reset;
#else
    uint32_t reset;
    uint32_t undef_instruction;
    uint32_t svc;
    uint32_t abort_prefetch;
    uint32_t abort_data;
    uint32_t reserved;
    uint32_t irq;
    uint32_t fiq;
#endif
};

static void __ramfunc jump_in(uint32_t reset)
{
        __asm__ volatile (
                /* reset -> r0 */
                "   mov  r0, %0\n"
#ifdef CONFIG_MCUBOOT_CLEANUP_RAM
                /* Base to write -> r1 */
                "   mov  r1, %1\n"
                /* Size to write -> r2 */
                "   mov  r2, %2\n"
                /* Value to write -> r3 */
                "   movw r3, %5\n"
                /* gap start */
                "   mov  r4, %3\n"
                /* gap size */
                "   mov  r5, %4\n"
                "clear:\n"
                "   subs r6, r4, r1\n"
                "   cbnz r6, skip_gap\n"
                "   add  r1, r5\n"
                "skip_gap:\n"
                "   str  r3, [r1]\n"
                "   add  r1, r1, #1\n"
                "   sub  r2, r2, #1\n"
                "   cbz  r2, clear_end\n"
                "   b    clear\n"
                "clear_end:\n"
                "   dsb\n"
#ifdef CONFIG_MCUBOOT_INFINITE_LOOP_AFTER_RAM_CLEANUP
                "   b       clear_end\n"
#endif /* CONFIG_MCUBOOT_INFINITE_LOOP_AFTER_RAM_CLEANUP */
#endif /* CONFIG_MCUBOOT_CLEANUP_RAM */

#ifdef CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
                ".thumb_func\n"
                "region_disable_rwx:\n"
                "   movw r1, %6\n"
                "   movt r1, %7\n"
                "   ldr  r2, [r1]\n"
                /* Size of the region should be set at this point
                 * by NSIB's DISABLE_NEXT_W.
                 * If not, set it according partition size.
                 */
                "   ands r4, r2, %12\n"
                "   cbnz r4, clear_rwx\n"
                "   movt r2, %8\n"
                "clear_rwx:\n"
                "   bfc  r2, %9, %10\n"
                /* Disallow further modifications */
                "   orr  r2, %11\n"
                "   str  r2, [r1]\n"
                "   dsb\n"
                /* Next assembly line is important for current function */

 #endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */

                /* Jump to reset vector of an app */
                "   bx   r0\n"
                :
                : "r" (reset),
                  "r" (CONFIG_SRAM_BASE_ADDRESS),
                  "i" (CONFIG_SRAM_SIZE * 1024),
                  "r" (CLEANUP_RAM_GAP_START),
                  "r" (CLEANUP_RAM_GAP_SIZE),
                  "i" (0)
#ifdef CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX
                  , "i" (RRAMC_REGION_TO_LOCK_ADDR_L),
                  "i" (RRAMC_REGION_TO_LOCK_ADDR_H),
                  "i" (CONFIG_PM_PARTITION_SIZE_B0_IMAGE / 1024),
                  "i" (RRAMC_REGION_RWX_LSB),
                  "i" (RRAMC_REGION_RWX_WIDTH),
                  "i" (RRAMC_REGION_CONFIG_LOCK_Msk),
                  "i" (RRAMC_REGION_CONFIG_SIZE_Msk)
#endif /* CONFIG_NCS_MCUBOOT_DISABLE_SELF_RWX */
                : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "memory"
        );
}

static void do_boot(struct boot_rsp *rsp)
{
    /* vt is static as it shall not land on the stack,
     * as this procedure modifies stack pointer before usage of *vt
     */
    static struct arm_vector_table *vt;

    /* The beginning of the image is the ARM vector table, containing
     * the initial stack pointer address and the reset vector
     * consecutively. Manually set the stack pointer and jump into the
     * reset vector
     */
#ifdef MCUBOOT_RAM_LOAD
    /* Get ram address for image */
    vt = (struct arm_vector_table *)(rsp->br_hdr->ih_load_addr + rsp->br_hdr->ih_hdr_size);
#else
    uintptr_t flash_base;
    int rc;

    /* Jump to flash image */
    rc = flash_device_base(rsp->br_flash_dev_id, &flash_base);
    assert(rc == 0);

    vt = (struct arm_vector_table *)(flash_base +
                                     rsp->br_image_off +
                                     rsp->br_hdr->ih_hdr_size);
#endif

    if (IS_ENABLED(CONFIG_SYSTEM_TIMER_HAS_DISABLE_SUPPORT)) {
        sys_clock_disable();
    }

#ifdef CONFIG_USB_DEVICE_STACK
    /* Disable the USB to prevent it from firing interrupts */
    usb_disable();
#endif

#if defined(CONFIG_FW_INFO) && !defined(CONFIG_EXT_API_PROVIDE_EXT_API_UNUSED)
    uintptr_t fw_start_addr;

    rc = flash_device_base(rsp->br_flash_dev_id, &fw_start_addr);
    assert(rc == 0);

    fw_start_addr += rsp->br_image_off + rsp->br_hdr->ih_hdr_size;

    const struct fw_info *firmware_info = fw_info_find(fw_start_addr);
    bool provided = fw_info_ext_api_provide(firmware_info, true);

#ifdef PM_S0_ADDRESS
    /* Only fail if the immutable bootloader is present. */
    if (!provided) {
	if (firmware_info == NULL) {
            BOOT_LOG_WRN("Unable to find firmware info structure in %p", vt);
	}
        BOOT_LOG_ERR("Failed to provide EXT_APIs to %p", vt);
    }
#endif
#endif
#if CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL
    nrf_cleanup_peripheral();
#endif
#if CONFIG_MCUBOOT_CLEANUP_ARM_CORE
    cleanup_arm_interrupts(); /* Disable and acknowledge all interrupts */

#if defined(CONFIG_BOOT_DISABLE_CACHES)
    /* Flush and disable instruction/data caches before chain-loading the application */
    (void)sys_cache_instr_flush_all();
    (void)sys_cache_data_flush_all();
    sys_cache_instr_disable();
    sys_cache_data_disable();
#endif

#if CONFIG_CPU_HAS_ARM_MPU || CONFIG_CPU_HAS_NXP_SYSMPU
    z_arm_clear_arm_mpu_config();
#endif

#if defined(CONFIG_BUILTIN_STACK_GUARD) && \
    defined(CONFIG_CPU_CORTEX_M_HAS_SPLIM)
    /* Reset limit registers to avoid inflicting stack overflow on image
     * being booted.
     */
    __set_PSPLIM(0);
    __set_MSPLIM(0);
#endif

#else
    irq_lock();
#endif /* CONFIG_MCUBOOT_CLEANUP_ARM_CORE */

#ifdef CONFIG_BOOT_INTR_VEC_RELOC
#if defined(CONFIG_SW_VECTOR_RELAY)
    _vector_table_pointer = vt;
#ifdef CONFIG_CPU_CORTEX_M_HAS_VTOR
    SCB->VTOR = (uint32_t)__vector_relay_table;
#endif
#elif defined(CONFIG_CPU_CORTEX_M_HAS_VTOR)
    SCB->VTOR = (uint32_t)vt;
#endif /* CONFIG_SW_VECTOR_RELAY */
#else /* CONFIG_BOOT_INTR_VEC_RELOC */
#if defined(CONFIG_CPU_CORTEX_M_HAS_VTOR) && defined(CONFIG_SW_VECTOR_RELAY)
    _vector_table_pointer = _vector_start;
    SCB->VTOR = (uint32_t)__vector_relay_table;
#endif
#endif /* CONFIG_BOOT_INTR_VEC_RELOC */

#ifdef CONFIG_CPU_CORTEX_M
    __set_MSP(vt->msp);
#endif

#if CONFIG_MCUBOOT_CLEANUP_ARM_CORE
#ifdef CONFIG_CPU_CORTEX_M
    __set_CONTROL(0x00); /* application will configures core on its own */
    __ISB();
#else
    /* Set mode to supervisor and A, I and F bit as described in the
     * Cortex R5 TRM */
    __asm__ volatile(
        "   mrs r0, CPSR\n"
        /* change mode bits to supervisor */
        "   bic r0, #0x1f\n"
        "   orr r0, #0x13\n"
        /* set the A, I and F bit */
        "   mov r1, #0b111\n"
        "   lsl r1, #0x6\n"
        "   orr r0, r1\n"

        "   msr CPSR, r0\n"
        ::: "r0", "r1");
#endif /* CONFIG_CPU_CORTEX_M */

#endif
    jump_in(vt->reset);
}

#elif defined(CONFIG_XTENSA) || defined(CONFIG_RISCV)

#ifndef CONFIG_SOC_FAMILY_ESPRESSIF_ESP32

#define SRAM_BASE_ADDRESS	0xBE030000

static void copy_img_to_SRAM(int slot, unsigned int hdr_offset)
{
    const struct flash_area *fap;
    int area_id;
    int rc;
    unsigned char *dst = (unsigned char *)(SRAM_BASE_ADDRESS + hdr_offset);

    BOOT_LOG_INF("Copying image to SRAM");

    area_id = flash_area_id_from_image_slot(slot);
    rc = flash_area_open(area_id, &fap);
    if (rc != 0) {
        BOOT_LOG_ERR("flash_area_open failed with %d\n", rc);
        goto done;
    }

    rc = flash_area_read(fap, hdr_offset, dst, fap->fa_size - hdr_offset);
    if (rc != 0) {
        BOOT_LOG_ERR("flash_area_read failed with %d\n", rc);
        goto done;
    }

done:
    flash_area_close(fap);
}
#endif /* !CONFIG_SOC_FAMILY_ESPRESSIF_ESP32 */

/* Entry point (.ResetVector) is at the very beginning of the image.
 * Simply copy the image to a suitable location and jump there.
 */
static void do_boot(struct boot_rsp *rsp)
{
#ifndef CONFIG_SOC_FAMILY_ESPRESSIF_ESP32
    void *start;
#endif /* CONFIG_SOC_FAMILY_ESPRESSIF_ESP32 */

    BOOT_LOG_INF("br_image_off = 0x%x\n", rsp->br_image_off);
    BOOT_LOG_INF("ih_hdr_size = 0x%x\n", rsp->br_hdr->ih_hdr_size);

#ifdef CONFIG_SOC_FAMILY_ESPRESSIF_ESP32
    int slot = (rsp->br_image_off == IMAGE0_PRIMARY_START_ADDRESS) ?
                PRIMARY_SLOT : SECONDARY_SLOT;
    /* Load memory segments and start from entry point */
    start_cpu0_image(IMAGE_INDEX_0, slot, rsp->br_hdr->ih_hdr_size);
#else
    /* Copy from the flash to HP SRAM */
    copy_img_to_SRAM(0, rsp->br_hdr->ih_hdr_size);

    /* Jump to entry point */
    start = (void *)(SRAM_BASE_ADDRESS + rsp->br_hdr->ih_hdr_size);
    ((void (*)(void))start)();
#endif /* CONFIG_SOC_FAMILY_ESPRESSIF_ESP32 */
}

#elif defined(CONFIG_ARC)

/*
 * ARC vector table has a pointer to the reset function as the first entry
 * in the vector table. Assume the vector table is at the start of the image,
 * and jump to reset
 */
static void do_boot(struct boot_rsp *rsp)
{
    struct arc_vector_table {
        void (*reset)(void); /* Reset vector */
    } *vt;

#if defined(MCUBOOT_RAM_LOAD)
    vt = (struct arc_vector_table *)(rsp->br_hdr->ih_load_addr + rsp->br_hdr->ih_hdr_size);
#else
    uintptr_t flash_base;
    int rc;

    rc = flash_device_base(rsp->br_flash_dev_id, &flash_base);
    assert(rc == 0);

    vt = (struct arc_vector_table *)(flash_base + rsp->br_image_off +
                     rsp->br_hdr->ih_hdr_size);
#endif

    /* Lock interrupts and dive into the entry point */
    irq_lock();
    vt->reset();
}

#else
/* Default: Assume entry point is at the very beginning of the image. Simply
 * lock interrupts and jump there. This is the right thing to do for X86 and
 * possibly other platforms.
 */
static void do_boot(struct boot_rsp *rsp)
{
    void *start;

#if defined(MCUBOOT_RAM_LOAD)
    start = (void *)(rsp->br_hdr->ih_load_addr + rsp->br_hdr->ih_hdr_size);
#else
    uintptr_t flash_base;
    int rc;

    rc = flash_device_base(rsp->br_flash_dev_id, &flash_base);
    assert(rc == 0);

    start = (void *)(flash_base + rsp->br_image_off +
                     rsp->br_hdr->ih_hdr_size);
#endif

    /* Lock interrupts and dive into the entry point */
    irq_lock();
    ((void (*)(void))start)();
}
#endif

#if defined(CONFIG_LOG) && !defined(CONFIG_LOG_MODE_IMMEDIATE) && \
    !defined(CONFIG_LOG_PROCESS_THREAD) && !defined(CONFIG_LOG_MODE_MINIMAL)
/* The log internal thread for log processing can't transfer log well as has too
 * low priority.
 * Dedicated thread for log processing below uses highest application
 * priority. This allows to transmit all logs without adding k_sleep/k_yield
 * anywhere else int the code.
 */

/* most simple log processing theread */
void boot_log_thread_func(void *dummy1, void *dummy2, void *dummy3)
{
    (void)dummy1;
    (void)dummy2;
    (void)dummy3;

    log_init();

    while (1) {
        if (log_process() == false) {
            if (boot_log_stop) {
                break;
            }
            k_sleep(BOOT_LOG_PROCESSING_INTERVAL);
        }
    }

    k_sem_give(&boot_log_sem);
}

void zephyr_boot_log_start(void)
{
    /* start logging thread */
    k_thread_create(&boot_log_thread, boot_log_stack,
                    K_THREAD_STACK_SIZEOF(boot_log_stack),
                    boot_log_thread_func, NULL, NULL, NULL,
                    K_HIGHEST_APPLICATION_THREAD_PRIO, 0,
                    BOOT_LOG_PROCESSING_INTERVAL);

    k_thread_name_set(&boot_log_thread, "logging");
}

void zephyr_boot_log_stop(void)
{
    boot_log_stop = true;

    /* wait until log procesing thread expired
     * This can be reworked using a thread_join() API once a such will be
     * available in zephyr.
     * see https://github.com/zephyrproject-rtos/zephyr/issues/21500
     */
    (void)k_sem_take(&boot_log_sem, K_FOREVER);
}
#endif /* defined(CONFIG_LOG) && !defined(CONFIG_LOG_MODE_IMMEDIATE) && \
        * !defined(CONFIG_LOG_PROCESS_THREAD) && !defined(CONFIG_LOG_MODE_MINIMAL)
        */

#if defined(CONFIG_BOOT_SERIAL_ENTRANCE_GPIO) || defined(CONFIG_BOOT_SERIAL_PIN_RESET) \
    || defined(CONFIG_BOOT_SERIAL_BOOT_MODE) || defined(CONFIG_BOOT_SERIAL_NO_APPLICATION)
static void boot_serial_enter()
{
    int rc;

#ifdef CONFIG_MCUBOOT_INDICATION_LED
    io_led_set(1);
#endif

    mcuboot_status_change(MCUBOOT_STATUS_SERIAL_DFU_ENTERED);

    BOOT_LOG_INF("Enter the serial recovery mode");
    rc = boot_console_init();
    __ASSERT(rc == 0, "Error initializing boot console.\n");
    boot_serial_start(&boot_funcs);
    __ASSERT(0, "Bootloader serial process was terminated unexpectedly.\n");
}
#endif

static int boot_prevalidate(void)
{
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST
    uint8_t image_index;
    enum boot_slot slot;
    uint32_t area_id;
    const struct flash_area *fap;
    int rc = boot_request_init();

    if (rc != 0) {
        return rc;
    }

    for (image_index = 0; image_index < BOOT_IMAGE_NUMBER; ++image_index) {
        for (slot = BOOT_SLOT_PRIMARY; slot < BOOT_SLOT_COUNT; slot++) {
            if (boot_request_check_confirmed_slot(image_index, slot)) {
                BOOT_LOG_DBG("Confirm image: %d slot: %d due to bootloader request.",
                    image_index, slot);

                area_id = flash_area_id_from_multi_image_slot(image_index, slot);
                rc = flash_area_open(area_id, &fap);
                if (rc == 0) {
                    rc = boot_set_next(fap, true, true);
                }
            }
        }
    }
#endif
    return 0;
}

int main(void)
{
    struct boot_rsp rsp;
    int rc;
#if defined(CONFIG_BOOT_USB_DFU_GPIO) || defined(CONFIG_BOOT_USB_DFU_WAIT)
    bool usb_dfu_requested = false;
#endif
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    MCUBOOT_WATCHDOG_SETUP();
    MCUBOOT_WATCHDOG_FEED();

#if !defined(MCUBOOT_DIRECT_XIP)
    BOOT_LOG_INF("Starting bootloader");
#else
    BOOT_LOG_INF("Starting Direct-XIP bootloader");
#endif

#ifdef CONFIG_MCUBOOT_INDICATION_LED
    /* LED init */
    io_led_init();
#endif

    os_heap_init();

    ZEPHYR_BOOT_LOG_START();

    (void)rc;

    mcuboot_status_change(MCUBOOT_STATUS_STARTUP);

#if defined(CONFIG_MCUBOOT_UUID_VID) || defined(CONFIG_MCUBOOT_UUID_CID)
    FIH_CALL(boot_uuid_init, fih_rc);
    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        BOOT_LOG_ERR("Unable to initialize UUID module: %d", fih_rc);
        FIH_PANIC;
    }
#endif /* CONFIG_MCUBOOT_UUID_VID || CONFIG_MCUBOOT_UUID_CID */

    rc = boot_prevalidate();
    if (rc) {
        BOOT_LOG_ERR("Failed to prevalidate the state: %d", rc);
    }

#ifdef CONFIG_BOOT_SERIAL_ENTRANCE_GPIO
    BOOT_LOG_DBG("Checking GPIO for serial recovery");
    if (io_detect_pin() &&
            !io_boot_skip_serial_recovery()) {
        boot_serial_enter();
    }
#endif

#ifdef CONFIG_BOOT_SERIAL_PIN_RESET
    BOOT_LOG_DBG("Checking RESET pin for serial recovery");
    if (io_detect_pin_reset()) {
        boot_serial_enter();
    }
#endif

#ifdef CONFIG_NRF_BOOT_SERIAL_BOOT_REQ
    if (boot_request_detect_recovery()) {
        BOOT_LOG_DBG("Staying in serial recovery");
        boot_serial_enter();
    }
#endif

#if defined(CONFIG_BOOT_USB_DFU_GPIO)
    BOOT_LOG_DBG("Checking GPIO for USB DFU request");
    if (io_detect_pin()) {
        BOOT_LOG_DBG("Entering USB DFU");

        usb_dfu_requested = true;

#ifdef CONFIG_MCUBOOT_INDICATION_LED
        io_led_set(1);
#endif

        mcuboot_status_change(MCUBOOT_STATUS_USB_DFU_ENTERED);
    }
#elif defined(CONFIG_BOOT_USB_DFU_WAIT)
    usb_dfu_requested = true;
#endif

#if defined(CONFIG_BOOT_USB_DFU_GPIO) || defined(CONFIG_BOOT_USB_DFU_WAIT)
    if (usb_dfu_requested) {
        rc = usb_enable(NULL);
        if (rc) {
            BOOT_LOG_ERR("Cannot enable USB: %d", rc);
        } else {
            BOOT_LOG_INF("Waiting for USB DFU");

#if defined(CONFIG_BOOT_USB_DFU_WAIT)
            BOOT_LOG_DBG("Waiting for USB DFU for %dms", CONFIG_BOOT_USB_DFU_WAIT_DELAY_MS);
            mcuboot_status_change(MCUBOOT_STATUS_USB_DFU_WAITING);
            wait_for_usb_dfu(K_MSEC(CONFIG_BOOT_USB_DFU_WAIT_DELAY_MS));
            BOOT_LOG_INF("USB DFU wait time elapsed");
            mcuboot_status_change(MCUBOOT_STATUS_USB_DFU_TIMED_OUT);
#else
            wait_for_usb_dfu(K_FOREVER);
            BOOT_LOG_INF("USB DFU wait terminated");
#endif
        }
    }
#endif

#ifdef CONFIG_BOOT_SERIAL_WAIT_FOR_DFU
    /* Initialize the boot console, so we can already fill up our buffers while
     * waiting for the boot image check to finish. This image check, can take
     * some time, so it's better to reuse thistime to already receive the
     * initial mcumgr command(s) into our buffers
     */
    rc = boot_console_init();
    int timeout_in_ms = CONFIG_BOOT_SERIAL_WAIT_FOR_DFU_TIMEOUT;
    uint32_t start = k_uptime_get_32();

#ifdef CONFIG_MCUBOOT_INDICATION_LED
    io_led_set(1);
#endif
#endif

    BOOT_HOOK_GO_CALL_FIH(boot_go_hook, FIH_BOOT_HOOK_REGULAR, fih_rc, &rsp);
    if (FIH_EQ(fih_rc, FIH_BOOT_HOOK_REGULAR)) {
        FIH_CALL(boot_go, fih_rc, &rsp);
    }
    BOOT_LOG_DBG("Left boot_go with success == %d", FIH_EQ(fih_rc, FIH_SUCCESS) ? 1 : 0);

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST
    (void)boot_request_clear();
#endif

#ifdef CONFIG_BOOT_SERIAL_BOOT_MODE
    if (io_detect_boot_mode()) {
        /* Boot mode to stay in bootloader, clear status and enter serial
         * recovery mode
         */
        BOOT_LOG_DBG("Staying in serial recovery");
        boot_serial_enter();
    }
#endif

#ifdef CONFIG_BOOT_SERIAL_WAIT_FOR_DFU
    timeout_in_ms -= (k_uptime_get_32() - start);
    if( timeout_in_ms <= 0 ) {
        /* at least one check if time was expired */
        timeout_in_ms = 1;
    }
    boot_serial_check_start(&boot_funcs,timeout_in_ms);

#ifdef CONFIG_MCUBOOT_INDICATION_LED
    io_led_set(0);
#endif
#endif

    if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
        BOOT_LOG_ERR("Unable to find bootable image");

        mcuboot_status_change(MCUBOOT_STATUS_NO_BOOTABLE_IMAGE_FOUND);

#ifdef CONFIG_BOOT_SERIAL_NO_APPLICATION
        /* No bootable image and configuration set to remain in serial
         * recovery mode
         */
        boot_serial_enter();
#elif defined(CONFIG_BOOT_USB_DFU_NO_APPLICATION)
        rc = usb_enable(NULL);
        if (rc && rc != -EALREADY) {
            BOOT_LOG_ERR("Cannot enable USB");
        } else {
            BOOT_LOG_INF("Waiting for USB DFU");
            wait_for_usb_dfu(K_FOREVER);
        }
#endif

        FIH_PANIC;
    }

#ifdef MCUBOOT_RAM_LOAD
    BOOT_LOG_INF("Bootloader chainload address offset: 0x%x",
                 rsp.br_hdr->ih_load_addr);
#else
    BOOT_LOG_INF("Bootloader chainload address offset: 0x%x",
                 rsp.br_image_off);
#endif

    BOOT_LOG_INF("Image version: v%d.%d.%d", rsp.br_hdr->ih_ver.iv_major,
                                                    rsp.br_hdr->ih_ver.iv_minor,
                                                    rsp.br_hdr->ih_ver.iv_revision);

#if defined(MCUBOOT_DIRECT_XIP)
    BOOT_LOG_INF("Jumping to the image slot");
#else
    BOOT_LOG_INF("Jumping to the first image slot");
#endif

    mcuboot_status_change(MCUBOOT_STATUS_BOOTABLE_IMAGE_FOUND);

    /* From this point MCUboot does not need access to crypto keys.
     * Clean up backend key objects and apply key access policies that
     * will take effect from now through entire boot session and application
     * run.
     */
    nrf_crypto_keys_housekeeping();

#if USE_PARTITION_MANAGER && CONFIG_FPROTECT

    rc = fprotect_area(PROTECT_ADDR, PROTECT_SIZE);

    if (rc != 0) {
        BOOT_LOG_ERR("Protect mcuboot flash failed, cancel startup.");
        while (1)
            ;
    }

#if defined(CONFIG_SOC_NRF5340_CPUAPP) && defined(PM_CPUNET_B0N_ADDRESS) && defined(CONFIG_PCD_APP)
#if defined(PM_TFM_SECURE_ADDRESS)
    pcd_lock_ram(false);
#else
    pcd_lock_ram(true);
#endif
#endif
#endif /* USE_PARTITION_MANAGER && CONFIG_FPROTECT */

    ZEPHYR_BOOT_LOG_STOP();

    do_boot(&rsp);

    mcuboot_status_change(MCUBOOT_STATUS_BOOT_FAILED);

    BOOT_LOG_ERR("Never should get here");
    while (1)
        ;
}
