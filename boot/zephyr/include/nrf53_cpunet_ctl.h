
/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef H_NRF53_CPUNET_CTL_
#define H_NRF53_CPUNET_CTL_

/**
 * Enable debug pins and uart output 
 */
void enable_network_core_debug_pins(void);

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
int do_network_core_update(void *addr, void *src_addr, size_t len);
#endif
