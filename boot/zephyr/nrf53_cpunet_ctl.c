/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <hal/nrf_reset.h>
#include <hal/nrf_spu.h>
#include <dfu/pcd.h>
#include <pm_config.h>
#include "bootutil/bootutil_log.h"
MCUBOOT_LOG_MODULE_DECLARE(mcuboot);

static void set_pcd_cmd_struct(void *src_addr, size_t len)
{
	struct pcd_cmd *cmd = (struct pcd_cmd *)PCD_CMD_ADDRESS;
	cmd->magic = PCD_CMD_MAGIC_COPY;
	cmd->src_addr = src_addr;
	cmd->len = len;
	cmd->offset = 0x10800;
}

static bool is_copying(void)
{
	struct pcd_cmd *cmd = (struct pcd_cmd *)PCD_CMD_ADDRESS;
	if (cmd->magic == PCD_CMD_MAGIC_COPY) {
		return true;
	} 

	return false;
}

static bool successful(void)
{
	struct pcd_cmd *cmd = (struct pcd_cmd *)PCD_CMD_ADDRESS;
	if (cmd->magic != PCD_CMD_MAGIC_DONE) {
		return false;
	}
	return true;
}

int do_network_core_update(void *src_addr, size_t len)
{
	/* Ensure that the network core is turned off */
	nrf_reset_network_force_off(NRF_RESET, true);
	set_pcd_cmd_struct(src_addr, len);

	nrf_reset_network_force_off(NRF_RESET, false);
	BOOT_LOG_INF("Turned on network core");

	while (is_copying())
		;

	if (!successful()) {
		BOOT_LOG_ERR("Network core update failed");
		return -1;
	}

	nrf_reset_network_force_off(NRF_RESET, true);
	BOOT_LOG_INF("Turned off network core");

	return 0;
}

void lock_ipc_ram_with_spu(){
	nrf_spu_ramregion_set(NRF_SPU,
			      APP_CORE_SRAM_SIZE/CONFIG_NRF_SPU_RAM_REGION_SIZE,
			      true, NRF_SPU_MEM_PERM_READ, true);
}
