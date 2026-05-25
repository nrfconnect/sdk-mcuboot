/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <bootutil/mcuboot_uuid.h>
#include <bootutil/mcuboot_uuid_generated.h>
#include <assert.h>

static bool boot_uuid_compare(const struct image_uuid *uuid1, const struct image_uuid *uuid2)
{
	if ((uuid1 == NULL) || (uuid2 == NULL)) {
		return false;
	}

	return memcmp(uuid1->raw, uuid2->raw, sizeof(uuid1->raw)) == 0;
}

#ifdef CONFIG_MCUBOOT_UUID_VID
static bool vid_matches_map_entry(const struct image_uuid *vid, const struct uuid_map_entry *entry)
{
	const struct uuid_map_entry *vid_map = NULL;
	size_t n_entries = boot_uuid_vid_map_get(&vid_map);

	if (vid == NULL) {
		return false;
	}

	for (size_t i = 0; i < n_entries; i++) {
		if ((vid_map[i].dev == entry->dev) && (vid_map[i].off == entry->off) &&
		    (vid_map[i].size == entry->size) && boot_uuid_compare(vid, &vid_map[i].uuid)) {
			return true;
		}
	}

	return false;
}

fih_ret boot_uuid_vid_match(const struct flash_area *fap, const struct image_uuid *uuid_vid)
{
	const struct uuid_map_entry *map = NULL;
	size_t n_uuids = boot_uuid_vid_map_get(&map);
	int fa_ret;
	uintptr_t base;

	assert(fap != NULL);

	/* The memory map contains absolute addresses - fetch the base address for the area
	 * in question.
	 */
	fa_ret = flash_device_base(flash_area_get_device_id(fap), &base);
	if (fa_ret != 0) {
		FIH_RET(FIH_FAILURE);
	}

	for (size_t i = 0; i < n_uuids; i++) {
		if ((map[i].dev == fap->fa_dev) && (fap->fa_off + base == map[i].off) &&
		    (fap->fa_size == map[i].size)) {
			if (boot_uuid_compare(uuid_vid, &map[i].uuid)) {
				FIH_RET(FIH_SUCCESS);
			}
		}
	}

	FIH_RET(FIH_FAILURE);
}
#endif /* CONFIG_MCUBOOT_UUID_VID */

#ifdef CONFIG_MCUBOOT_UUID_CID
fih_ret boot_uuid_cid_match(const struct flash_area *fap, const struct image_uuid *uuid_cid)
{
	const struct uuid_map_entry *map = NULL;
	size_t n_uuids = boot_uuid_cid_map_get(&map);
	int fa_ret;
	uintptr_t base;

	assert(fap != NULL);

	/* The memory map contains absolute addresses - fetch the base address for the area
	 * in question.
	 */
	fa_ret = flash_device_base(flash_area_get_device_id(fap), &base);
	if (fa_ret != 0) {
		FIH_RET(FIH_FAILURE);
	}

	for (size_t i = 0; i < n_uuids; i++) {
		if ((map[i].dev == fap->fa_dev) && (fap->fa_off + base == map[i].off) &&
		    (fap->fa_size == map[i].size)) {
			if (boot_uuid_compare(uuid_cid, &map[i].uuid)) {
				FIH_RET(FIH_SUCCESS);
			}
		}
	}

	FIH_RET(FIH_FAILURE);
}

int boot_uuid_find_image(const struct image_uuid *cid, const struct image_uuid *vid,
			 size_t *image_index, size_t *partition_index)
{
	const struct uuid_map_entry *cid_map = NULL;
	size_t n_entries;

	if ((cid == NULL) || (image_index == NULL) || (partition_index == NULL)) {
		return -EINVAL;
	}

#ifdef MCUBOOT_UUID_VID
	if (vid == NULL) {
		return -EINVAL;
	}
#endif

	n_entries = boot_uuid_cid_map_get(&cid_map);
	if ((n_entries == 0) || (cid_map == NULL)) {
		return -ENOENT;
	}

	for (size_t i = 0; i < n_entries; i++) {
		if (!boot_uuid_compare(cid, &cid_map[i].uuid)) {
			continue;
		}

#ifdef MCUBOOT_UUID_VID
		if (!vid_matches_map_entry(vid, &cid_map[i])) {
			continue;
		}
#endif

		*image_index = cid_map[i].image_index;
		*partition_index = cid_map[i].partition_index;

		return 0;
	}

	return -ENOENT;
}
#endif /* CONFIG_MCUBOOT_UUID_CID */

fih_ret boot_uuid_init(void)
{
	FIH_RET(FIH_SUCCESS);
}
