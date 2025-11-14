/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025 Nordic Semiconductor ASA
 */

#ifndef __BOOT_REQUEST_MEM_H__
#define __BOOT_REQUEST_MEM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize the boot request memory areas.
 *
 * @retval 0       if initialization is successful.
 * @retval -EIO    if unable to access the flash devices.
 * @retval -EROFS  if unable to initialize memory.
 */
int boot_request_mem_init(void);

/**
 * @brief Prepare main area for updates and check if it can be updated.
 *
 * @details This function is used to verify if there is a valid backup area, so
 *          an interrupted write attempts will not clear all of the boot requests.
 *          Additionally, if the main area is corrupted, this function will
 *          restore it from the backup.
 *
 * @note This function intentionally does not modify the backup area, so the
 *       backup area can be set as read-only right before leaving the bootloader.
 *
 * @return true if the area is updateable; false otherwise.
 */
bool boot_request_mem_wrtie_prepare(void);

/**
 * @brief Read a single boot request entry from a memory.
 *
 * @param[in]  entry Index of the entry to read.
 * @param[out] value Pointer to store the read value.
 *
 * @retval 0       if entry is successfully read.
 * @retval -EINVAL if input parameters are invalid.
 * @retval -ENOENT if both retention areas are invalid.
 */
int boot_request_mem_read(size_t entry, uint8_t *value);

/**
 * @brief Write a single boot request entry to a memory.
 *
 * @param[in] entry Index of the entry to write.
 * @param[in] value Pointer to the value to write.
 *
 * @retval 0       if entry is successfully written.
 * @retval -EINVAL if input parameters are invalid.
 * @retval -EIO    if unable to read from the area.
 * @retval -EROFS  if unable to write to the area.
 */
int boot_request_mem_write(size_t entry, uint8_t *value);

/**
 * @brief Selectively erase boot request entries, keeping specified indexes intact.
 *
 * @param[in] nv_indexes Array of indexes of entries to keep.
 * @param[in] nv_count   Number of entries in the array.
 *
 * @retval 0        if selective erase is successful.
 * @retval -EINVAL  if one of the input arguments is not correct (i.e. NULL).
 * @retval -ENOTSUP if selective erase is not supported.
 * @retval -EIO     if unable to erase or read from the area.
 * @retval -EBADF   if unable to read a entry at a given index.
 * @retval -EROFS   if unable to write a entry at a given index.
 */
int boot_request_mem_selective_erase(size_t *nv_indexes, size_t nv_count);

#ifdef __cplusplus
}
#endif

#endif /* __BOOT_REQUEST_MEMH__ */
