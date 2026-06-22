# CMakeLists.txt for building mcuboot as a Zephyr project
#
# Copyright (c) 2017 Open Source Foundries Limited
# Copyright (c) 2023-2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

# This sample shows usage of an external module and we need to set the
# set the extra module path before calling find_package(Zephyr).
if(TEST_RUNTIME_SOURCE_HOOKS)
  set(EXTRA_ZEPHYR_MODULES "${CMAKE_SOURCE_DIR}/../../samples/runtime-source/zephyr/hooks")
endif()

# find_package(Zephyr) in order to load application boilerplate:
# http://docs.zephyrproject.org/application/application.html
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

# Path to "boot" subdirectory of repository root.
get_filename_component(BOOT_DIR ${APPLICATION_SOURCE_DIR} DIRECTORY)
# Path to top-level repository root directory.
get_filename_component(MCUBOOT_DIR ${BOOT_DIR} DIRECTORY)
# Path to tinycrypt library source subdirectory of MCUBOOT_DIR.
set(TINYCRYPT_DIR "${MCUBOOT_DIR}/ext/tinycrypt/lib")
assert_exists(TINYCRYPT_DIR)
set(TINYCRYPT_SHA512_DIR "${MCUBOOT_DIR}/ext/tinycrypt-sha512/lib")
assert_exists(TINYCRYPT_SHA512_DIR)
# Path to crypto-fiat
set(FIAT_DIR "${MCUBOOT_DIR}/ext/fiat")
assert_exists(FIAT_DIR)
# Path to mbed-tls' asn1 parser library.
if(NOT CONFIG_MBEDTLS_BUILTIN AND NOT CONFIG_BOOT_KEY_IMPORT_BYPASS_ASN)
  set(MBEDTLS_ASN1_DIR "${MCUBOOT_DIR}/ext/mbedtls-asn1")
  assert_exists(MBEDTLS_ASN1_DIR)
endif()
set(MCUBOOT_NRF_EXT_DIR "${MCUBOOT_DIR}/ext/nrf")

if(CONFIG_BOOT_USE_NRF_CC310_BL)
  if(NOT EXISTS ${ZEPHYR_NRFXLIB_MODULE_DIR})
    message(FATAL_ERROR "
    ------------------------------------------------------------------------
    No such file or directory: ${ZEPHYR_NRFXLIB_MODULE_DIR}
    The current configuration enables nRF CC310 crypto accelerator hardware
    with the `CONFIG_BOOT_USE_NRF_CC310_BL` option. Please follow
    `ext/nrf/README.md` guide to fix your setup or use tinycrypt instead of
    the HW accelerator.
    To use the tinycrypt set `CONFIG_BOOT_ECDSA_TINYCRYPT` to y.
    ------------------------------------------------------------------------")
  endif()
endif()

# Include the UUID generation code
add_subdirectory(uuid)

# Include the IronSide-based HW counters implementation
add_subdirectory_ifdef(CONFIG_NRF_MCUBOOT_IRONSIDE_COUNTERS ironside_counters)

if(CONFIG_NRF_SECURITY)
  zephyr_library_link_libraries(mbedcrypto)
endif()

zephyr_library_include_directories(
  include
  )

# Zephyr port-specific sources.
zephyr_library_sources(
  main.c
  flash_map_extended.c
  os.c
  keys.c
  )

if(CONFIG_NCS_BM)
  zephyr_library_sources(io_bm.c)
else()
  zephyr_library_sources(io.c)
endif()

if(DEFINED CONFIG_ENABLE_MGMT_PERUSER)
  zephyr_library_sources(
    boot_serial_extensions.c
    )

  zephyr_linker_sources_ifdef(
    CONFIG_ENABLE_MGMT_PERUSER
    SECTIONS include/boot_serial/boot_serial.ld
  )

  if(DEFINED CONFIG_BOOT_MGMT_CUSTOM_STORAGE_ERASE)
    zephyr_library_sources(
      boot_serial_extension_zephyr_basic.c
      )
  endif()
endif()

if(DEFINED CONFIG_BOOT_SHARE_BACKEND_RETENTION)
  zephyr_library_sources(
    shared_data.c
    )
endif()

if(DEFINED CONFIG_BOOT_KEYS_REVOCATION)
  zephyr_library_sources(
    ${BOOT_DIR}/bootutil/src/key_revocation.c
)
endif()

# Generic bootutil sources and includes.
zephyr_library_include_directories(${BOOT_DIR}/bootutil/include)
zephyr_library_sources(
  ${BOOT_DIR}/bootutil/src/image_validate.c
  ${BOOT_DIR}/bootutil/src/bootutil_find_key.c
  ${BOOT_DIR}/bootutil/src/bootutil_img_hash.c
  ${BOOT_DIR}/bootutil/src/bootutil_img_security_cnt.c
  ${BOOT_DIR}/bootutil/src/mcuboot_manifest.c
  ${BOOT_DIR}/bootutil/src/tlv.c
  ${BOOT_DIR}/bootutil/src/encrypted.c
  ${BOOT_DIR}/bootutil/src/image_rsa_nrf.c
  ${BOOT_DIR}/bootutil/src/image_ecdsa.c
  ${BOOT_DIR}/bootutil/src/image_ed25519.c
  ${BOOT_DIR}/bootutil/src/bootutil_misc.c
  ${BOOT_DIR}/bootutil/src/bootutil_area.c
  ${BOOT_DIR}/bootutil/src/bootutil_loader.c
  ${BOOT_DIR}/bootutil/src/fault_injection_hardening.c
  )

if((CONFIG_BOOT_ENCRYPT_X25519 AND CONFIG_BOOT_ED25519_PSA)
   OR (CONFIG_BOOT_ENCRYPT_EC256 AND CONFIG_BOOT_ECDSA_PSA)
   OR (CONFIG_BOOT_ENCRYPT_RSA AND CONFIG_BOOT_SIGNATURE_TYPE_RSA))
  zephyr_library_sources(${BOOT_DIR}/bootutil/src/encrypted_psa.c)
endif()

if(CONFIG_MEASURED_BOOT OR CONFIG_BOOT_SHARE_DATA)
  zephyr_library_sources(
    ${BOOT_DIR}/bootutil/src/boot_record.c
    )

  # Set a define for this file which will allow inclusion of the Zephyr version
  # include file
  set_source_files_properties(
    ${BOOT_DIR}/bootutil/src/boot_record.c
    PROPERTIES COMPILE_FLAGS -DZEPHYR_VER_INCLUDE=1
    )
endif()

# library which might be common source code for MCUBoot and an application
zephyr_link_libraries(MCUBOOT_BOOTUTIL)

if(CONFIG_BOOT_FIH_PROFILE_HIGH)
zephyr_library_sources(
  ${BOOT_DIR}/bootutil/src/fault_injection_hardening_delay_rng_mbedtls.c
  )
endif()

if(CONFIG_SINGLE_APPLICATION_SLOT_RAM_LOAD)
  zephyr_library_sources(
    ${BOOT_DIR}/zephyr/single_loader.c
    ${BOOT_DIR}/bootutil/src/ram_load.c
    )
  zephyr_library_include_directories(${BOOT_DIR}/bootutil/src)
elseif(CONFIG_SINGLE_APPLICATION_SLOT)
  zephyr_library_sources(
    ${BOOT_DIR}/zephyr/single_loader.c
    )
  zephyr_library_include_directories(${BOOT_DIR}/bootutil/src)
elseif(CONFIG_BOOT_FIRMWARE_LOADER)
  if(CONFIG_NCS_BM)
    zephyr_library_sources(
      ${BOOT_DIR}/zephyr/firmware_loader_bm.c
      )
  else()
    zephyr_library_sources(
      ${BOOT_DIR}/zephyr/firmware_loader.c
      )
  endif()
  zephyr_library_include_directories(${BOOT_DIR}/bootutil/src)
elseif(CONFIG_MCUBOOT_MANIFEST_UPDATES)
  zephyr_library_sources(
    ${BOOT_DIR}/bootutil/src/swap_misc.c
    ${BOOT_DIR}/bootutil/src/caps.c
    )

  if(CONFIG_BOOT_SWAP_USING_MOVE)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_move.c
      )
  elseif(CONFIG_BOOT_SWAP_USING_OFFSET)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_offset.c
      )
  elseif(CONFIG_BOOT_DIRECT_XIP)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/loader_manifest_xip.c
      )
  else()
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_scratch.c
      )
  endif()
else()
  zephyr_library_sources(
    ${BOOT_DIR}/bootutil/src/loader.c
    ${BOOT_DIR}/bootutil/src/swap_misc.c
    ${BOOT_DIR}/bootutil/src/caps.c
    )

  if(CONFIG_BOOT_SWAP_USING_MOVE)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_move.c
      )
  elseif(CONFIG_BOOT_SWAP_USING_OFFSET)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_offset.c
      )
  else()
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_scratch.c
      )

    if(CONFIG_BOOT_RAM_LOAD)
      zephyr_library_sources(
        ${BOOT_DIR}/bootutil/src/ram_load.c
        )
    endif()
  endif()

  if(NOT CONFIG_MCUBOOT_MCUBOOT_IMAGE_NUMBER EQUAL "-1" AND NOT CONFIG_BOOT_UPGRADE_ONLY)
    zephyr_library_sources(
      ${BOOT_DIR}/bootutil/src/swap_nsib.c
    )
  endif()
endif()

if(CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256 OR CONFIG_BOOT_ENCRYPT_EC256)
  # When ECDSA PSA is used, do not pull in additional ASN.1 include
  # directories or sources, as it would cause incorrect header files
  # to be included.
  if(MBEDTLS_ASN1_DIR AND NOT CONFIG_NRF_SECURITY)
    zephyr_library_include_directories(
      ${MBEDTLS_ASN1_DIR}/include
    )
    zephyr_library_sources(
      # Additionally pull in just the ASN.1 parser from mbedTLS.
      ${MBEDTLS_ASN1_DIR}/src/asn1parse.c
      ${MBEDTLS_ASN1_DIR}/src/platform_util.c
    )
  endif()
  if(CONFIG_BOOT_USE_TINYCRYPT)
    # When using ECDSA signatures, pull in our copy of the tinycrypt library.
    zephyr_library_include_directories(
      ${BOOT_DIR}/zephyr/include
      ${TINYCRYPT_DIR}/include
    )
    zephyr_include_directories(${TINYCRYPT_DIR}/include)

    zephyr_library_sources(
      ${TINYCRYPT_DIR}/source/ecc.c
      ${TINYCRYPT_DIR}/source/ecc_dsa.c
      ${TINYCRYPT_DIR}/source/sha256.c
      ${TINYCRYPT_DIR}/source/utils.c
    )
  elseif(CONFIG_BOOT_USE_NRF_CC310_BL)
    zephyr_library_sources(${MCUBOOT_NRF_EXT_DIR}/cc310_glue.c)
    zephyr_library_include_directories(${MCUBOOT_NRF_EXT_DIR})
    zephyr_link_libraries(nrfxlib_crypto)
  elseif(CONFIG_BOOT_USE_NRF_EXTERNAL_CRYPTO)
    zephyr_include_directories(${BL_CRYPTO_DIR}/../include)
  endif()

  if(CONFIG_MBEDTLS_CONFIG_FILE)
    # Since here we are not using Zephyr's mbedTLS but rather our own, we need
    # to set MBEDTLS_CONFIG_FILE ourselves. When using Zephyr's copy, this
    # variable is set by its Kconfig in the Zephyr codebase.
    zephyr_library_compile_definitions(
      MBEDTLS_CONFIG_FILE="${CONFIG_MBEDTLS_CONFIG_FILE}"
    )
  endif()
elseif(CONFIG_BOOT_SIGNATURE_TYPE_NONE)
  zephyr_library_include_directories(
    ${BOOT_DIR}/zephyr/include
    ${TINYCRYPT_DIR}/include
    )

  zephyr_library_sources(
    ${TINYCRYPT_DIR}/source/sha256.c
    ${TINYCRYPT_DIR}/source/utils.c
    )
elseif(CONFIG_BOOT_SIGNATURE_TYPE_RSA)
  # Use mbedTLS provided by Zephyr for RSA signatures. (Its config file
  # is set using Kconfig.)
  zephyr_include_directories(include)
  if(CONFIG_BOOT_ENCRYPT_RSA)
    set_source_files_properties(
      ${BOOT_DIR}/bootutil/src/encrypted.c
      PROPERTIES
      INCLUDE_DIRECTORIES ${ZEPHYR_MBEDTLS_MODULE_DIR}/library
      )
  endif()
elseif(CONFIG_BOOT_SIGNATURE_TYPE_ED25519 OR CONFIG_BOOT_ENCRYPT_X25519)
  if(CONFIG_BOOT_USE_TINYCRYPT)
    if(MBEDTLS_ASN1_DIR)
      zephyr_library_include_directories(
        ${MBEDTLS_ASN1_DIR}/include
      )
      zephyr_library_sources(
        # Additionally pull in just the ASN.1 parser from mbedTLS.
        ${MBEDTLS_ASN1_DIR}/src/asn1parse.c
        ${MBEDTLS_ASN1_DIR}/src/platform_util.c
      )
    endif()
    zephyr_library_include_directories(
      ${BOOT_DIR}/zephyr/include
      ${TINYCRYPT_DIR}/include
      ${TINYCRYPT_SHA512_DIR}/include
      )
    zephyr_library_sources(
      ${TINYCRYPT_DIR}/source/sha256.c
      ${TINYCRYPT_DIR}/source/utils.c
      ${TINYCRYPT_SHA512_DIR}/source/sha512.c
      )
    zephyr_library_compile_definitions(
      MBEDTLS_CONFIG_FILE="${CONFIG_MBEDTLS_CONFIG_FILE}"
      )
  else()
    zephyr_include_directories(include)
  endif()

  zephyr_library_include_directories(
    ${BOOT_DIR}/zephyr/include
    ${FIAT_DIR}/include/
  )

  if(NOT CONFIG_BOOT_ED25519_PSA)
    zephyr_library_sources(
      ${FIAT_DIR}/src/curve25519.c
    )
  else()
    if(MBEDTLS_ASN1_DIR)
      zephyr_library_sources(
        ${ZEPHYR_OBERON_PSA_CRYPTO_MODULE_DIR}/utilities/asn1parse.c
      )
    endif()
    if(NOT CONFIG_BOOT_SIGNATURE_USING_KMU AND NOT CONFIG_NCS_BOOT_SIGNATURE_USING_ITS)
      zephyr_library_sources(
        ${BOOT_DIR}/bootutil/src/ed25519_psa.c
      )
    else()
      zephyr_library_sources(
        ${BOOT_DIR}/bootutil/src/ed25519_psa_kmu_its.c
      )
    endif()
  endif()
endif()

if(NOT CONFIG_BOOT_ED25519_PSA AND NOT CONFIG_BOOT_ECDSA_PSA)
  if(CONFIG_BOOT_ENCRYPT_EC256 OR CONFIG_BOOT_ENCRYPT_X25519)
    zephyr_library_sources(
      ${TINYCRYPT_DIR}/source/aes_encrypt.c
      ${TINYCRYPT_DIR}/source/aes_decrypt.c
      ${TINYCRYPT_DIR}/source/ctr_mode.c
      ${TINYCRYPT_DIR}/source/hmac.c
      ${TINYCRYPT_DIR}/source/ecc_dh.c
    )
  endif()
endif()

if(CONFIG_BOOT_ENCRYPT_EC256 AND NOT CONFIG_BOOT_ECDSA_PSA)
  zephyr_library_sources(
    ${TINYCRYPT_DIR}/source/ecc_dh.c
    )
endif()

if(CONFIG_BOOT_DECOMPRESSION)
  zephyr_library_sources(
    decompression.c
  )
endif()

if(CONFIG_MCUBOOT_SERIAL)
  zephyr_sources(${BOOT_DIR}/zephyr/serial_adapter.c)
  zephyr_sources(${BOOT_DIR}/boot_serial/src/boot_serial.c)
  zephyr_sources(${BOOT_DIR}/boot_serial/src/zcbor_bulk.c)

  zephyr_include_directories(${BOOT_DIR}/bootutil/include)
  zephyr_include_directories(${BOOT_DIR}/boot_serial/include)
  zephyr_include_directories(include)

  zephyr_include_directories_ifdef(
    CONFIG_BOOT_ERASE_PROGRESSIVELY
    ${BOOT_DIR}/bootutil/src
    )

  if(CONFIG_BOOT_SERIAL_CDC_ACM AND CONFIG_USB_DEVICE_STACK_NEXT)
    zephyr_sources(${BOOT_DIR}/zephyr/boot_serial_cdc_acm_usb_next.c)
  endif()

  if(CONFIG_BOOT_ENCRYPT_IMAGE)
    zephyr_library_sources(
      ${BOOT_DIR}/boot_serial/src/boot_serial_encryption.c
    )
  endif()
endif()

if(NOT CONFIG_BOOT_SIGNATURE_USING_KMU AND NOT CONFIG_BOOT_SIGNATURE_KEY_FILE STREQUAL "")
  # CONF_FILE points to the KConfig configuration files of the bootloader.
  foreach (filepath ${CONF_FILE})
    file(READ ${filepath} temp_text)
    string(FIND "${temp_text}" ${CONFIG_BOOT_SIGNATURE_KEY_FILE} match)
    if (${match} GREATER_EQUAL 0)
      if (NOT DEFINED CONF_DIR)
        get_filename_component(CONF_DIR ${filepath} DIRECTORY)
      else()
        message(FATAL_ERROR "Signature key file defined in multiple conf files")
      endif()
    endif()
  endforeach()

  if(IS_ABSOLUTE ${CONFIG_BOOT_SIGNATURE_KEY_FILE})
    set(KEY_FILE ${CONFIG_BOOT_SIGNATURE_KEY_FILE})
  elseif((DEFINED CONF_DIR) AND
	 (EXISTS ${CONF_DIR}/${CONFIG_BOOT_SIGNATURE_KEY_FILE}))
    set(KEY_FILE ${CONF_DIR}/${CONFIG_BOOT_SIGNATURE_KEY_FILE})
  else()
    set(KEY_FILE ${MCUBOOT_DIR}/${CONFIG_BOOT_SIGNATURE_KEY_FILE})
  endif()
  message("MCUBoot bootloader key file: ${KEY_FILE}")

  set_property(
	GLOBAL
	PROPERTY
	KEY_FILE
	${KEY_FILE}
	)

  set(mcuboot_default_signature_files
      ${MCUBOOT_DIR}/root-ec-p256-pkcs8.pem
      ${MCUBOOT_DIR}/root-ec-p384.pem
      ${MCUBOOT_DIR}/root-ec-p384-pkcs8.pem
      ${MCUBOOT_DIR}/root-ed25519.pem
      ${MCUBOOT_DIR}/root-rsa-2048.pem
      ${MCUBOOT_DIR}/root-rsa-3072.pem
      ${MCUBOOT_DIR}/root-ec-p256.pem
  )

  # Emit a warning if using one of the default MCUboot key files
  if(${KEY_FILE} IN_LIST mcuboot_default_signature_files)
    message(WARNING "WARNING: Using default MCUboot signing key file, this file is for debug use only and is not secure!")
  endif()

  set(GENERATED_PUBKEY ${ZEPHYR_BINARY_DIR}/autogen-pubkey.c)
  add_custom_command(
    OUTPUT ${GENERATED_PUBKEY}
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${MCUBOOT_DIR}/scripts/imgtool.py
    getpub
    -k
    ${KEY_FILE}
    > ${GENERATED_PUBKEY}
    DEPENDS ${KEY_FILE}
    )
  zephyr_library_sources(${GENERATED_PUBKEY})
endif()

if(CONFIG_BOOT_ENCRYPTION_KEY_FILE AND NOT CONFIG_BOOT_ENCRYPTION_KEY_FILE STREQUAL "")
  # CONF_FILE points to the KConfig configuration files of the bootloader.
  unset(CONF_DIR)
  foreach(filepath ${CONF_FILE})
    file(READ ${filepath} temp_text)
    string(FIND "${temp_text}" ${CONFIG_BOOT_ENCRYPTION_KEY_FILE} match)
    if(${match} GREATER_EQUAL 0)
      if(NOT DEFINED CONF_DIR)
        get_filename_component(CONF_DIR ${filepath} DIRECTORY)
      else()
        message(FATAL_ERROR "Encryption key file defined in multiple conf files")
      endif()
    endif()
  endforeach()

  if(IS_ABSOLUTE ${CONFIG_BOOT_ENCRYPTION_KEY_FILE})
    set(KEY_FILE ${CONFIG_BOOT_ENCRYPTION_KEY_FILE})
  elseif((DEFINED CONF_DIR) AND
	 (EXISTS ${CONF_DIR}/${CONFIG_BOOT_ENCRYPTION_KEY_FILE}))
    set(KEY_FILE ${CONF_DIR}/${CONFIG_BOOT_ENCRYPTION_KEY_FILE})
  else()
    set(KEY_FILE ${MCUBOOT_DIR}/${CONFIG_BOOT_ENCRYPTION_KEY_FILE})
  endif()
  message("MCUBoot bootloader encryption key file: ${KEY_FILE}")

  # Emit a warning if using one of the default MCUboot key files
  set(mcuboot_default_encryption_files
      ${MCUBOOT_DIR}/enc-ec256-priv.pem
      ${MCUBOOT_DIR}/enc-ec256-pub.pem
      ${MCUBOOT_DIR}/enc-rsa2048-priv.pem
      ${MCUBOOT_DIR}/enc-rsa2048-pub.pem
      ${MCUBOOT_DIR}/enc-x25519-priv.pem
      ${MCUBOOT_DIR}/enc-x25519-pub.pem
  )

  if(${KEY_FILE} IN_LIST mcuboot_default_encryption_files)
    message(WARNING "WARNING: Using default MCUboot encryption key file, this file is for debug use only and is not secure!")
  endif()

  set(GENERATED_ENCKEY ${ZEPHYR_BINARY_DIR}/autogen-enckey.c)
  add_custom_command(
    OUTPUT ${GENERATED_ENCKEY}
    COMMAND
    ${PYTHON_EXECUTABLE}
    ${MCUBOOT_DIR}/scripts/imgtool.py
    getpriv
    -k
    ${KEY_FILE}
    > ${GENERATED_ENCKEY}
    DEPENDS ${KEY_FILE}
    )
  zephyr_library_sources(${GENERATED_ENCKEY})
endif()

if(CONFIG_MCUBOOT_CLEANUP_ARM_CORE)
  # ARM Cortex-M
  zephyr_library_sources_ifdef(
    CONFIG_CPU_CORTEX_M
    ${BOOT_DIR}/zephyr/cleanup/arm_cortex_m.c
  )

  # ARM Cortex-R
  zephyr_library_sources_ifdef(
    CONFIG_ARMV7_R
    ${BOOT_DIR}/zephyr/cleanup/arm_cortex_r.c
  )
endif()

if(CONFIG_SOC_EARLY_RESET_HOOK)
  zephyr_library_sources(${BOOT_DIR}/zephyr/nrf54h20_custom_s2ram.S)
  zephyr_library_sources(${BOOT_DIR}/zephyr/nrf54h20_custom_s2ram.c)
endif()

if(CONFIG_MCUBOOT_BOOT_BANNER)
  # Replace Zephyr's boot banner with the MCUboot one
  zephyr_sources(kernel/banner.c)
endif()

function(align_up num align result)
  math(EXPR out "(((${num}) + ((${align}) - 1)) & ~((${align}) - 1))")
  set(${result} "${out}" PARENT_SCOPE)
endfunction()

# Takes a node path to a partition and goes up until a parent with the soc-nv-flash compatible
# is found, then returns the path for that device
function(dt_get_nvm_device node nvm_device)
  string(FIND "${node}" "/" node_first_slash)
  string(FIND "${node}" "/" node_final_slash REVERSE)
  string(SUBSTRING "${node}" 0 ${node_final_slash} current_path)

  while(${node_final_slash} GREATER ${node_first_slash})
    set(current_compatible)
    dt_prop(current_compatible PATH "${current_path}" PROPERTY "compatible")

    if("soc-nv-flash" IN_LIST current_compatible)
      set(${nvm_device} ${current_path} PARENT_SCOPE)
      break()
    elseif("fixed-partitions" IN_LIST current_compatible)
      string(FIND "${current_path}" "/" node_final_slash REVERSE)
      string(SUBSTRING "${current_path}" 0 ${node_final_slash} current_path)
      set(${nvm_device} ${current_path} PARENT_SCOPE)
      break()
    endif()

    string(FIND "${current_path}" "/" node_final_slash REVERSE)
    string(SUBSTRING "${current_path}" 0 ${node_final_slash} current_path)
  endwhile()
endfunction()

# Takes a node path to a partition and goes up until a parent with the soc-nv-flash compatible
# is found, then reads the erase and write block sizes from it
function(dt_get_erase_write_block_sizes node erase_block_size write_block_size)
  dt_get_nvm_device(${node} nvm_path)
  if(DEFINED nvm_path)
    dt_prop(erase_size PATH "${nvm_path}" PROPERTY "erase-block-size")
    dt_prop(write_size PATH "${nvm_path}" PROPERTY "write-block-size")
    set(${erase_block_size} ${erase_size} PARENT_SCOPE)
    set(${write_block_size} ${write_size} PARENT_SCOPE)
  endif()
endfunction()

# Calculate erase/write sizes and provide definitions for them as well as calculating the maximum
# sectors (if the feature is enabled)
set(image 0)
set(auto_min_sectors 0)

while(${image} LESS ${CONFIG_UPDATEABLE_IMAGE_NUMBER})
  set(secondary_slot_flash)
  set(secondary_slot_size)
  set(erase_size_secondary_slot)
  set(write_size_secondary_slot)
  math(EXPR primary_slot "${image} * 2")

  dt_nodelabel(primary_slot_flash NODELABEL "slot${primary_slot}_partition")
  if(NOT DEFINED primary_slot_flash)
    message(STATUS "slot${primary_slot}_partition not found. Skip reading erase/write block sizes.")
    math(EXPR image "${image} + 1")
    continue()
  endif()

  dt_get_erase_write_block_sizes(${primary_slot_flash} erase_size_primary_slot
    write_size_primary_slot)

  if(CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET)
    if(DEFINED erase_size_primary_slot)
      if(${image} EQUAL 0)
        set(erase_size_slot0 ${erase_size_primary_slot})
      endif()
      zephyr_compile_definitions(
        "MCUBOOT_SLOT${primary_slot}_EXPECTED_ERASE_SIZE=${erase_size_primary_slot}")
    endif()

    if(DEFINED write_size_primary_slot)
      if(${image} EQUAL 0)
        set(write_size_slot0 ${write_size_primary_slot})
      endif()
      zephyr_compile_definitions(
        "MCUBOOT_SLOT${primary_slot}_EXPECTED_WRITE_SIZE=${write_size_primary_slot}")
    endif()
  endif()

  if(CONFIG_BOOT_MAX_IMG_SECTORS_AUTO)
    dt_prop(primary_slot_size PATH "${primary_slot_flash}" PROPERTY "reg" INDEX 1)
    if(DEFINED primary_slot_size AND DEFINED erase_size_primary_slot)
      math(EXPR remainder "${primary_slot_size} % ${erase_size_primary_slot}")
      if(NOT remainder EQUAL 0)
        message(FATAL_ERROR "slot${primary_slot}_partition size (${primary_slot_size}) is not "
          "divisible by erase block size (${erase_size_primary_slot})"
        )
      endif()
      math(EXPR slot_min_sectors "${primary_slot_size} / ${erase_size_primary_slot}")

      if(${slot_min_sectors} GREATER ${auto_min_sectors})
        set(auto_min_sectors ${slot_min_sectors})
      endif()
    else()
      message(WARNING "Unable to determine erase size/total size of slot${primary_slot} partition")
    endif()
  endif()

  if(NOT CONFIG_SINGLE_APPLICATION_SLOT AND NOT CONFIG_SINGLE_APPLICATION_SLOT_RAM_LOAD)
    math(EXPR secondary_slot "${primary_slot} + 1")

    dt_nodelabel(secondary_slot_flash NODELABEL "slot${secondary_slot}_partition")
    if(NOT DEFINED secondary_slot_flash)
      message(STATUS
        "slot${secondary_slot}_partition not found. Skip reading erase/write block sizes.")
      math(EXPR image "${image} + 1")
      continue()
    endif()

    dt_get_erase_write_block_sizes(${secondary_slot_flash} erase_size_secondary_slot write_size_secondary_slot)

    if(CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET)
      if(DEFINED erase_size_secondary_slot)
        if(${image} EQUAL 0)
          set(erase_size_slot1 ${erase_size_secondary_slot})
        endif()
        zephyr_compile_definitions(
          "MCUBOOT_SLOT${secondary_slot}_EXPECTED_ERASE_SIZE=${erase_size_secondary_slot}")
      endif()

      if(DEFINED write_size_secondary_slot)
        if(${image} EQUAL 0)
          set(write_size_slot1 ${write_size_secondary_slot})
        endif()
        zephyr_compile_definitions(
          "MCUBOOT_SLOT${secondary_slot}_EXPECTED_WRITE_SIZE=${write_size_secondary_slot}")
      endif()
    endif()

    if(CONFIG_BOOT_MAX_IMG_SECTORS_AUTO)
      dt_prop(secondary_slot_size PATH "${secondary_slot_flash}" PROPERTY "reg" INDEX 1)
      if(DEFINED secondary_slot_size AND DEFINED erase_size_secondary_slot)
        math(EXPR remainder "${secondary_slot_size} % ${erase_size_secondary_slot}")
        if(NOT remainder EQUAL 0)
          message(FATAL_ERROR "slot${secondary_slot}_partition size (${secondary_slot_size}) is not "
            "divisible by erase block size (${erase_size_secondary_slot})"
          )
        endif()
        math(EXPR slot_min_sectors "${secondary_slot_size} / ${erase_size_secondary_slot}")

        if(${slot_min_sectors} GREATER ${auto_min_sectors})
          set(auto_min_sectors ${slot_min_sectors})
        endif()
      else()
        message(WARNING
          "Unable to determine erase size/total size of slot${secondary_slot} partition")
      endif()
    endif()
  endif()

  math(EXPR image "${image} + 1")
endwhile()

if(CONFIG_BOOT_MAX_IMG_SECTORS_AUTO)
  if("${auto_min_sectors}" GREATER "0")
    zephyr_compile_definitions("MIN_SECTOR_COUNT=${auto_min_sectors}")
    message("Calculated maximum number of sectors: ${auto_min_sectors}")
  else()
    message(WARNING "Unable to calculate minimum number of sector sizes, falling back to 128 sector default. Please disable CONFIG_BOOT_MAX_IMG_SECTORS_AUTO and set CONFIG_BOOT_MAX_IMG_SECTORS to the required value")
  endif()
endif()

if((CONFIG_BOOT_SWAP_USING_SCRATCH OR CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET) AND (DEFINED write_size_slot0 OR DEFINED write_size_slot1))
  zephyr_library_sources(flash_check.c)
endif()

if(CONFIG_BOOT_RAM_LOAD)
  zephyr_library_sources(ram_load.c)
endif()

if(SYSBUILD)
  if(CONFIG_SINGLE_APPLICATION_SLOT OR CONFIG_BOOT_FIRMWARE_LOADER OR CONFIG_BOOT_SWAP_USING_SCRATCH OR CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET OR CONFIG_BOOT_UPGRADE_ONLY OR CONFIG_BOOT_DIRECT_XIP OR CONFIG_BOOT_RAM_LOAD)
    # TODO: RAM LOAD support
    dt_nodelabel(slot0_flash NODELABEL "slot0_partition" REQUIRED)
    dt_get_nvm_device(${slot0_flash} slot0_device)

    if(NOT CONFIG_SINGLE_APPLICATION_SLOT)
      dt_nodelabel(slot1_flash NODELABEL "slot1_partition" REQUIRED)
      dt_get_nvm_device(${slot1_flash} slot1_device)

      if(NOT "${slot0_device}" STREQUAL "${slot1_device}")
        # Check both slots for the one with the largest write/erase block size
        dt_prop(erase_size_slot0 PATH "${slot0_device}" PROPERTY "erase-block-size")
        dt_prop(write_size_slot0 PATH "${slot0_device}" PROPERTY "write-block-size")
        dt_prop(erase_size_slot1 PATH "${slot1_device}" PROPERTY "erase-block-size")
        dt_prop(write_size_slot1 PATH "${slot1_device}" PROPERTY "write-block-size")

        if(DEFINED erase_size_slot0 AND DEFINED erase_size_slot1)
          if(${erase_size_slot0} GREATER ${erase_size_slot1})
            set(erase_size ${erase_size_slot0})
          else()
            set(erase_size ${erase_size_slot1})
          endif()
        elseif(DEFINED erase_size_slot0)
          set(erase_size ${erase_size_slot0})
        elseif(DEFINED erase_size_slot1)
          set(erase_size ${erase_size_slot1})
        endif()

        if(DEFINED write_size_slot0 AND DEFINED write_size_slot1)
          if(${write_size_slot0} GREATER ${write_size_slot1})
            set(write_size ${write_size_slot0})
          else()
            set(write_size ${write_size_slot1})
          endif()
        elseif(DEFINED write_size_slot0)
          set(write_size ${write_size_slot0})
        elseif(DEFINED write_size_slot1)
          set(write_size ${write_size_slot1})
        endif()
      else()
        dt_prop(erase_size PATH "${slot0_device}" PROPERTY "erase-block-size")
        dt_prop(write_size PATH "${slot0_device}" PROPERTY "write-block-size")
      endif()
    else()
      dt_prop(erase_size PATH "${slot0_device}" PROPERTY "erase-block-size")
      dt_prop(write_size PATH "${slot0_device}" PROPERTY "write-block-size")
    endif()

    if(NOT DEFINED erase_size)
      message(WARNING "Unable to determine erase size of slot0 or slot1 partition, setting to 1 (this is probably wrong)")
      set(erase_size 1)
    endif()

    if(NOT DEFINED write_size)
      message(WARNING "Unable to determine write size of slot0 or slot1 partition, setting to 8 (this is probably wrong)")
      set(write_size 8)
    endif()

    if(${write_size} LESS 8)
      set(max_align_size 8)
    else()
      set(max_align_size ${write_size})
    endif()

    set(key_size 0)

    # Boot trailer magic size
    set(boot_magic_size 16)

    # Estimates for trailer TLV data size, this was taken from hello world builds for nrf52840dk
    if(CONFIG_BOOT_SIGNATURE_TYPE_RSA)
      if(CONFIG_BOOT_SIGNATURE_TYPE_RSA_LEN EQUAL 3072)
        set(boot_tlv_estimate 464)
      else()
        set(boot_tlv_estimate 336)
      endif()
    elseif(CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256)
      set(boot_tlv_estimate 150)
    elseif(CONFIG_BOOT_SIGNATURE_TYPE_ED25519)
      set(boot_tlv_estimate 144)
    else()
      set(boot_tlv_estimate 40)
    endif()

    if(CONFIG_BOOT_ENCRYPT_RSA OR CONFIG_BOOT_ENCRYPT_EC256 OR CONFIG_BOOT_ENCRYPT_X25519)
      # 128-bit AES key size
      set(boot_enc_key_size 16)

      if(CONFIG_BOOT_SWAP_SAVE_ENCTLV)
        if(CONFIG_BOOT_ENCRYPT_RSA)
          set(key_size 256)
        elseif(CONFIG_BOOT_ENCRYPT_EC256)
          math(EXPR key_size "65 + 32 + ${boot_enc_key_size}")
        elseif(CONFIG_BOOT_ENCRYPT_X25519)
          math(EXPR key_size "32 + 32 + ${boot_enc_key_size}")
        endif()
      else()
        set(key_size "${boot_enc_key_size}")
      endif()

      align_up(${key_size} ${max_align_size} key_size)
      math(EXPR key_size "${key_size} * 2")
    endif()

    align_up(${boot_magic_size} ${write_size} boot_magic_size)

    if(CONFIG_SINGLE_APPLICATION_SLOT OR CONFIG_BOOT_FIRMWARE_LOADER)
      set(boot_swap_data_size 0)
    else()
      math(EXPR boot_swap_data_size "${max_align_size} * 4")
    endif()

    if(CONFIG_BOOT_SWAP_USING_SCRATCH OR CONFIG_BOOT_SWAP_USING_MOVE)
      if(CONFIG_BOOT_MAX_IMG_SECTORS_AUTO AND DEFINED auto_min_sectors AND "${auto_min_sectors}" GREATER "0")
        math(EXPR boot_status_data_size "${auto_min_sectors} * (3 * ${write_size})")
      else()
        if(CONFIG_BOOT_MAX_IMG_SECTORS)
          math(EXPR boot_status_data_size "${CONFIG_BOOT_MAX_IMG_SECTORS} * (3 * ${write_size})")
        else()
          message(WARNING "CONFIG_BOOT_MAX_IMG_SECTORS is not defined, falling back to 128 sector default. Please set CONFIG_BOOT_MAX_IMG_SECTORS to the required value")
          math(EXPR boot_status_data_size "128 * (3 * ${write_size})")
        endif()
      endif()
    elseif(CONFIG_BOOT_SWAP_USING_OFFSET)
      if(CONFIG_BOOT_MAX_IMG_SECTORS_AUTO AND DEFINED auto_min_sectors AND "${auto_min_sectors}" GREATER "0")
        math(EXPR boot_status_data_size "${auto_min_sectors} * (2 * ${write_size})")
      else()
        if(CONFIG_BOOT_MAX_IMG_SECTORS)
          math(EXPR boot_status_data_size "${CONFIG_BOOT_MAX_IMG_SECTORS} * (2 * ${write_size})")
        else()
          message(WARNING "CONFIG_BOOT_MAX_IMG_SECTORS is not defined, falling back to 128 sector default. Please set CONFIG_BOOT_MAX_IMG_SECTORS to the required value")
          math(EXPR boot_status_data_size "128 * (2 * ${write_size})")
        endif()
      endif()
    else()
      set(boot_status_data_size 0)
    endif()

    math(EXPR trailer_size "${key_size} + ${boot_magic_size} + ${boot_swap_data_size} + ${boot_status_data_size}")

    if(CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET)
      align_up(${trailer_size} ${erase_size} trailer_size)
    endif()

    math(EXPR required_size "${trailer_size} + ${boot_tlv_estimate}")

    if(CONFIG_SINGLE_APPLICATION_SLOT OR CONFIG_BOOT_FIRMWARE_LOADER)
      set(required_upgrade_size "0")
    else()
      math(EXPR required_upgrade_size "${boot_magic_size} + ${boot_swap_data_size} + ${boot_status_data_size}")

      if(CONFIG_BOOT_SWAP_USING_MOVE OR CONFIG_BOOT_SWAP_USING_OFFSET)
        align_up(${required_upgrade_size} ${erase_size} required_upgrade_size)
      endif()
    endif()

    if(CONFIG_BOOT_SWAP_USING_MOVE)
      math(EXPR required_size "${required_size} + ${erase_size}")
      math(EXPR required_upgrade_size "${required_upgrade_size} + ${erase_size}")
    elseif(CONFIG_BOOT_SWAP_USING_OFFSET)
#todo: check how different slot sizes are...
#      math(EXPR required_size "${required_size} + ${erase_size}")
#      math(EXPR required_upgrade_size "${required_upgrade_size} + ${erase_size}")
    endif()
  else()
    set(required_size 0)
    set(required_upgrade_size 0)
  endif()

  set(mcuboot_image_footer_size ${required_size} CACHE INTERNAL "Estimated MCUboot image trailer size" FORCE)
  set(mcuboot_image_upgrade_footer_size ${required_upgrade_size} CACHE INTERNAL "Estimated MCUboot update image trailer size" FORCE)
endif()

if(CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL)
  zephyr_library_sources(
    ${BOOT_DIR}/zephyr/nrf_cleanup.c
  )

  if(CONFIG_MSPI_NRF_SQSPI)
    zephyr_library_include_directories(
      ${ZEPHYR_NRFXLIB_MODULE_DIR}/softperipheral/include
    )
  endif()
endif()

if(CONFIG_NCS_MCUBOOT_LOAD_PERIPHCONF)
  zephyr_sources(${BOOT_DIR}/zephyr/load_ironside_se_conf.c)
endif()
if(CONFIG_NCS_MCUBOOT_MPCCONF_STATIC_WRITE_PROTECTION)
  zephyr_sources(${BOOT_DIR}/zephyr/load_ironside_se_conf_mpc.c)
endif()


if(SYSBUILD AND CONFIG_PCD_APP)
    # Sysbuild requires details of the RAM flash device are stored to the cache of MCUboot so
    # that they can be read when running partition manager
    dt_nodelabel(ram_flash_dev NODELABEL flash_sim0)
    dt_reg_addr(ram_flash_addr PATH ${ram_flash_dev})
    dt_reg_size(ram_flash_size PATH ${ram_flash_dev})

    set(RAM_FLASH_ADDR "${ram_flash_addr}" CACHE STRING "" FORCE)
    set(RAM_FLASH_SIZE "${ram_flash_size}" CACHE STRING "" FORCE)
endif()

if(NOT CONFIG_PARTITION_MANAGER_ENABLED AND CONFIG_SOC_NRF5340_CPUAPP)
  if(CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER AND NOT CONFIG_MCUBOOT_NETWORK_CORE_IMAGE_NUMBER EQUAL -1)
    if(NOT TARGET b0n_target)
      add_custom_target(b0n_target)
      zephyr_dt_import(EDT_PICKLE_FILE ${CMAKE_BINARY_DIR}/../b0n/zephyr/edt.pickle TARGET b0n_target)
    endif()
    # Really this dependency if set for MCUboot, but it will also cover s1 image,
    # since contition that let us here checks for `CONFIG_MCUBOOT` which is only
    # set for MCUboot builds.
    add_dependencies(app b0n_target)

    dt_nodelabel(s0_partition_path NODELABEL s0_partition REQUIRED TARGET b0n_target)
    dt_reg_addr(s0_partition_address PATH ${s0_partition_path} TARGET b0n_target)
    dt_reg_size(s0_partition_size PATH ${s0_partition_path} TARGET b0n_target)
    # Using same identifiers as nrf5340 hooks use, as they represnt the same
    # information in hooks.
    zephyr_compile_definitions("PCD_NET_CORE_APP_ADDRESS=${s0_partition_address}")
    zephyr_compile_definitions("PCD_NET_CORE_APP_SIZE=${s0_partition_size}")
  endif()
endif()
