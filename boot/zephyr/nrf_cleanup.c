/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <hal/nrf_clock.h>
#include <hal/nrf_uarte.h>
#include <haly/nrfy_uarte.h>
#if defined(NRF_RTC0) || defined(NRF_RTC1) || defined(NRF_RTC2)
    #include <hal/nrf_rtc.h>
#endif
#if defined(NRF_PPI)
    #include <hal/nrf_ppi.h>
#endif
#if defined(NRF_DPPIC)
    #include <hal/nrf_dppi.h>
#endif

#include <string.h>

#if USE_PARTITION_MANAGER
#include <pm_config.h>
#endif

#if defined(NRF_UARTE0) || defined(NRF_UARTE1) || defined(NRF_UARTE20) ||   \
    defined(NRF_UARTE30)
#define NRF_UARTE_CLEANUP
#endif

#define NRF_UARTE_SUBSCRIBE_CONF_OFFS offsetof(NRF_UARTE_Type, SUBSCRIBE_STARTRX)
#define NRF_UARTE_SUBSCRIBE_CONF_SIZE (offsetof(NRF_UARTE_Type, EVENTS_CTS) -\
                                       NRF_UARTE_SUBSCRIBE_CONF_OFFS)

#define NRF_UARTE_PUBLISH_CONF_OFFS offsetof(NRF_UARTE_Type, PUBLISH_CTS)
#define NRF_UARTE_PUBLISH_CONF_SIZE (offsetof(NRF_UARTE_Type, SHORTS) -\
                                     NRF_UARTE_PUBLISH_CONF_OFFS)

#if defined(NRF_RTC0) || defined(NRF_RTC1) || defined(NRF_RTC2)
static inline void nrf_cleanup_rtc(NRF_RTC_Type * rtc_reg)
{
    nrf_rtc_task_trigger(rtc_reg, NRF_RTC_TASK_STOP);
    nrf_rtc_event_disable(rtc_reg, 0xFFFFFFFF);
    nrf_rtc_int_disable(rtc_reg, 0xFFFFFFFF);
}
#endif

#if defined(NRF_UARTE_CLEANUP)
static NRF_UARTE_Type *nrf_uarte_to_clean[] = {
#if defined(NRF_UARTE0)
    NRF_UARTE0,
#endif
#if defined(NRF_UARTE1)
    NRF_UARTE1,
#endif
#if defined(NRF_UARTE20)
    NRF_UARTE20,
#endif
#if defined(NRF_UARTE30)
    NRF_UARTE30,
#endif
};
#endif

static void nrf_cleanup_clock(void)
{
    nrf_clock_int_disable(NRF_CLOCK, 0xFFFFFFFF);
}

void nrf_cleanup_peripheral(void)
{
#if defined(NRF_RTC0)
    nrf_cleanup_rtc(NRF_RTC0);
#endif
#if defined(NRF_RTC1)
    nrf_cleanup_rtc(NRF_RTC1);
#endif
#if defined(NRF_RTC2)
    nrf_cleanup_rtc(NRF_RTC2);
#endif

#if defined(NRF_UARTE_CLEANUP)
    for (int i = 0; i < sizeof(nrf_uarte_to_clean) / sizeof(nrf_uarte_to_clean[0]); ++i) {
        NRF_UARTE_Type *current = nrf_uarte_to_clean[i];

        nrfy_uarte_int_disable(current, 0xFFFFFFFF);
        nrfy_uarte_int_uninit(current);
        nrfy_uarte_task_trigger(current, NRF_UARTE_TASK_STOPRX);

        nrfy_uarte_event_clear(current, NRF_UARTE_EVENT_RXSTARTED);
        nrfy_uarte_event_clear(current, NRF_UARTE_EVENT_ENDRX);
        nrfy_uarte_event_clear(current, NRF_UARTE_EVENT_RXTO);
        nrfy_uarte_disable(current);

#if defined(NRF_DPPIC)
        /* Clear all SUBSCRIBE configurations. */
        memset((uint8_t *)current + NRF_UARTE_SUBSCRIBE_CONF_OFFS, 0,
               NRF_UARTE_SUBSCRIBE_CONF_SIZE);
        /* Clear all PUBLISH configurations. */
        memset((uint8_t *)current + NRF_UARTE_PUBLISH_CONF_OFFS, 0,
               NRF_UARTE_PUBLISH_CONF_SIZE);
#endif
    }
#endif

#if defined(NRF_PPI)
    nrf_ppi_channels_disable_all(NRF_PPI);
#endif
#if defined(NRF_DPPIC)
    nrf_dppi_channels_disable_all(NRF_DPPIC);
#endif
    nrf_cleanup_clock();
}

#if USE_PARTITION_MANAGER \
	&& defined(CONFIG_ARM_TRUSTZONE_M) \
	&& defined(PM_SRAM_NONSECURE_NAME)
void nrf_cleanup_ns_ram(void)
{
	memset((void *) PM_SRAM_NONSECURE_ADDRESS, 0, PM_SRAM_NONSECURE_SIZE);
}
#endif
