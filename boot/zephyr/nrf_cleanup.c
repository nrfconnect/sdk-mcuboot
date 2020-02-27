/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <hal/nrf_rtc.h>
#include <hal/nrf_uarte.h>
#include <hal/nrf_clock.h>

#if CONFIG_HAS_HW_NRF_RTC0 || CONFIG_HAS_HW_NRF_RTC1 || CONFIG_HAS_HW_NRF_RTC2
static inline void nrf_cleanup_rtc(NRF_RTC_Type * rtc_reg)
{
    uint32_t mask = NRF_RTC_INT_TICK_MASK     |
                    NRF_RTC_INT_OVERFLOW_MASK |
                    NRF_RTC_INT_COMPARE0_MASK |
                    NRF_RTC_INT_COMPARE1_MASK |
                    NRF_RTC_INT_COMPARE2_MASK |
                    NRF_RTC_INT_COMPARE3_MASK;

    nrf_rtc_task_trigger(rtc_reg, NRF_RTC_TASK_STOP);
    nrf_rtc_event_disable(rtc_reg, mask);
    nrf_rtc_int_disable(rtc_reg, mask);
}
#endif

void nrf_cleanup_clock(void)
{
    //nrfx_clock_stop(NRF_CLOCK_DOMAIN_LFCLK);
    //nrfx_clock_stop(NRF_CLOCK_DOMAIN_HFCLK);
    //nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTOP);
    
#if NRF_CLOCK_HAS_HFCLK192M
    //nrfx_clock_stop(NRF_CLOCK_DOMAIN_HFCLK192M);
#endif
#if NRF_CLOCK_HAS_HFCLKAUDIO
    //nrfx_clock_stop(NRF_CLOCK_DOMAIN_HFCLKAUDIO);
#endif
    	nrf_clock_int_disable(NRF_CLOCK,
			(NRF_CLOCK_INT_HF_STARTED_MASK |
			 NRF_CLOCK_INT_LF_STARTED_MASK |
			 COND_CODE_1(CONFIG_USB_NRFX,
				(NRF_POWER_INT_USBDETECTED_MASK |
				 NRF_POWER_INT_USBREMOVED_MASK |
				 NRF_POWER_INT_USBPWRRDY_MASK),
				(0))));
}

void nrf_cleanup_peripheral(void)
{
#if CONFIG_HAS_HW_NRF_RTC0
	nrf_cleanup_rtc(NRF_RTC0);
#endif
#if CONFIG_HAS_HW_NRF_RTC1
	nrf_cleanup_rtc(NRF_RTC1);
#endif
#if CONFIG_HAS_HW_NRF_RTC2
	nrf_cleanup_rtc(NRF_RTC2);
#endif
#if CONFIG_HAS_HW_NRF_UARTE0
	nrf_uarte_disable(NRF_UARTE0);
#endif
#if CONFIG_HAS_HW_NRF_UARTE1
	nrf_uarte_disable(NRF_UARTE1);
	    nrf_uarte_int_disable(NRF_UARTE1, NRF_UARTE_INT_ENDRX_MASK |
                                            NRF_UARTE_INT_ENDTX_MASK |
                                            NRF_UARTE_INT_ERROR_MASK |
                                            NRF_UARTE_INT_RXTO_MASK  |
                                            NRF_UARTE_INT_TXSTOPPED_MASK);
#endif
nrf_cleanup_clock();
}

void nrf_cleanup_nvic(void) {
	/* Allow any pending interrupts to be recognized */
	__ISB();
	__disable_irq();
	NVIC_Type *nvic = NVIC;
	/* Disable NVIC interrupts */
	for (u8_t i = 0; i < ARRAY_SIZE(nvic->ICER); i++) {
		nvic->ICER[i] = 0xFFFFFFFF;
	}
	/* Clear pending NVIC interrupts */
	for (u8_t i = 0; i < ARRAY_SIZE(nvic->ICPR); i++) {
		nvic->ICPR[i] = 0xFFFFFFFF;
	}
}

