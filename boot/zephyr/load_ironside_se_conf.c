/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "../bootutil/src/bootutil_priv.h"
#include "flash_map_backend/flash_map_backend.h"
#include <zephyr/sys/math_extras.h>

#include <ironside/se/api.h>

BOOT_LOG_MODULE_DECLARE(mcuboot);

#define PERIPHCONF_TLV_ID CONFIG_MCUBOOT_NRF_PERIPHCONF_TLV_ID

/* TODO: what is a reasonable limit? can we avoid a limit */
#define MAX_PERIPHCONFS CONFIG_MCUBOOT_NRF_LOAD_PERIPHCONF_MAX_BLOBS

/* To store parsed PERIPHCONF parameters between reading them from
 * the TLVs and calling the IronSide API.
 */
static struct {
	uintptr_t address;
	size_t count;
} periphconfs[MAX_PERIPHCONFS];

static size_t num_periphconfs = 0;

/* Checks that the blob defined by (entries offset, entry count)
 * is contained in the image region.
 */
static bool entries_contained_in_image(uint32_t offset, size_t count, size_t entry_size,
				       size_t image_size)
{
	uint32_t pconf_entry_size;
	uint32_t pconf_offset_end;

	if (u32_mul_overflow(count, sizeof(struct periphconf_entry), &pconf_entry_size)) {
		return false;
	}

	if (u32_add_overflow(offset, pconf_entry_size, &pconf_offset_end)) {
		return false;
	}

	/* Implictly range checks start offset as well. */
	if (pconf_offset_end >= image_size) {
		return false;
	}

	return true;
}

static int periphconf_params_add(uintptr_t address, size_t count)
{
	if (num_periphconfs >= MAX_PERIPHCONFS) {
		return -1;
	}

	periphconfs[num_periphconfs].address = address;
	periphconfs[num_periphconfs].count = count;
	num_periphconfs++;

	return 0;
}

int nrf_parse_custom_tlv_data(struct boot_loader_state *state, int slot)
{
	const struct flash_area *fap = NULL;
	const struct image_header *hdr;
	struct image_tlv_iter it;
	uint32_t pconf_params[2];
	uint32_t off;
	uint16_t len;
	uint32_t conf_base_address;
	uintptr_t flash_base;
	uintptr_t absolute_address;
	int rc;

	if (state == NULL) {
		return BOOT_EBADARGS;
	}

	hdr = boot_img_hdr(state, slot);
	if (hdr == NULL) {
		return BOOT_EBADARGS;
	}

	/* The PERIPHCONF TLV is in the protected part of the TLV area. */
	if (hdr->ih_protect_tlv_size == 0) {
		/* TLV not present; this is valid. */
		return 0;
	}

	fap = BOOT_IMG_AREA(state, slot);
	assert(fap != NULL);

	/* The offsets in the PERIPHCONF TLV  are calculated from the start
	 * of the image firmware area.
	 */
	rc = flash_device_base(flash_area_get_device_id(fap), &flash_base);
	if (rc != 0) {
		return BOOT_EFLASH;
	}

	conf_base_address = flash_base + boot_img_slot_off(state, slot) + hdr->ih_hdr_size;

#if defined(MCUBOOT_SWAP_USING_OFFSET)
	it.start_off = boot_get_state_secondary_offset(state, fap);
#endif

	rc = bootutil_tlv_iter_begin(&it, hdr, fap, PERIPHCONF_TLV_ID, true);
	if (rc) {
		return rc;
	}

	/* Traverse through the protected TLV area to find
	 * the PERIPHCONF TLV.
	 */
	rc = bootutil_tlv_iter_next(&it, &off, &len, NULL);
	switch (rc) {
	case 0:
		/* TLV found; continue parsing it below. */
		break;
	case 1:
		/* TLV not present; this is valid. */
		return 0;
	default:
		/* Error condition. */
		return rc;
	}

	if (len % sizeof(pconf_params) != 0) {
		/* Must have some multiple of [offset, count]. */
		return BOOT_EBADIMAGE;
	}

	for (uint32_t entry_idx = 0; entry_idx < len / sizeof(pconf_params); entry_idx++) {
		const uint32_t entry_off = off + entry_idx * sizeof(pconf_params);

		rc = LOAD_IMAGE_DATA(hdr, fap, entry_off, pconf_params, sizeof(pconf_params));
		if (rc != 0) {
			return BOOT_EFLASH;
		}

		const uintptr_t pconf_offset = pconf_params[0];
		const size_t pconf_entry_count = pconf_params[1];

		if (!entries_contained_in_image(pconf_offset, pconf_entry_count,
						sizeof(struct periphconf_entry),
						hdr->ih_img_size)) {
			return BOOT_EBADIMAGE;
		}

		absolute_address = conf_base_address + pconf_offset;
		rc = periphconf_params_add(absolute_address, pconf_entry_count);
		if (rc != 0) {
			return BOOT_ENOMEM;
		}

		BOOT_LOG_DBG("Added PERIPHCONF %u: %p, %u", num_periphconfs,
			     (void *)absolute_address, (unsigned int)pconf_params[1]);
	}

	return 0;
}

/* @warning Do not touch any peripherals apart from BELLBOARD in this function (incl. logging).
 * This function is expected to be called after peripheral/logging teardown.
 * It may remove MCUboot's access to certain peripherals.
 */
int nrf_load_periphconf(void)
{
	int rc;
	struct ironside_se_periphconf_status status = {0};

	for (size_t i = 0; i < num_periphconfs; i++) {
		status = ironside_se_periphconf_write((void *)periphconfs[i].address,
						      periphconfs[i].count);
		if (status.status != 0) {
			/* Stop loading, but continue with finalization below. */
			break;
		}
	}

	rc = ironside_se_periphconf_finish_init();
	if (rc != 0) {
		return -1;
	}

	/* Non-zero status indicates that the load loop exited early due to error. */
	if (status.status != 0) {
		return -1;
	}

	return 0;
}
