/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NRF53_CPUNET_CTL_H
#define __NRF53_CPUNET_CTL_H
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initiate a network core update
 *
 * @param addr Adress to PCD_CMD structure set to PCD_CMD_ADDRESS if not
 *	       provided.
 *
 * @param src_addr Start address of the data which is to be copied into the
 *		   network core.
 * @param len Length of the data which is to be copied into the network core.
 *
 */
int do_network_core_update(void *src_addr, size_t len);

/**
 * Lock RAM used to communicate with network bootloader
 */
void lock_ipc_ram_with_spu();
#ifdef __cplusplus
}
#endif

#endif /* NRF53_CPUNET_CTL */
