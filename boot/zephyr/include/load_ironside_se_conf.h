/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef H_LOAD_IRONSIDE_SE_CONF_
#define H_LOAD_IRONSIDE_SE_CONF_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct boot_loader_state;
struct image_header;
struct flash_area;

/* Validate custom TLVs containing configuration metadata. */
int nrf_validate_custom_tlv_data(const struct image_header *hdr, const struct flash_area *fap,
				 uint32_t slot_off, uint16_t tlv_type, uint32_t tlv_off,
				 uint16_t tlv_len);

/* Add parameters from custom TLVs containing configuration metadata
 * for the active slot. The parameters added here are loaded using the nrf_load_* functions.
 */
int nrf_add_custom_tlv_data(struct boot_loader_state *state, int slot);

/* Configure peripherals using parameters loaded from PERIPHCONF TLVs. */
int nrf_load_periphconf(void);

#ifdef __cplusplus
}
#endif

#endif /* H_LOAD_IRONSIDE_SE_CONF_ */
