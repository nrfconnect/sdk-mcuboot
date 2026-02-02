/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <bootutil/mcuboot_uuid.h>
#include <bootutil/mcuboot_uuid_generated.h>

static bool boot_uuid_compare(const struct image_uuid *uuid1, const struct image_uuid *uuid2)
{
	return memcmp(uuid1->raw, uuid2->raw, ARRAY_SIZE(uuid1->raw)) == 0;
}

fih_ret boot_uuid_init(void)
{
	FIH_RET(FIH_SUCCESS);
}

#ifdef CONFIG_MCUBOOT_UUID_VID
fih_ret boot_uuid_vid_match(const struct flash_area *fap, const struct image_uuid *uuid_vid)
{
	const struct uuid_map_entry *map = NULL;
	if (uuid_vid == NULL) {
		FIH_RET(FIH_FAILURE);
	}
	size_t n_uuids = boot_uuid_vid_map_get(&map);
	for (size_t i = 0; i < n_uuids; i++) {
		if ((map[i].dev == fap->fa_dev) && (fap->fa_off >= map[i].off) &&
		    ((fap->fa_off + fap->fa_size) <= map[i].off + map[i].size)) {
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
	if (uuid_cid == NULL) {
		FIH_RET(FIH_FAILURE);
	}

	const struct uuid_map_entry *map = NULL;
	size_t n_uuids = boot_uuid_cid_map_get(&map);

	for (size_t i = 0; i < n_uuids; i++) {
		if ((map[i].dev == fap->fa_dev) && (fap->fa_off >= map[i].off) &&
		    ((fap->fa_off + fap->fa_size) <= map[i].off + map[i].size)) {
			if (boot_uuid_compare(uuid_cid, &map[i].uuid)) {
				FIH_RET(FIH_SUCCESS);
			}
		}
	}

	FIH_RET(FIH_FAILURE);
}
#endif /* CONFIG_MCUBOOT_UUID_CID */
