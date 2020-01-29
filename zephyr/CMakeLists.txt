if(CONFIG_BOOTLOADER_MCUBOOT)
  function(sign to_sign_hex output_prefix offset sign_depends signed_hex_out)
    set(op ${output_prefix})
    set(signed_hex ${op}_signed.hex)
    set(${signed_hex_out} ${signed_hex} PARENT_SCOPE)
    set(to_sign_bin ${op}_to_sign.bin)
    set(update_bin ${op}_update.bin)
    set(moved_test_update_hex ${op}_moved_test_update.hex)
    set(test_update_hex ${op}_test_update.hex)

    add_custom_command(
      OUTPUT
      ${update_bin}            # Signed binary of input hex.
      ${signed_hex}            # Signed hex of input hex.
      ${test_update_hex}       # Signed hex with IMAGE_MAGIC
      ${moved_test_update_hex} # Signed hex with IMAGE_MAGIC located at secondary slot

      COMMAND
      # Create signed hex file from input hex file.
      # This does not have the IMAGE_MAGIC at the end. So for this hex file
      # to be applied by mcuboot, the application is required to write the
      # IMAGE_MAGIC into the image trailer.
      ${sign_cmd}
      ${to_sign_hex}
      ${signed_hex}

      COMMAND
      # Create binary version of the input hex file, this is done so that we
      # can create a signed binary file which will be transferred in OTA
      # updates.
      ${CMAKE_OBJCOPY}
      --input-target=ihex
      --output-target=binary
      ${to_sign_hex}
      ${to_sign_bin}

      COMMAND
      # Sign the binary version of the input hex file.
      ${sign_cmd}
      ${to_sign_bin}
      ${update_bin}

      COMMAND
      # Create signed hex file from input hex file *with* IMAGE_MAGIC.
      # As this includes the IMAGE_MAGIC in its image trailer, it will be
      # swapped in by mcuboot without any invocation from the app. Note,
      # however, that this this hex file is located in the same address space
      # as the input hex file, so in order for it to work as a test update,
      # it needs to be moved.
      ${sign_cmd}
      --pad # Adds IMAGE_MAGIC to end of slot.
      ${to_sign_hex}
      ${test_update_hex}

      COMMAND
      # Create version of test update which is located at the secondary slot.
      # Hence, if a programmer is given this hex file, it will flash it
      # to the secondary slot, and upon reboot mcuboot will swap in the
      # contents of the hex file.
      ${CMAKE_OBJCOPY}
      --input-target=ihex
      --output-target=ihex
      --change-address ${offset}
      ${test_update_hex}
      ${moved_test_update_hex}

      DEPENDS
      ${sign_depends}
      )
  endfunction()

  # Build a second bootloader image
  set(MCUBOOT_BASE ${CMAKE_CURRENT_LIST_DIR}/..)

  add_child_image(mcuboot ${MCUBOOT_BASE}/boot/zephyr)

  set(merged_hex_file
    ${PROJECT_BINARY_DIR}/mcuboot_primary_app.hex)
  set(merged_hex_file_depends
    mcuboot_primary_app_hex$<SEMICOLON>${PROJECT_BINARY_DIR}/mcuboot_primary_app.hex)
  set(sign_merged
    $<TARGET_EXISTS:partition_manager>)
  set(app_to_sign_hex
    $<IF:${sign_merged},${merged_hex_file},${PROJECT_BINARY_DIR}/${KERNEL_HEX_NAME}>)
  set(app_sign_depends
    $<IF:${sign_merged},${merged_hex_file_depends},zephyr_final>)

  set(sign_cmd
    ${PYTHON_EXECUTABLE}
    ${MCUBOOT_BASE}/scripts/imgtool.py
    sign
    --key ${MCUBOOT_BASE}/${CONFIG_BOOT_SIGNATURE_KEY_FILE}
    --header-size $<TARGET_PROPERTY:partition_manager,PM_MCUBOOT_PAD_SIZE>
    --align       ${DT_FLASH_WRITE_BLOCK_SIZE}
    --version     ${CONFIG_MCUBOOT_IMAGE_VERSION}
    --slot-size   $<TARGET_PROPERTY:partition_manager,PM_MCUBOOT_PRIMARY_SIZE>
    --pad-header
    )

  set(app_offset $<TARGET_PROPERTY:partition_manager,PM_MCUBOOT_PRIMARY_SIZE>)

  sign(${app_to_sign_hex}     # Hex to sign
    ${PROJECT_BINARY_DIR}/app # Prefix for generated files
    ${app_offset}             # Offset
    ${app_sign_depends}       # Dependencies
    app_signed_hex            # Generated hex output variable
    )

  add_custom_target(mcuboot_sign_target DEPENDS ${app_signed_hex})

  set_property(GLOBAL PROPERTY
    mcuboot_primary_app_PM_HEX_FILE
    ${app_signed_hex}
    )
  set_property(GLOBAL PROPERTY
    mcuboot_primary_app_PM_TARGET
    mcuboot_sign_target
    )
endif()
