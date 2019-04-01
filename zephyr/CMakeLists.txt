if(CONFIG_BOOTLOADER_MCUBOOT)
  # Build a second bootloader image

  set(MCUBOOT_BASE ${CMAKE_CURRENT_LIST_DIR}/..)

  zephyr_add_executable(mcuboot require_build)

  if (${require_build})
    add_subdirectory(${MCUBOOT_BASE}/boot/zephyr ${CMAKE_CURRENT_BINARY_DIR}/mcuboot)

    set(to_sign ${KERNEL_HEX_NAME})

    # TODO: Assert that the bootloader and image use the same key.

    set(signed_image_hex ${PROJECT_BINARY_DIR}/signed.hex)
    set(signed_image_bin ${PROJECT_BINARY_DIR}/signed.bin)
    set(to_sign_bin ${PROJECT_BINARY_DIR}/to_sign.bin)
    set(update_hex ${PROJECT_BINARY_DIR}/update.hex)
    set(update_bin ${PROJECT_BINARY_DIR}/update.bin)

    set(merged_hex_file
      $<TARGET_PROPERTY:partition_manager,MCUBOOT_TO_SIGN>)
    set(merged_hex_file_depends
      $<TARGET_PROPERTY:partition_manager,MCUBOOT_TO_SIGN_DEPENDS>)
    set(sign_merged
      $<BOOL:$<TARGET_PROPERTY:partition_manager,MCUBOOT_TO_SIGN>>)
    set(to_sign
      $<IF:${sign_merged},${merged_hex_file},${PROJECT_BINARY_DIR}/${KERNEL_HEX_NAME}>)
    set(sign_depends
      $<IF:${sign_merged},${merged_hex_file_depends},kernel_elf>)
    add_custom_command(
      OUTPUT
      ${signed_image_hex}
      ${update_hex}
      ${update_bin}
      COMMAND
      ${PYTHON_EXECUTABLE}
      ${MCUBOOT_BASE}/scripts/imgtool.py
      sign
      --key ${MCUBOOT_BASE}/${CONFIG_BOOT_SIGNATURE_KEY_FILE}
      --header-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_HEADER_SIZE>
      --align       ${DT_FLASH_WRITE_BLOCK_SIZE}
      --version 0.1       # TODO: Configurable?
      --slot-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_SLOT_SIZE>
      --pad-header
      ${to_sign}
      ${signed_image_hex}
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target=ihex
      --output-target=binary
      ${to_sign}
      ${to_sign_bin}
      COMMAND
      ${PYTHON_EXECUTABLE}
      ${MCUBOOT_BASE}/scripts/imgtool.py
      sign
      --key ${MCUBOOT_BASE}/${CONFIG_BOOT_SIGNATURE_KEY_FILE}
      --header-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_HEADER_SIZE>
      --align       ${DT_FLASH_WRITE_BLOCK_SIZE}
      --version 0.1       # TODO: Configurable?
      --slot-size $<TARGET_PROPERTY:partition_manager,MCUBOOT_SLOT_SIZE>
      --pad-header
      ${to_sign_bin}
      ${update_bin}
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target=binary
      --output-target=ihex
      ${update_bin}
      ${update_hex}
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target=binary
      --output-target=ihex
      ${update_bin}
      ${update_hex}
      COMMAND
      ${CMAKE_OBJCOPY}
      --input-target=ihex
      --output-target=ihex
      --change-address $<TARGET_PROPERTY:partition_manager,MCUBOOT_SECONDARY_ADDRESS>
      ${update_hex}
      ${PROJECT_BINARY_DIR}/moved_update.hex
      DEPENDS
      ${sign_depends}
      )
    add_custom_target(mcuboot_sign_target DEPENDS ${signed_image_hex})

    set_property(GLOBAL APPEND PROPERTY
      HEX_FILES_TO_MERGE
      ${signed_image_hex}
      )
    set_property(GLOBAL APPEND PROPERTY
      HEX_FILES_TO_MERGE_TARGET
      ${logical_target_for_zephyr_elf}
      mcuboot_sign_target
      )
  endif() # ${require_build}
endif()
