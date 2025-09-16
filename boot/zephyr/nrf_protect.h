/* Copyright (c) 5020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NRF_PROTECT_H__
#define NRF_PROTECT_H__

#if USE_PARTITION_MANAGER && CONFIG_FPROTECT

#include <pm_config.h>
#include <fprotect.h>

#ifdef PM_S1_ADDRESS
/* MCUBoot is stored in either S0 or S1, protect both */
#define PROTECT_SIZE (PM_MCUBOOT_PRIMARY_ADDRESS - PM_S0_ADDRESS)
#define PROTECT_ADDR PM_S0_ADDRESS
#else
/* There is only one instance of MCUBoot */
#define PROTECT_SIZE (PM_MCUBOOT_PRIMARY_ADDRESS - PM_MCUBOOT_ADDRESS)
#define PROTECT_ADDR PM_MCUBOOT_ADDRESS
#endif

#ifdef CONFIG_SOC_SERIES_NRF54LX
#if defined(CONFIG_FPROTECT_ALLOW_COMBINED_REGIONS)
#define REGION_SIZE_MAX (62 *1024)
#if (PROTECT_ADDR != 0)
#error "FPROTECT with combined regions can only be used to protect from address 0"
#endif
#else
#define REGION_SIZE_MAX (31 *1024)
#endif

#if (PROTECT_SIZE > REGION_SIZE_MAX)
#error "FPROTECT size too large"
#endif

#endif /* CONFIG_SOC_SERIES_NRF54LX */

#endif /* USE_PARTITION_MANAGER && CONFIG_FPROTECT */

#endif /* NRF_PROTECT_H__ */
