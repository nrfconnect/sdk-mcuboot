/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>

#include "boot_serial/boot_serial_cdc_acm_usb_next.h"
#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

USBD_DEVICE_DEFINE(boot_cdc_acm_serial,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   CONFIG_BOOT_SERIAL_CDC_ACM_STRING_VID, CONFIG_BOOT_SERIAL_CDC_ACM_STRING_PID);

USBD_DESC_LANG_DEFINE(boot_cdc_acm_serial_lang);
USBD_DESC_MANUFACTURER_DEFINE(boot_cdc_acm_serial_mfr, CONFIG_BOOT_SERIAL_CDC_ACM_STRING_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(boot_cdc_acm_serial_product, CONFIG_BOOT_SERIAL_CDC_ACM_STRING_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(boot_cdc_acm_serial_sn)));

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

static const uint8_t attributes = IS_ENABLED(CONFIG_CDC_ACM_SERIAL_SELF_POWERED) ?
				  USB_SCD_SELF_POWERED : 0;

USBD_CONFIGURATION_DEFINE(boot_cdc_acm_serial_fs_config,
			  attributes,
			  CONFIG_BOOT_SERIAL_CDC_ACM_MAX_POWER, &fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(boot_cdc_acm_serial_hs_config,
			  attributes,
			  CONFIG_BOOT_SERIAL_CDC_ACM_MAX_POWER, &hs_cfg_desc);


static int register_cdc_acm(struct usbd_context *const uds_ctx,
			    const enum usbd_speed speed)
{
	struct usbd_config_node *cfg_nd;
	int err;

	if (speed == USBD_SPEED_HS) {
		cfg_nd = &boot_cdc_acm_serial_hs_config;
	} else {
		cfg_nd = &boot_cdc_acm_serial_fs_config;
	}

	err = usbd_add_configuration(uds_ctx, speed, cfg_nd);
	if (err) {
		BOOT_LOG_ERR("Failed to add configuration: %d", err);
		return err;
	}

	err = usbd_register_class(uds_ctx, "cdc_acm_0", speed, 1);
	if (err) {
		BOOT_LOG_ERR("Failed to register usb device: %d", err);
		return err;
	}

	return usbd_device_set_code_triple(uds_ctx, speed,
					   USB_BCC_MISCELLANEOUS, 0x02, 0x01);
}

int boot_serial_cdc_acm_usb_next_enable(void)
{
	int rc = 0;

	rc = usbd_add_descriptor(&boot_cdc_acm_serial, &boot_cdc_acm_serial_lang);
	if (rc) {
		BOOT_LOG_ERR("Failed to initialize usb device language descriptor: %d", rc);
		return rc;
	}

	rc = usbd_add_descriptor(&boot_cdc_acm_serial, &boot_cdc_acm_serial_mfr);
	if (rc) {
		BOOT_LOG_ERR("Failed to initialize usb device manufacturer descriptor: %d", rc);
		return rc;
	}

	rc = usbd_add_descriptor(&boot_cdc_acm_serial, &boot_cdc_acm_serial_product);
	if (rc) {
		BOOT_LOG_ERR("Failed to initialize usb device product descriptor: %d", rc);
		return rc;
	}

	IF_ENABLED(CONFIG_HWINFO, (
		rc = usbd_add_descriptor(&boot_cdc_acm_serial, &boot_cdc_acm_serial_sn);
	))
	if (rc) {
		BOOT_LOG_ERR("Failed to initialize usb device SN descriptor: %d", rc);
		return rc;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&boot_cdc_acm_serial) == USBD_SPEED_HS) {
		rc = register_cdc_acm(&boot_cdc_acm_serial, USBD_SPEED_HS);
		if (rc) {
			return rc;
		}
	}

	rc = register_cdc_acm(&boot_cdc_acm_serial, USBD_SPEED_FS);
	if (rc) {
		return rc;
	}

	rc = usbd_init(&boot_cdc_acm_serial);
	if (rc) {
		BOOT_LOG_ERR("Failed to initialize usb device: %d", rc);
		return rc;
	}

	rc = usbd_enable(&boot_cdc_acm_serial);
	if (rc) {
		BOOT_LOG_ERR("Failed to enable usb device: %d", rc);
		return rc;
	}

	return 0;
}

int boot_serial_cdc_acm_usb_next_disable(void)
{
	int rc = 0;

	rc = usbd_disable(&boot_cdc_acm_serial);
	if (rc) {
		BOOT_LOG_ERR("Failed to disable usb device: %d", rc);
		return rc;
	}

	return 0;
}
