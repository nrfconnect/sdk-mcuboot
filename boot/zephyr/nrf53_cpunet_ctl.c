#include <zephyr.h>
#include <hal/nrf_reset.h>
#include <hal/nrf_spu.h>
#include <dfu/pcd.h>
#include <pm_config.h>
#include "bootutil/bootutil_log.h"
MCUBOOT_LOG_MODULE_DECLARE(mcuboot);

int do_network_core_update(void *addr, void *src_addr, size_t len)
{
	struct pcd_cmd *rsp = (struct pcd_cmd *) PCD_RSP_ADDRESS;
	if (addr == NULL) {
		addr = (void *)PCD_CMD_ADDRESS;

	}

	struct pcd_cmd *cmd = (struct pcd_cmd *)addr;

	rsp->magic = PCD_CMD_MAGIC_COPY;
	cmd->magic = PCD_CMD_MAGIC_COPY;
	cmd->src_addr = src_addr;
	cmd->len = len;
	cmd->offset = 0x10800;
	nrf_spu_ramregion_set(NRF_SPU,
			      APP_CORE_SRAM_SIZE/CONFIG_NRF_SPU_RAM_REGION_SIZE,
			      true, NRF_SPU_MEM_PERM_READ, true);

	nrf_reset_network_force_off(NRF_RESET, false);
	BOOT_LOG_INF("Turned on network core");

	while (rsp->magic == PCD_CMD_MAGIC_COPY)
		;

	if (rsp->magic == PCD_CMD_MAGIC_FAIL) {
		BOOT_LOG_ERR("Network core update failed");
		return -1;
	}

	nrf_reset_network_force_off(NRF_RESET, true);
	BOOT_LOG_INF("Turned off network core");

	return 0;
}

/* This should come from DTS, possibly an overlay. */
#if defined(CONFIG_BOARD_NRF5340PDK_NRF5340_CPUAPP)
#define CPUNET_UARTE_PIN_TX  25
#define CPUNET_UARTE_PIN_RX  26
#define CPUNET_UARTE_PORT_TRX NRF_P0
#define CPUNET_UARTE_PIN_RTS 10
#define CPUNET_UARTE_PIN_CTS 12
#elif defined(CONFIG_BOARD_NRF5340DK_NRF5340_CPUAPP)
#define CPUNET_UARTE_PIN_TX  1
#define CPUNET_UARTE_PIN_RX  0
#define CPUNET_UARTE_PORT_TRX NRF_P1
#define CPUNET_UARTE_PIN_RTS 11
#define CPUNET_UARTE_PIN_CTS 10
#endif

#if defined(CONFIG_BT_CTLR_DEBUG_PINS_CPUAPP)
#include <../subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/debug.h>
#else
#define DEBUG_SETUP()
#endif

void enable_network_core_debug_pins(void)
{
	/* UARTE */
	/* Assign specific GPIOs that will be used to get UARTE from
	 * nRF5340 Network MCU.
	 */
	CPUNET_UARTE_PORT_TRX->PIN_CNF[CPUNET_UARTE_PIN_TX] =
	GPIO_PIN_CNF_MCUSEL_NetworkMCU << GPIO_PIN_CNF_MCUSEL_Pos;
	CPUNET_UARTE_PORT_TRX->PIN_CNF[CPUNET_UARTE_PIN_RX] =
	GPIO_PIN_CNF_MCUSEL_NetworkMCU << GPIO_PIN_CNF_MCUSEL_Pos;
	NRF_P0->PIN_CNF[CPUNET_UARTE_PIN_RTS] =
	GPIO_PIN_CNF_MCUSEL_NetworkMCU << GPIO_PIN_CNF_MCUSEL_Pos;
	NRF_P0->PIN_CNF[CPUNET_UARTE_PIN_CTS] =
	GPIO_PIN_CNF_MCUSEL_NetworkMCU << GPIO_PIN_CNF_MCUSEL_Pos;

	/* Route Bluetooth Controller Debug Pins */
	DEBUG_SETUP();

	/* Retain nRF5340 Network MCU in Secure domain (bus
	 * accesses by Network MCU will have Secure attribute set).
	 */
	NRF_SPU->EXTDOMAIN[0].PERM = 1 << 4;
}
