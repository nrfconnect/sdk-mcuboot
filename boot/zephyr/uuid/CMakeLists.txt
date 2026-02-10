#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

function(generate_uuid namespace input out_var)
  # Check if RAW UUID format is used
  message(STATUS "Generating UUID for input: ${input} with namespace: ${namespace}")
  string(REGEX MATCHALL "^([0-9A-F][0-9A-F]|\-)+$" match_parts "${input}")
  if("${match_parts}" STREQUAL "${input}")
    set(uuid ${match_parts})
  elseif("${namespace}" STREQUAL "UUID_DNS_NAMESPACE")
    # If DNS namespace is used - generate UUID based on DNS namespace
    set(namespace 6ba7b810-9dad-11d1-80b4-00c04fd430c8)
    string(
      UUID uuid
      NAMESPACE ${namespace}
      NAME ${input}
      TYPE SHA1 UPPER
    )
  elseif(NOT "${namespace}" STREQUAL "")
    # If not - generate UUID based on VID and CID values
    string(
      UUID uuid
      NAMESPACE ${namespace}
      NAME ${input}
      TYPE SHA1 UPPER
    )
  else()
    set(uuid "${out_var}-NOTFOUND")
  endif()

  set(${out_var} ${uuid} PARENT_SCOPE)
endfunction()

function(generate_raw_uuid uuid out_var)
  # Convert UUID into C array.
  string(REGEX REPLACE "([0-9A-F][0-9A-F])\-?" "0x\\1, " uuid_raw "${uuid}")
  set(${out_var} ${uuid_raw} PARENT_SCOPE)
endfunction()

if(CONFIG_NCS_MCUBOOT_UUID_SINGLE_VID OR CONFIG_NCS_MCUBOOT_UUID_SINGLE_VID_SLOTTED_CID)
  if(CONFIG_MCUBOOT_UUID_VID OR CONFIG_MCUBOOT_UUID_CID)
    zephyr_library_sources(
      uuid.c
      )
  endif()

  # Generate VID value and raw value definition
  if(CONFIG_MCUBOOT_UUID_VID OR CONFIG_MCUBOOT_UUID_CID)
    if("${CONFIG_NCS_MCUBOOT_UUID_VID_VALUE}" STREQUAL "" AND CONFIG_MCUBOOT_UUID_VID)
      message(WARNING "VID value not set")
      return()
    endif()

    generate_uuid(UUID_DNS_NAMESPACE "${CONFIG_NCS_MCUBOOT_UUID_VID_VALUE}" UUID_VID)

    if(CONFIG_MCUBOOT_UUID_VID)
      generate_raw_uuid("${UUID_VID}" UUID_VID_RAW)
      zephyr_compile_definitions(NCS_MCUBOOT_UUID_VID_VALUE=${UUID_VID_RAW})
    endif()
  endif()

  # Generate VID value(s) and raw value definition(s)
  if(CONFIG_MCUBOOT_UUID_CID)
    set(MCUBOOT_IMAGES_COUNT ${CONFIG_UPDATEABLE_IMAGE_NUMBER})
    foreach(image_id RANGE ${MCUBOOT_IMAGES_COUNT})
      if(CONFIG_NCS_MCUBOOT_UUID_CID_IMAGE_${image_id})
        if("${CONFIG_NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_VALUE}" STREQUAL "")
          message(WARNING "CID value not set for image ${image_id}")
          return()
        endif()

        generate_uuid("${UUID_VID}" "${CONFIG_NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_VALUE}" UUID_CID_IMAGE_${image_id})
        if ("${UUID_CID_IMAGE_${image_id}}" STREQUAL "UUID_CID_IMAGE_${image_id}-NOTFOUND")
          message(WARNING "VID value not set, cannot generate CID for image ${image_id}")
          return()
        endif()

        generate_raw_uuid("${UUID_CID_IMAGE_${image_id}}" UUID_CID_IMAGE_${image_id}_RAW)
        zephyr_compile_definitions(NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_VALUE=${UUID_CID_IMAGE_${image_id}_RAW})

        if(CONFIG_NCS_MCUBOOT_UUID_SINGLE_VID_SLOTTED_CID)
          if("${CONFIG_NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_SECONDARY_VALUE}" STREQUAL "")
            message(WARNING "Secondary slot CID value not set for image ${image_id}")
            return()
          endif()

          generate_uuid("${UUID_VID}" "${CONFIG_NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_SECONDARY_VALUE}" UUID_CID_IMAGE_${image_id}_SECONDARY)
          if ("${UUID_CID_IMAGE_${image_id}_SECONDARY}" STREQUAL "UUID_CID_IMAGE_${image_id}_SECONDARY-NOTFOUND")
            message(WARNING "VID value not set, cannot generate secondary slot CID for image ${image_id}")
            return()
          endif()

          generate_raw_uuid("${UUID_CID_IMAGE_${image_id}_SECONDARY}" UUID_CID_IMAGE_${image_id}_SECONDARY_RAW)
          zephyr_compile_definitions(NCS_MCUBOOT_UUID_CID_IMAGE_${image_id}_SECONDARY_VALUE=${UUID_CID_IMAGE_${image_id}_SECONDARY_RAW})
        endif()
      endif()
    endforeach()
  endif()
endif()
