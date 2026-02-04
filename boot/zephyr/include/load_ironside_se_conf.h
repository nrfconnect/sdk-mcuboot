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

/** Parse custom TLVs containing configuration metadata. */
int nrf_parse_custom_tlv_data(struct boot_loader_state *state, int slot);

/** Configure peripherals using parameters loaded from PERIPHCONF TLVs. */
int nrf_load_periphconf(void);

#ifdef __cplusplus
}
#endif

#endif /* H_LOAD_IRONSIDE_SE_CONF_ */

