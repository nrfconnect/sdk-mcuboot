/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief MCUBoot IronSide security counters implementation.
 */

#include <stdint.h>
#include <nrf_ironside/counter.h>
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/bootutil_public.h"

#define IRONSIDE_COUNTER_READ_RETRIES 3

fih_int boot_nv_security_counter_init(void)
{
	return FIH_SUCCESS;
}

fih_int boot_nv_security_counter_get(uint32_t image_id, fih_int *security_cnt)
{
	uint32_t cur_sec_cnt[IRONSIDE_COUNTER_READ_RETRIES];
	size_t i;

	if (security_cnt == NULL) {
		FIH_RET(FIH_FAILURE);
	}

	if (image_id > IRONSIDE_COUNTER_NUM) {
		FIH_RET(FIH_FAILURE);
	}

	/* Since the IronSide service is not protected against fault injection,
	 * read the counter multiple times and compare the results.
	 */
	for (i = 0; i < IRONSIDE_COUNTER_READ_RETRIES; i++) {
		if (ironside_counter_get(image_id, &cur_sec_cnt[i]) != 0) {
			FIH_RET(FIH_FAILURE);
		}
	}

	for (i = 1; i < IRONSIDE_COUNTER_READ_RETRIES; i++) {
		if (cur_sec_cnt[0] != cur_sec_cnt[i]) {
			FIH_RET(FIH_FAILURE);
		}
	}

	if (cur_sec_cnt[0] <= IRONSIDE_COUNTER_MAX_VALUE) {
		*security_cnt = fih_int_encode(cur_sec_cnt[0]);
		FIH_RET(FIH_SUCCESS);
	}

	FIH_RET(FIH_FAILURE);
}

int32_t boot_nv_security_counter_update(uint32_t image_id, uint32_t img_security_cnt)
{
	if ((img_security_cnt > IRONSIDE_COUNTER_MAX_VALUE) || (image_id > IRONSIDE_COUNTER_NUM)) {
		return -BOOT_EBADARGS;
	}

	if (ironside_counter_set(image_id, img_security_cnt) != 0) {
		return -BOOT_EBADSTATUS;
	}

	return 0;
}

fih_int boot_nv_security_counter_is_update_possible(uint32_t image_id, uint32_t img_security_cnt)
{
	fih_int security_cnt = FIH_FAILURE;
	fih_int fih_err;

	FIH_CALL(boot_nv_security_counter_get, fih_err, image_id, &security_cnt);
	if (FIH_EQ(fih_err, FIH_SUCCESS)) {
		int cnt = fih_int_decode(security_cnt);

		if ((cnt <= IRONSIDE_COUNTER_MAX_VALUE) && (cnt <= img_security_cnt)) {
			FIH_RET(FIH_SUCCESS);
		}
	}

	FIH_RET(FIH_FAILURE);
}

int32_t boot_nv_security_counter_lock(uint32_t image_id)
{
	if (image_id > IRONSIDE_COUNTER_NUM) {
		return -BOOT_EBADARGS;
	}

	if (ironside_counter_lock(image_id) != 0) {
		return -BOOT_EBADSTATUS;
	}

	return 0;
}
