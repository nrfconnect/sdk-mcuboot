/*
 *  Copyright (C) 2018 Open Source Foundries Limited
 *  SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MCUBOOT_MBEDTLS_CONFIG_
#define _MCUBOOT_MBEDTLS_CONFIG_

/**
 * @file
 *
 * This is the top-level mbedTLS configuration file for MCUboot. The
 * configuration depends on the signature type, so this file just
 * pulls in the right header depending on that setting.
 */

/*
 * IMPORTANT:
 *
 * If you put any "generic" definitions in here, make sure to update
 * the simulator build.rs accordingly.
 */

/*
 * When the CC3XX_PLATFORM library is enabled we need to
 * inform the Mbed TLS library to not compile the
 * platform_zeroize function, otherwise we will get
 * a multiple definitions error.
 */
#if defined(CONFIG_NRF_CC3XX_PLATFORM)
#define MBEDTLS_PLATFORM_ZEROIZE_ALT
#endif

#if defined(CONFIG_BOOT_SIGNATURE_TYPE_RSA) || defined(CONFIG_BOOT_ENCRYPT_RSA)
#include "config-rsa.h"
#elif defined(CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256) || \
      defined(CONFIG_BOOT_ENCRYPT_EC256) || \
      defined(CONFIG_BOOT_SERIAL_ENCRYPT_EC256) || \
      (defined(CONFIG_BOOT_ENCRYPT_X25519) && !defined(CONFIG_BOOT_SIGNATURE_TYPE_ED25519))
#include "config-asn1.h"
#elif defined(CONFIG_BOOT_SIGNATURE_TYPE_ED25519)
#include "config-ed25519.h"
#else
#error "Cannot configure mbedTLS; signature type is unknown."
#endif

#endif
