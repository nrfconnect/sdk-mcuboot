/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025 Nordic Semiconductor ASA
 */
#include <bootutil/boot_request.h>

#include "bootutil/bootutil_log.h"
#include <zephyr/retention/retention.h>

/** Special value of image number, indicating a request to the bootloader. */
#define BOOT_REQUEST_IMG_BOOTLOADER 0xFF

/** Additional memory used by the retention subsystem (2B - prefix, 4B - CRC).*/
#define BOOT_REQUEST_ENTRY_METADATA_SIZE (2 + 4)

BOOT_LOG_MODULE_REGISTER(bootloader_request);

static const struct device *bootloader_request_dev =
	DEVICE_DT_GET(DT_CHOSEN(nrf_bootloader_request));

enum boot_request_type {
	/** Invalid request. */
	BOOT_REQUEST_INVALID = 0,

	/** Request a change in the bootloader boot mode.
	 *
	 * @details Use @p boot_request_mode as argument.
	 *          @p BOOT_REQUEST_IMG_BOOTLOADER as image number.
	 *
	 * @note Used to trigger recovery through i.e. retention sybsystem.
	 */
	BOOT_REQUEST_BOOT_MODE = 1,

	/** Select the preferred image to be selected during boot or update.
	 *
	 * @details Use @p boot_request_slot_t as argument.
	 *
	 * @note Used in the Direct XIP mode.
	 */
	BOOT_REQUEST_IMG_PREFERENCE = 2,

	/** Request a confirmation of an image.
	 *
	 * @details Use @p boot_request_slot_t as argument.
	 *
	 * @note Used if the code cannot modify the image trailer directly.
	 */
	BOOT_REQUEST_IMG_CONFIRM = 3,
};

/* Entries inside the boot request shared memory. */
enum boot_request_entry {
	BOOT_REQUEST_ENTRY_BOOT_MODE = 0,
	BOOT_REQUEST_ENTRY_IMAGE_0_PREFERENCE = 1,
	BOOT_REQUEST_ENTRY_IMAGE_0_CONFIRM = 2,
	BOOT_REQUEST_ENTRY_IMAGE_1_PREFERENCE = 3,
	BOOT_REQUEST_ENTRY_IMAGE_1_CONFIRM = 4,
	BOOT_REQUEST_ENTRY_MAX = 5,
};

/* Assert that all requests will fit within the retention area. */
BUILD_ASSERT((BOOT_REQUEST_ENTRY_METADATA_SIZE + BOOT_REQUEST_ENTRY_MAX * sizeof(uint8_t)) <
		     DT_REG_SIZE_BY_IDX(DT_CHOSEN(nrf_bootloader_request), 0),
	     "nrf,bootloader-request area is too small for bootloader request struct");

enum boot_request_slot {
	/** Unsupported value. */
	BOOT_REQUEST_SLOT_INVALID = 0,
	/** Primary slot. */
	BOOT_REQUEST_SLOT_PRIMARY = 1,
	/** Secondary slot. */
	BOOT_REQUEST_SLOT_SECONDARY = 2,
};

/** Alias type for the image and number. */
typedef uint8_t boot_request_slot_t;

enum boot_request_mode {
	/** Execute a regular boot logic. */
	BOOT_REQUEST_MODE_REGULAR = 0,
	/** Execute the recovery boot logic. */
	BOOT_REQUEST_MODE_RECOVERY = 1,
	/** Execute the firmware loader logic. */
	BOOT_REQUEST_MODE_FIRMWARE_LOADER = 2,
	/** Unsupported value. */
	BOOT_REQUEST_MODE_INVALID = 0xFF,
};

/** Alias type for the image number. */
typedef uint8_t boot_request_img_t;

/**
 * @brief Find an entry for a given request.
 *
 * @param[in]  type   Type of request.
 * @param[in]  image  Image number. Use @p BOOT_REQUEST_IMG_BOOTLOADER for generic requests.
 * @param[out] entry  Entry to use.
 *
 * @return 0 on success; nonzero on failure.
 */
static int boot_request_entry_find(enum boot_request_type type, boot_request_img_t image,
				   size_t *entry)
{
	if (entry == NULL) {
		return -EINVAL;
	}

	switch (type) {
	case BOOT_REQUEST_BOOT_MODE:
		*entry = BOOT_REQUEST_ENTRY_BOOT_MODE;
		break;
	case BOOT_REQUEST_IMG_PREFERENCE:
		switch (image) {
		case 0:
			*entry = BOOT_REQUEST_ENTRY_IMAGE_0_PREFERENCE;
			break;
		case 1:
			*entry = BOOT_REQUEST_ENTRY_IMAGE_1_PREFERENCE;
			break;
		default:
			return -EINVAL;
		}
		break;
	case BOOT_REQUEST_IMG_CONFIRM:
		switch (image) {
		case 0:
			*entry = BOOT_REQUEST_ENTRY_IMAGE_0_CONFIRM;
			break;
		case 1:
			*entry = BOOT_REQUEST_ENTRY_IMAGE_1_CONFIRM;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int boot_request_init(void)
{
	if (!device_is_ready(bootloader_request_dev)) {
		return -EIO;
	}

	return 0;
}

int boot_request_clear(void)
{
	return retention_clear(bootloader_request_dev);
}

int boot_request_confirm_slot(uint8_t image, uint32_t slot)
{
	uint8_t value = BOOT_REQUEST_SLOT_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_IMG_CONFIRM, image, &req_entry);
	if (ret != 0) {
		return ret;
	}

	switch (slot) {
	case 0:
		value = BOOT_REQUEST_SLOT_PRIMARY;
		break;
	case 1:
		value = BOOT_REQUEST_SLOT_SECONDARY;
		break;
	default:
		return -EINVAL;
	}

	return retention_write(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			       sizeof(value));
}

bool boot_request_check_confirmed_slot(uint8_t image, uint32_t slot)
{
	uint8_t value = BOOT_REQUEST_SLOT_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_IMG_CONFIRM, image, &req_entry);
	if (ret != 0) {
		return false;
	}

	ret = retention_read(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			     sizeof(value));
	if (ret != 0) {
		return false;
	}

	switch (value) {
	case BOOT_REQUEST_SLOT_PRIMARY:
		return (slot == 0);
	case BOOT_REQUEST_SLOT_SECONDARY:
		return (slot == 1);
	default:
		break;
	}

	return false;
}

int boot_request_set_preferred_slot(uint8_t image, uint32_t slot)
{
	uint8_t value = BOOT_REQUEST_SLOT_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_IMG_PREFERENCE, image, &req_entry);
	if (ret != 0) {
		return ret;
	}

	switch (slot) {
	case 0:
		value = BOOT_REQUEST_SLOT_PRIMARY;
		break;
	case 1:
		value = BOOT_REQUEST_SLOT_SECONDARY;
		break;
	default:
		return -EINVAL;
	}

	return retention_write(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			       sizeof(value));
}

#ifdef CONFIG_FIND_NEXT_SLOT_HOOKS
uint32_t boot_request_get_preferred_slot(uint8_t image)
{
	uint8_t value = BOOT_REQUEST_SLOT_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_IMG_PREFERENCE, image, &req_entry);
	if (ret != 0) {
		return BOOT_REQUEST_NO_PREFERRED_SLOT;
	}

	ret = retention_read(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			     sizeof(value));
	if (ret != 0) {
		return BOOT_REQUEST_NO_PREFERRED_SLOT;
	}

	switch (value) {
	case BOOT_REQUEST_SLOT_PRIMARY:
		return 0;
	case BOOT_REQUEST_SLOT_SECONDARY:
		return 1;
	default:
		break;
	}

	return BOOT_REQUEST_NO_PREFERRED_SLOT;
}
#endif /* CONFIG_FIND_NEXT_SLOT_HOOKS */

int boot_request_enter_recovery(void)
{
	uint8_t value = BOOT_REQUEST_MODE_RECOVERY;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_BOOT_MODE, BOOT_REQUEST_IMG_BOOTLOADER,
				      &req_entry);
	if (ret != 0) {
		return ret;
	}

	return retention_write(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			       sizeof(value));
}

#ifdef CONFIG_NRF_BOOT_SERIAL_BOOT_REQ
bool boot_request_detect_recovery(void)
{
	uint8_t value = BOOT_REQUEST_MODE_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_BOOT_MODE, BOOT_REQUEST_IMG_BOOTLOADER,
				      &req_entry);
	if (ret != 0) {
		return false;
	}

	ret = retention_read(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			     sizeof(value));
	if ((ret == 0) && (value == BOOT_REQUEST_MODE_RECOVERY)) {
		return true;
	}

	return false;
}
#endif /* CONFIG_NRF_BOOT_SERIAL_BOOT_REQ */

int boot_request_enter_firmware_loader(void)
{
	uint8_t value = BOOT_REQUEST_MODE_FIRMWARE_LOADER;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_BOOT_MODE, BOOT_REQUEST_IMG_BOOTLOADER,
				      &req_entry);
	if (ret != 0) {
		return ret;
	}

	return retention_write(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			       sizeof(value));
}

#ifdef CONFIG_NRF_BOOT_FIRMWARE_LOADER_BOOT_REQ
bool boot_request_detect_firmware_loader(void)
{
	uint8_t value = BOOT_REQUEST_MODE_INVALID;
	size_t req_entry;
	int ret;

	ret = boot_request_entry_find(BOOT_REQUEST_BOOT_MODE, BOOT_REQUEST_IMG_BOOTLOADER,
				      &req_entry);
	if (ret != 0) {
		return false;
	}

	ret = retention_read(bootloader_request_dev, req_entry * sizeof(value), (void *)&value,
			     sizeof(value));
	if ((ret == 0) && (value == BOOT_REQUEST_MODE_FIRMWARE_LOADER)) {
		return true;
	}

	return false;
}
#endif /* CONFIG_NRF_BOOT_FIRMWARE_LOADER_BOOT_REQ */
