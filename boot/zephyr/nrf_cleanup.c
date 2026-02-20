/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#if defined(CONFIG_NRFX_CLOCK)
#include <hal/nrf_clock.h>
#endif
#include <hal/nrf_uarte.h>
#include <haly/nrfy_uarte.h>
#include <haly/nrfy_gpio.h>
#if defined(NRF_RTC0) || defined(NRF_RTC1) || defined(NRF_RTC2)
    #include <hal/nrf_rtc.h>
#endif
#if defined(CONFIG_NRF_GRTC_TIMER)
    #include <nrfx_grtc.h>
#endif
#if defined(NRF_PPI)
    #include <hal/nrf_ppi.h>
#endif
#if defined(NRF_DPPIC)
    #include <hal/nrf_dppi.h>
#endif
#if defined(CONFIG_MSPI_NRF_SQSPI)
    #include <hal/nrf_vpr.h>
    #include <softperipheral_regif.h>
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

#if defined(CONFIG_NRF_GRTC_TIMER) && !defined(CONFIG_SYSTEM_TIMER_HAS_DISABLE_SUPPORT)

/**
 * This function is temporary and should be removed once nrfx_grtc_uninit
 * no longer resets the counter - see NRFX-8487.
 */
static inline void nrfx_grtc_uninit_no_counter_reset(void)
{
    if (nrfx_grtc_init_check() == false) {
        /* GRTC not initialized, nothing to do */
        return;
    }

    uint32_t ch_mask = NRFX_GRTC_CONFIG_ALLOWED_CC_CHANNELS_MASK;

#if NRF_GRTC_HAS_RTCOUNTER
    uint32_t grtc_all_int_mask = (NRFX_GRTC_CONFIG_ALLOWED_CC_CHANNELS_MASK |
                                  GRTC_NON_SYSCOMPARE_INT_MASK);
#else
    uint32_t grtc_all_int_mask = NRFX_GRTC_CONFIG_ALLOWED_CC_CHANNELS_MASK;
#endif

    nrfy_grtc_int_disable(NRF_GRTC, grtc_all_int_mask);

    for (uint8_t chan = 0; ch_mask; chan++, ch_mask >>= 1)
    {
        nrfx_grtc_syscounter_cc_disable(chan);
        nrfx_grtc_channel_free(chan);
    }
    nrfy_grtc_int_uninit(NRF_GRTC);
}

static inline void nrf_cleanup_grtc(void)
{
    nrfx_grtc_uninit_no_counter_reset();
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
#if defined(NRF_UARTE136)
    NRF_UARTE136,
#endif
};
#endif

#if defined(CONFIG_NRFX_CLOCK)
static void nrf_cleanup_clock(void)
{
    nrf_clock_int_disable(NRF_CLOCK, 0xFFFFFFFF);
}
#endif
#if defined(CONFIG_MSPI_NRF_SQSPI)
static void nrf_cleanup_sqspi(void)
{
    nrf_vpr_cpurun_set(NRF_VPR, false);

    // Reset VPR.
    nrf_vpr_debugif_dmcontrol_mask_set(NRF_VPR,
                                       (VPR_DEBUGIF_DMCONTROL_NDMRESET_Active
                                        << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos |
                                        VPR_DEBUGIF_DMCONTROL_DMACTIVE_Enabled
                                        << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos));
    nrf_vpr_debugif_dmcontrol_mask_set(NRF_VPR,
                                       (VPR_DEBUGIF_DMCONTROL_NDMRESET_Inactive
                                        << VPR_DEBUGIF_DMCONTROL_NDMRESET_Pos |
                                        VPR_DEBUGIF_DMCONTROL_DMACTIVE_Disabled
                                        << VPR_DEBUGIF_DMCONTROL_DMACTIVE_Pos));
}
#endif

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

#if defined(CONFIG_MSPI_NRF_SQSPI)
    nrf_cleanup_sqspi();
#endif

#if defined(CONFIG_NRF_GRTC_TIMER) && !defined(CONFIG_SYSTEM_TIMER_HAS_DISABLE_SUPPORT)
    nrf_cleanup_grtc();
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

#ifndef CONFIG_SOC_SERIES_NRF54L
        /* Disconnect pins UARTE pins
         * causes issues on nRF54l SoCs,
         * could be enabled once fix to NCSDK-33039 will be implemented.
         */

        uint32_t pin[4];

        pin[0] = nrfy_uarte_tx_pin_get(current);
        pin[1] = nrfy_uarte_rx_pin_get(current);
        pin[2] = nrfy_uarte_rts_pin_get(current);
        pin[3] = nrfy_uarte_cts_pin_get(current);

        nrfy_uarte_pins_disconnect(current);

        for (int j = 0; j < 4; j++) {
            if (pin[j] != NRF_UARTE_PSEL_DISCONNECTED) {
                nrfy_gpio_cfg_default(pin[j]);
            }
        }
#endif

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

#if defined(CONFIG_NRFX_CLOCK)
    nrf_cleanup_clock();
#endif
}
