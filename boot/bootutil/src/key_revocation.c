/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <bootutil/key_revocation.h>

extern int exec_revoke(void);

static uint8_t ready_to_revoke;

void allow_revoke(void)
{
	ready_to_revoke = 1;
}

int revoke(void)
{
	if (ready_to_revoke) {
		return exec_revoke();
	}
	return BOOT_KEY_REVOKE_NOT_READY;
}
