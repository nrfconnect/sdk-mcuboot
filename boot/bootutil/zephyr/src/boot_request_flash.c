/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025 Nordic Semiconductor ASA
 */

#include "boot_request_mem.h"
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "bootutil/bootutil_log.h"

#define MAIN_FLASH_DEV FIXED_PARTITION_NODE_DEVICE(DT_CHOSEN(nrf_bootloader_request))
#define MAIN_OFFSET    FIXED_PARTITION_NODE_OFFSET(DT_CHOSEN(nrf_bootloader_request))
#define MAIN_SIZE      FIXED_PARTITION_NODE_SIZE(DT_CHOSEN(nrf_bootloader_request))

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
#define BACKUP_FLASH_DEV FIXED_PARTITION_NODE_DEVICE(DT_CHOSEN(nrf_bootloader_request_backup))
#define BACKUP_OFFSET    FIXED_PARTITION_NODE_OFFSET(DT_CHOSEN(nrf_bootloader_request_backup))
#define BACKUP_SIZE      FIXED_PARTITION_NODE_SIZE(DT_CHOSEN(nrf_bootloader_request_backup))
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */

#define BOOT_REQUEST_CHECKSUM_SIZE sizeof(uint32_t)
#define BOOT_REQUEST_PREFIX        ((uint16_t)0x0B01)
#define BOOT_REQUEST_PREREFIX_SIZE sizeof(uint16_t)
#define BOOT_REQUEST_ENTRIES_SIZE  (MAIN_SIZE - BOOT_REQUEST_CHECKSUM_SIZE - BOOT_REQUEST_PREREFIX_SIZE)

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
BUILD_ASSERT((MAIN_SIZE == BACKUP_SIZE),
	     "nrf,bootloader-request and nrf,bootloader-request-backup areas must be of equal size");
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
BUILD_ASSERT((BOOT_REQUEST_ENTRIES_SIZE > 0),
	     "nrf,bootloader-request area is too small");

BOOT_LOG_MODULE_DECLARE(bootloader_request);

/** Structure representing the boot request area in NVM. */
typedef struct {
	/* Prefix to identify the boot request area. Must be equal to BOOT_REQUEST_PREFIX. */
	uint16_t prefix;
	/* Modifiable entries in the boot request area. */
	uint8_t entries[BOOT_REQUEST_ENTRIES_SIZE];
	/* Checksum of the boot request entries area. */
	uint32_t checksum;
} boot_request_area_t;

/**
 * @brief Check if the boot request area contains a valid checksum.
 *
 * @param[in] fdev   Flash device.
 * @param[in] offset Offset of the boot request area in flash.
 *
 * @return true if the area is valid; false otherwise.
 */
static bool boot_request_area_valid(const struct device *fdev, size_t offset)
{
	boot_request_area_t area_copy;
	uint32_t exp_checksum;

	if (flash_read(fdev, offset, (uint8_t *)&area_copy, sizeof(boot_request_area_t)) != 0) {
		return false;
	};

	if (area_copy.prefix != BOOT_REQUEST_PREFIX) {
		return false;
	}

	exp_checksum = crc32_ieee_update(0, area_copy.entries, BOOT_REQUEST_ENTRIES_SIZE);
	if (exp_checksum != area_copy.checksum) {
		return false;
	}

	return true;
}

/**
 * @brief Commit the boot request area by updating its checksum.
 *
 * @param[in] fdev   Flash device.
 * @param[in] offset Offset of the boot request area in flash.
 *
 * @retval 0       if area is successfully committed.
 * @retval -EIO    if unable to read from the area.
 * @retval -EROFS  if unable to write to the area.
 */
static int boot_request_commit(const struct device *fdev, size_t offset)
{
	boot_request_area_t area_copy;

	if (flash_read(fdev, offset, (uint8_t *)&area_copy, sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	};

	area_copy.prefix = BOOT_REQUEST_PREFIX;
	area_copy.checksum = crc32_ieee_update(0, area_copy.entries, BOOT_REQUEST_ENTRIES_SIZE);

	if (flash_write(fdev, offset, (uint8_t *)&area_copy, sizeof(boot_request_area_t)) != 0) {
		return -EROFS;
	}

	return 0;
}

/**
 * @brief Clear the boot request area by erasing it and committing the changes.
 *
 * @param[in] fdev   Flash device.
 * @param[in] offset Offset of the boot request area in flash.
 *
 * @retval 0       if area is successfully cleared.
 * @retval -EIO    if unable to erase or read from the area.
 * @retval -EROFS  if unable to commit the changes.
 */
static int boot_request_clear(const struct device *fdev, size_t offset)
{
	if (flash_erase(fdev, offset, sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	}

	return boot_request_commit(fdev, offset);
}

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
/**
 * @brief Check if two boot request areas are equal.
 *
 * @param[in] fdev1   Flash device of the first area.
 * @param[in] offset1 Offset of the first area in flash.
 * @param[in] fdev2   Flash device of the second area.
 * @param[in] offset2 Offset of the second area in flash.
 *
 * @return true if areas are equal; false otherwise.
 */
static bool boot_request_equal(const struct device *fdev1, size_t offset1,
			       const struct device *fdev2, size_t offset2)
{
	boot_request_area_t area1_copy;
	boot_request_area_t area2_copy;

	if (flash_read(fdev1, offset1, (uint8_t *)&area1_copy, sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	}

	if (flash_read(fdev2, offset2, (uint8_t *)&area2_copy, sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	}

	return (memcmp(&area1_copy, &area2_copy, sizeof(boot_request_area_t)) == 0);
}

/**
 * @brief Copy the boot request area from one location to another.
 *
 * @param[in] fdev1   Flash device of the destination area.
 * @param[in] offset1 Offset of the destination area in flash.
 * @param[in] fdev2   Flash device of the source area.
 * @param[in] offset2 Offset of the source area in flash.
 *
 * @retval 0       if area is successfully copied.
 * @retval -ENOENT if source area is not valid.
 * @retval -EIO    if unable to read from source area.
 * @retval -EROFS  if unable to write to destination area.
 */
static int boot_request_copy(const struct device *fdev1, size_t offset1,
			     const struct device *fdev2, size_t offset2)
{
	boot_request_area_t area_copy;

	if (!boot_request_area_valid(fdev2, offset2)) {
		return -ENOENT;
	}

	BOOT_LOG_INF("Copying boot request area (0x%x to 0x%x).", offset2, offset1);

	if (flash_read(fdev2, offset2, (uint8_t *)&area_copy, sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	}

	if (flash_write(fdev1, offset1, (uint8_t *)&area_copy, sizeof(boot_request_area_t)) != 0) {
		return -EROFS;
	}

	return 0;
}
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */

int boot_request_mem_init(void)
{
	if (!device_is_ready(MAIN_FLASH_DEV)) {
		return -EIO;
	}

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
	if (!device_is_ready(BACKUP_FLASH_DEV)) {
		return -EIO;
	}

	if (boot_request_area_valid(MAIN_FLASH_DEV, MAIN_OFFSET)) {
		if (boot_request_area_valid(BACKUP_FLASH_DEV, BACKUP_OFFSET)) {
			if (!boot_request_equal(MAIN_FLASH_DEV, MAIN_OFFSET,
						BACKUP_FLASH_DEV, BACKUP_OFFSET)) {
				BOOT_LOG_INF("New values found. Update backup area.");
				/* Primary is valid, backup is outdated. */
				(void)boot_request_copy(BACKUP_FLASH_DEV, BACKUP_OFFSET,
							MAIN_FLASH_DEV, MAIN_OFFSET);
			} else {
				/* Both are valid and equal, nothing to do. */
			}
		} else {
			BOOT_LOG_INF("Backup area is invalid. Update backup area.");
			/* Backup is invalid, copy primary to backup. */
			(void)boot_request_copy(BACKUP_FLASH_DEV, BACKUP_OFFSET,
						MAIN_FLASH_DEV, MAIN_OFFSET);
		}
	} else {
		if (boot_request_area_valid(BACKUP_FLASH_DEV, BACKUP_OFFSET)) {
			BOOT_LOG_INF("Primary area is invalid. Restore from backup.");
			/* Primary is invalid, restore from backup. */
			return boot_request_copy(MAIN_FLASH_DEV, MAIN_OFFSET,
						 BACKUP_FLASH_DEV, BACKUP_OFFSET);
		} else {
			BOOT_LOG_INF("Both areas are invalid. Clear both areas.");
			/* Both are invalid, clear both. */
			(void)boot_request_clear(BACKUP_FLASH_DEV, BACKUP_OFFSET);
			return boot_request_clear(MAIN_FLASH_DEV, MAIN_OFFSET);
		}
	}
#else /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
	if (!boot_request_area_valid(MAIN_FLASH_DEV, MAIN_OFFSET)) {
		BOOT_LOG_INF("Retention area is invalid. Clear area.");
		return boot_request_clear(MAIN_FLASH_DEV, MAIN_OFFSET);
	}
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
	return 0;
}

bool boot_request_mem_wrtie_prepare(void)
{
	if (!boot_request_area_valid(MAIN_FLASH_DEV, MAIN_OFFSET)) {
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
		if (boot_request_area_valid(BACKUP_FLASH_DEV, BACKUP_OFFSET)) {
			BOOT_LOG_INF("Broken main area. Restore from backup.");
			/* Recover main area from a valid backup. */
			if (boot_request_copy(MAIN_FLASH_DEV, MAIN_OFFSET, BACKUP_FLASH_DEV,
					      BACKUP_OFFSET) != 0) {
				return false;
			}
		} else if (boot_request_clear(MAIN_FLASH_DEV, MAIN_OFFSET) != 0) {
			/* Both areas are not valid - clear the main area. */
			return false;
		}
#else
		/* Main area is not valid and no backup area is available. */
		if (boot_request_clear(MAIN_FLASH_DEV, MAIN_OFFSET) != 0) {
			return false;
		}
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
	}

	return true;
}

int boot_request_mem_read(size_t entry, uint8_t *value)
{
	if ((value == NULL) || (entry >= BOOT_REQUEST_ENTRIES_SIZE)) {
		return -EINVAL;
	}

	if (boot_request_area_valid(MAIN_FLASH_DEV, MAIN_OFFSET)) {
		/* Read from the main area. */
		return flash_read(MAIN_FLASH_DEV, MAIN_OFFSET +
				  offsetof(boot_request_area_t, entries) + entry,
				  value, sizeof(uint8_t));
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
	} else if (boot_request_area_valid(BACKUP_FLASH_DEV,
					   BACKUP_OFFSET)) {
		/* Read from backup area. */
		return flash_read(BACKUP_FLASH_DEV, BACKUP_OFFSET +
				  offsetof(boot_request_area_t, entries) + entry,
				  value, sizeof(uint8_t));
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
	}

	/* Both areas are invalid. */
	return -ENOENT;
}

int boot_request_mem_write(size_t entry, uint8_t *value)
{
	boot_request_area_t area_copy;

	if ((value == NULL) || (entry >= BOOT_REQUEST_ENTRIES_SIZE)) {
		return -EINVAL;
	}

	/* Update only the main region. It will be backed up after a reboot. */
	if (flash_read(MAIN_FLASH_DEV, MAIN_OFFSET, (uint8_t *)&area_copy,
	    sizeof(boot_request_area_t)) != 0) {
		return -EIO;
	}

	if (area_copy.entries[entry] == *value) {
		/* No need to update the entry. */
		return 0;
	}

	area_copy.entries[entry] = *value;

	if (flash_write(MAIN_FLASH_DEV, MAIN_OFFSET, (uint8_t *)&area_copy,
	    sizeof(boot_request_area_t)) != 0) {
		return -EROFS;
	};

	/* Update checksum. */
	return boot_request_commit(MAIN_FLASH_DEV, MAIN_OFFSET);
}

int boot_request_mem_selective_erase(size_t *nv_indexes, size_t nv_count)
{
#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
	boot_request_area_t new_area;
	boot_request_area_t old_area;
	bool update_needed = false;
	size_t i;
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */

	if ((nv_indexes == NULL) && (nv_count != 0)) {
		return -EINVAL;
	}

#ifdef CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP
	if (boot_request_area_valid(BACKUP_FLASH_DEV, BACKUP_OFFSET)) {
		/* Prepare a new area with all entries (including prefix and CRC) set to 0xFF. */
		memset(&new_area, 0xFF, sizeof(boot_request_area_t));

		/* Copy non-volatile entries. */
		for (i = 0; i < nv_count; i++) {
			if (flash_read(BACKUP_FLASH_DEV, BACKUP_OFFSET +
					offsetof(boot_request_area_t, entries) + nv_indexes[i],
					&new_area.entries[nv_indexes[i]], sizeof(uint8_t)) != 0) {
				return -EBADF;
			};
		}

		/* Read the current value of the main area. */
		if (flash_read(MAIN_FLASH_DEV, MAIN_OFFSET, (uint8_t *)&old_area,
		    sizeof(boot_request_area_t)) != 0) {
			return -EIO;
		}

		/* Check if there is a need to update entries. */
		for (i = 0; i < ARRAY_SIZE(new_area.entries); i++) {
			if (new_area.entries[i] != old_area.entries[i]) {
				update_needed = true;
				break;
			}
		}

		if (!update_needed) {
			/* No need to update the area. */
			return 0;
		}

		/* Erase the main area. */
		if (flash_erase(MAIN_FLASH_DEV, MAIN_OFFSET, sizeof(boot_request_area_t)) != 0) {
			return -EIO;
		}

		/* Initialize the main area with prepared values. */
		if (flash_write(MAIN_FLASH_DEV, MAIN_OFFSET, (uint8_t *)&new_area,
		    sizeof(boot_request_area_t)) != 0) {
			return -EROFS;
		};

		/* Validate the main area by updating checksum. */
		if (boot_request_commit(MAIN_FLASH_DEV, MAIN_OFFSET) != 0) {
			return -EIO;
		}

		/* Sync backup with the main area. */
		return boot_request_copy(BACKUP_FLASH_DEV, BACKUP_OFFSET, MAIN_FLASH_DEV,
					 MAIN_OFFSET);
	}

	/* Backup is invalid, do not clear the memory to keep at least one valid copy. */
	return 0;
#else /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
	/* Selective erase is not supported if there is no backup area. */
	if (nv_count != 0) {
		return -ENOTSUP;
	}

	return boot_request_clear(MAIN_FLASH_DEV, MAIN_OFFSET);
#endif /* CONFIG_NRF_MCUBOOT_BOOT_REQUEST_PREFERENCE_KEEP */
}
