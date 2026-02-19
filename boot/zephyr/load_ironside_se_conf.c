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

#define PERIPHCONF_TLV_ID CONFIG_NCS_MCUBOOT_PERIPHCONF_TLV_ID

#define MAX_PERIPHCONFS CONFIG_NCS_MCUBOOT_LOAD_PERIPHCONF_MAX_BLOBS

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

static int validate_or_add(const struct image_header *hdr, const struct flash_area *fap,
			   uint32_t slot_off, uint16_t tlv_type, uint32_t tlv_off, uint16_t tlv_len,
			   bool add_to_params)
{
	uint32_t pconf_params[2];
	uint32_t conf_base_address;
	uintptr_t flash_base;
	uintptr_t absolute_address;
	int rc;

	assert(fap != NULL);
	assert(hdr != NULL);

	if (tlv_type != PERIPHCONF_TLV_ID) {
		/* Not a TLV of concern. */
		return 0;
	}

	if (tlv_len % sizeof(pconf_params) != 0) {
		/* Must have some multiple of [offset, count]. */
		return BOOT_EBADIMAGE;
	}

	rc = flash_device_base(flash_area_get_device_id(fap), &flash_base);
	if (rc != 0) {
		return BOOT_EFLASH;
	}

	/* The offsets in the PERIPHCONF TLV  are calculated from the start
	 * of the image firmware area.
	 */
	conf_base_address = flash_base + slot_off + hdr->ih_hdr_size;

	for (uint32_t entry_idx = 0; entry_idx < tlv_len / sizeof(pconf_params); entry_idx++) {
		const uint32_t entry_off = tlv_off + entry_idx * sizeof(pconf_params);

		rc = LOAD_IMAGE_DATA(hdr, fap, entry_off, pconf_params, sizeof(pconf_params));
		if (rc != 0) {
			return BOOT_EFLASH;
		}

		const uintptr_t pconf_offset = pconf_params[0];
		const size_t pconf_entry_count = pconf_params[1];

		/* The PERIPHCONF TLV points to an array of struct periphconf_entry that will
		 * be sent to IronSide SE. We expect the entire array to be contained within the
		 * image.
		 */
		if (!entries_contained_in_image(pconf_offset, pconf_entry_count,
						sizeof(struct periphconf_entry),
						hdr->ih_img_size)) {
			return BOOT_EBADIMAGE;
		}

		if (add_to_params) {
			absolute_address = conf_base_address + pconf_offset;
			rc = periphconf_params_add(absolute_address, pconf_entry_count);
			if (rc != 0) {
				return BOOT_ENOMEM;
			}

			BOOT_LOG_DBG("Added PERIPHCONF %u: %p, %u", num_periphconfs,
				     (void *)absolute_address, (unsigned int)pconf_params[1]);
		}
	}

	return 0;
}

int nrf_validate_custom_tlv_data(const struct image_header *hdr, const struct flash_area *fap,
				 uint32_t slot_off, uint16_t tlv_type, uint32_t tlv_off,
				 uint16_t tlv_len)
{
	return validate_or_add(hdr, fap, slot_off, tlv_type, tlv_off, tlv_len, false);
}

int nrf_add_custom_tlv_data(struct boot_loader_state *state, int slot)
{
	const struct flash_area *fap = NULL;
	const struct image_header *hdr;
	struct image_tlv_iter it;
	uint32_t slot_off;
	uint32_t off;
	uint16_t len;
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

	slot_off = boot_img_slot_off(state, slot);

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

	return validate_or_add(hdr, fap, slot_off, PERIPHCONF_TLV_ID, off, len, true);
}

/* @warning Do not touch any peripherals apart from BELLBOARD in this function (incl. logging).
 * This function is expected to be called after peripheral/logging teardown.
 * MCUboot may lose access to peripherals during the function.
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
