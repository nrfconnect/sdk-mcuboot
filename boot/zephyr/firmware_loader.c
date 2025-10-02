/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2020 Arm Limited
 * Copyright (c) 2020-2025 Nordic Semiconductor ASA
 */

#include <assert.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/fault_injection_hardening.h"

#include "io/io.h"
#include "mcuboot_config/mcuboot_config.h"
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST
#include <bootutil/boot_request.h>
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST */

BOOT_LOG_MODULE_DECLARE(mcuboot);

/* Variables passed outside of unit via poiters. */
static const struct flash_area *_fa_p;
static struct image_header _hdr = { 0 };

#if DT_NODE_EXISTS(DT_NODELABEL(slot0_partition))
#define SLOT0_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition))
#else
#error "No slot0_partition found in DTS"
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(slot2_partition))
#define SLOT2_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot2_partition))
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(fw_loader_partition))
#define FW_LOADER_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(fw_loader_partition))
#elif DT_NODE_EXISTS(DT_NODELABEL(slot1_partition))
#define FW_LOADER_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition))
#else
#error "No firmware loader partition found in DTS"
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(fw_loader_aux_partition))
#define FW_LOADER_AUX_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(fw_loader_aux_partition))
#elif DT_NODE_EXISTS(DT_NODELABEL(slot3_partition))
#define FW_LOADER_AUX_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot3_partition))
#endif

/**
 * Validate hash of a primary boot image.
 *
 * @param[in]	fa_p	flash area pointer
 * @param[in]	hdr	boot image header pointer
 *
 * @return		FIH_SUCCESS on success, error code otherwise
 */
fih_ret
boot_image_validate(const struct flash_area *fa_p,
                    struct image_header *hdr)
{
    static uint8_t tmpbuf[BOOT_TMPBUF_SZ];
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("boot_image_validate: encrypted == %d", (int)IS_ENCRYPTED(hdr));

    /* NOTE: The first argument to boot_image_validate, for enc_state pointer,
     * is allowed to be NULL only because the single image loader compiles
     * with BOOT_IMAGE_NUMBER == 1, which excludes the code that uses
     * the pointer from compilation.
     */
    /* Validate hash */
    if (IS_ENCRYPTED(hdr))
    {
        /* Clear the encrypted flag we didn't supply a key
         * This flag could be set if there was a decryption in place
         * was performed. We will try to validate the image, and if still
         * encrypted the validation will fail, and go in panic mode
         */
        hdr->ih_flags &= ~(ENCRYPTIONFLAGS);
    }
    FIH_CALL(bootutil_img_validate, fih_rc, NULL, hdr, fa_p, tmpbuf,
             BOOT_TMPBUF_SZ, NULL, 0, NULL);

    FIH_RET(fih_rc);
}

#if defined(MCUBOOT_VALIDATE_PRIMARY_SLOT_ONCE)
inline static fih_ret
boot_image_validate_once(const struct flash_area *fa_p,
                    struct image_header *hdr)
{
    static struct boot_swap_state state;
    int rc;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("boot_image_validate_once: flash area %p", fa_p);

    memset(&state, 0, sizeof(struct boot_swap_state));
    rc = boot_read_swap_state(fa_p, &state);
    if (rc != 0)
        FIH_RET(FIH_FAILURE);
    if (state.magic != BOOT_MAGIC_GOOD
            || state.image_ok != BOOT_FLAG_SET) {
        /* At least validate the image once */
        FIH_CALL(boot_image_validate, fih_rc, fa_p, hdr);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            FIH_RET(FIH_FAILURE);
        }
        if (state.magic != BOOT_MAGIC_GOOD) {
            rc = boot_write_magic(fa_p);
            if (rc != 0)
                FIH_RET(FIH_FAILURE);
        }
        rc = boot_write_image_ok(fa_p);
        if (rc != 0)
            FIH_RET(FIH_FAILURE);
    }
    FIH_RET(FIH_SUCCESS);
}
#endif

/**
 * Validates that an image in a partition is OK to boot.
 *
 * @param[in]	id	Fixed partition ID to check
 * @param[out]	rsp	Parameters for booting image, on success
 *
 * @return		FIH_SUCCESS on success; non-zero on failure.
 */
static fih_ret validate_image_id(int id, struct boot_rsp *rsp)
{
    int rc = -1;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("validate_image_id: id %d", id);

    rc = flash_area_open(id, &_fa_p);
    assert(rc == 0);

    rc = boot_image_load_header(_fa_p, &_hdr);
    if (rc != 0) {
        goto other;
    }

    switch (id) {
    case SLOT0_PARTITION_ID:
#ifdef SLOT2_PARTITION_ID
    case SLOT2_PARTITION_ID:
#endif /* SLOT2_PARTITION_ID */
#ifdef MCUBOOT_VALIDATE_PRIMARY_SLOT
        FIH_CALL(boot_image_validate, fih_rc, _fa_p, &_hdr);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            goto other;
        }
#elif defined(MCUBOOT_VALIDATE_PRIMARY_SLOT_ONCE)
        FIH_CALL(boot_image_validate_once, fih_rc, _fa_p, &_hdr);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            goto other;
        }
        break;
#else
        fih_rc = FIH_SUCCESS;
        goto other;
#endif  /* !MCUBOOT_VALIDATE_PRIMARY_SLOT */
    default:
        FIH_CALL(boot_image_validate, fih_rc, _fa_p, &_hdr);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            goto other;
        }
        break;
    }

    BOOT_LOG_INF("validate_image_id: id %d is valid.", id);
    rsp->br_flash_dev_id = flash_area_get_device_id(_fa_p);
    rsp->br_image_off = flash_area_get_off(_fa_p);
    rsp->br_hdr = &_hdr;

other:
    flash_area_close(_fa_p);

    FIH_RET(fih_rc);
}

/**
 * Gather information on image and prepare for booting. Will boot from main
 * image if none of the enabled entrance modes for the firmware loader are set,
 * otherwise will boot the firmware loader. Note: firmware loader must be a
 * valid signed image with the same signing key as the application image.
 *
 * @param[out]	rsp	Parameters for booting image, on success
 *
 * @return		FIH_SUCCESS on success; non-zero on failure.
 */
fih_ret
boot_go(struct boot_rsp *rsp)
{
    bool boot_firmware_loader = false;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("boot_go: firmware loader");

#ifdef CONFIG_BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO
    if (io_detect_pin() && !io_boot_skip_serial_recovery()) {
        BOOT_LOG_INF("Button press detected - enter firmware loader.");
        boot_firmware_loader = true;
    }
#endif

#ifdef CONFIG_BOOT_FIRMWARE_LOADER_PIN_RESET
    if (io_detect_pin_reset()) {
        BOOT_LOG_INF("Pin reset detected - enter firmware loader.");
        boot_firmware_loader = true;
    }
#endif

#ifdef CONFIG_BOOT_FIRMWARE_LOADER_BOOT_MODE
    if (io_detect_boot_mode()) {
        BOOT_LOG_INF("Boot mode detected - enter firmware loader.");
        boot_firmware_loader = true;
    }
#endif

#ifdef CONFIG_NRF_BOOT_FIRMWARE_LOADER_BOOT_REQ
    if (boot_request_detect_firmware_loader()) {
        BOOT_LOG_INF("Boot request detected - enter firmware loader.");
        boot_firmware_loader = true;
    }
#endif

    while (boot_firmware_loader == false) {
        BOOT_LOG_DBG("Validating main image(s)...");
#ifdef SLOT2_PARTITION_ID
        FIH_CALL(validate_image_id, fih_rc, SLOT2_PARTITION_ID, rsp);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
#ifdef CONFIG_BOOT_FIRMWARE_LOADER_NO_APPLICATION
            BOOT_LOG_WRN("Failed to validate slot2_partition. Enter firmware loader.");
            boot_firmware_loader = true;
            break;
#else
            BOOT_LOG_ERR("Failed to validate slot2_partition.");
            FIH_RET(fih_rc);
#endif
        }
#endif /* slot2_partition */

        FIH_CALL(validate_image_id, fih_rc, SLOT0_PARTITION_ID, rsp);
#ifdef CONFIG_BOOT_FIRMWARE_LOADER_NO_APPLICATION
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            BOOT_LOG_WRN("Failed to validate slot0_partition. Enter firmware loader.");
            boot_firmware_loader = true;
            break;
        }
#endif
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
                BOOT_LOG_ERR("Failed to validate slot0_partition.");
        }
        FIH_RET(fih_rc);
    }

    /* Check if firmware loader button is pressed. TODO: check all entrance methods */
    if (boot_firmware_loader == true) {
        BOOT_LOG_DBG("Validating firmware loader image(s)...");
#ifdef FW_LOADER_AUX_PARTITION_ID
        FIH_CALL(validate_image_id, fih_rc, FW_LOADER_AUX_PARTITION_ID, rsp);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            BOOT_LOG_ERR("Failed to validate auxiliary firmware loader image.");
            FIH_RET(fih_rc);
        }
#endif
        FIH_CALL(validate_image_id, fih_rc, FW_LOADER_PARTITION_ID, rsp);
        if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
            BOOT_LOG_ERR("Failed to validate firmware loader image.");
        }
    }

    FIH_RET(fih_rc);
}
