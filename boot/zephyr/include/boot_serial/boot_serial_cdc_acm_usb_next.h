/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef H_BOOT_SERIAL_CDC_ACM_USB_NEXT_
#define H_BOOT_SERIAL_CDC_ACM_USB_NEXT_

/**
 * @brief Initialize USB device descriptors, register CDC ACM, and enable USBD.
 *
 * @return 0 on success, negative errno otherwise.
 */
int boot_serial_cdc_acm_usb_next_enable(void);

/**
 * @brief Disable the USB device stack used for serial recovery.
 *
 * @return 0 on success, negative errno otherwise.
 */
int boot_serial_cdc_acm_usb_next_disable(void);

#endif /* H_BOOT_SERIAL_CDC_ACM_USB_NEXT_ */
