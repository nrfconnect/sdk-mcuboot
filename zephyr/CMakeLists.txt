if(CONFIG_BOOTLOADER_MCUBOOT)
  # Build a second bootloader image

  set(MCUBOOT_BASE ${CMAKE_CURRENT_LIST_DIR}/..)

  zephyr_add_executable(mcuboot require_build)

  if (${require_build})
    add_subdirectory(${MCUBOOT_BASE}/boot/zephyr ${CMAKE_BINARY_DIR}/mcuboot)
  endif() # ${require_build}

  set(to_sign_hex ${KERNEL_HEX_NAME})

  # TODO: Assert that the bootloader and image use the same key.

  set(signed_image_hex ${PROJECT_BINARY_DIR}/signed.hex)
  set(signed_image_bin ${PROJECT_BINARY_DIR}/signed.bin)
  set(to_sign_bin ${PROJECT_BINARY_DIR}/to_sign.bin)
  set(update_hex ${PROJECT_BINARY_DIR}/update.hex)
  set(update_bin ${PROJECT_BINARY_DIR}/update.bin)

  get_property(app_binary_dir GLOBAL PROPERTY PROJECT_BINARY_DIR)
  set(merged_hex_file
    ${app_binary_dir}/mcuboot_primary_app.hex)
  set(merged_hex_file_depends
    mcuboot_primary_app_hex)
  set(sign_merged
    $<TARGET_EXISTS:partition_manager>)
  set(to_sign_hex
    $<IF:${sign_merged},${merged_hex_file},${PROJECT_BINARY_DIR}/${KERNEL_HEX_NAME}>)
  set(sign_depends
    $<IF:${sign_merged},${merged_hex_file_depends},zephyr_final>)
  set(sign_cmd
    ${PYTHON_EXECUTABLE}
    ${MCUBOOT_BASE}/scripts/imgtool.py
    sign
    --key ${MCUBOOT_BASE}/${CONFIG_BOOT_SIGNATURE_KEY_FILE}
    --header-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_HEADER_SIZE>
    --align       ${DT_FLASH_WRITE_BLOCK_SIZE}
    --version 0.1 # TODO configurable?
    --slot-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_SLOT_SIZE>
    --pad-header
    )

  add_custom_command(
    OUTPUT
    ${signed_image_hex}
    ${update_hex}
    ${update_bin}
    COMMAND
    ${sign_cmd}
    ${to_sign_hex}
    ${signed_image_hex}
    COMMAND
    ${CMAKE_OBJCOPY}
    --input-target=ihex
    --output-target=binary
    ${to_sign_hex}
    ${to_sign_bin}
    COMMAND
    ${sign_cmd}
    ${to_sign_bin}
    ${update_bin}
    COMMAND
    ${sign_cmd}
    --pad # This argument is needed for MCUboot to apply the test swap.
    ${to_sign_hex}
    ${update_hex}
    COMMAND
    ${CMAKE_OBJCOPY}
    --input-target=ihex
    --output-target=ihex
    --change-address $<TARGET_PROPERTY:partition_manager,MCUBOOT_SLOT_SIZE>
    ${update_hex}
    ${PROJECT_BINARY_DIR}/moved_update.hex
    DEPENDS
    ${sign_depends}
    )
  add_custom_target(mcuboot_sign_target DEPENDS ${signed_image_hex})

  set_property(GLOBAL PROPERTY
    mcuboot_primary_app_PM_HEX_FILE
    ${signed_image_hex}
    )
  set_property(GLOBAL PROPERTY
    mcuboot_primary_app_PM_TARGET
    mcuboot_sign_target
    )
endif()
