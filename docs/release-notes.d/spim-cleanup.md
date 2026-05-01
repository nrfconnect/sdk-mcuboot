 - Added SPIM peripheral cleanup in nrf_cleanup to prevent RAMACCERR when
   TF-M reconfigures SPU SRAM permissions after MCUboot uses SPI flash.
