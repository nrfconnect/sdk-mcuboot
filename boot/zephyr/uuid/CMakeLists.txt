#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

set(edt_pickle ${CMAKE_BINARY_DIR}/zephyr/edt.pickle)
set(gen_mcuboot_config_path
  "${ZEPHYR_MCUBOOT_MODULE_DIR}/scripts/gen_mcuboot_config.py"
)
set(gen_mcuboot_config_args
  --edt-pickle "${edt_pickle}"
  --output-dir "${CMAKE_CURRENT_BINARY_DIR}"
)

if(CONFIG_MCUBOOT_UUID_VID)
  list(APPEND gen_mcuboot_config_args --uuid-vid-map)
endif()

if(CONFIG_MCUBOOT_UUID_CID)
  list(APPEND gen_mcuboot_config_args --uuid-cid-map)
endif()

if(CONFIG_MCUBOOT_UUID_VID OR CONFIG_MCUBOOT_UUID_CID)
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} ${gen_mcuboot_config_path} ${gen_mcuboot_config_args}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND_ERROR_IS_FATAL ANY
  )
  include(${CMAKE_CURRENT_BINARY_DIR}/uuid.cmake)
  target_link_libraries(uuid PUBLIC zephyr_interface)

  zephyr_library_sources(
    uuid.c
    )
  zephyr_library_link_libraries(uuid)
endif()
