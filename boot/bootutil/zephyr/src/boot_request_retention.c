/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025 Nordic Semiconductor ASA
 */

#include "boot_request_mem.h"
#include <zephyr/retention/retention.h>

static const struct device *bootloader_request_dev =
	DEVICE_DT_GET(DT_CHOSEN(nrf_bootloader_request));

int boot_request_mem_init(void)
{
	if (!device_is_ready(bootloader_request_dev)) {
		return -EIO;
	}
 
	return 0;
}

bool boot_request_mem_wrtie_prepare(void)
{
	return true;
}

int boot_request_mem_read(size_t entry, uint8_t *value)
{
	if ((value == NULL) || (entry >= retention_size(bootloader_request_dev) / sizeof(*value))) {
		return -EINVAL;
	}

	return retention_read(bootloader_request_dev, entry * sizeof(*value), (void *)value,
			      sizeof(*value));
}

int boot_request_mem_write(size_t entry, uint8_t *value)
{
	if ((value == NULL) || (entry >= retention_size(bootloader_request_dev) / sizeof(*value))) {
		return -EINVAL;
	}

	return retention_write(bootloader_request_dev, entry * sizeof(*value), (void *)value,
			       sizeof(*value));
}

int boot_request_mem_selective_erase(size_t *nv_indexes, size_t nv_count)
{
	if ((nv_indexes == NULL) && (nv_count != 0)) {
		return -EINVAL;
	}

	/* Selective erase is not supported if there is no backup area. */
	if (nv_count != 0) {
		return -ENOTSUP;
	}

	return retention_clear(bootloader_request_dev);
}
