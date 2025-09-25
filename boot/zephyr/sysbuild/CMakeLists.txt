function(mathup num align result)
  math(EXPR out "(((${num}) + ((${align}) - 1)) & ~((${align}) - 1))")
  set(${result} "${out}" PARENT_SCOPE)
endfunction()

function(${SYSBUILD_CURRENT_MODULE_NAME}_pre_image_cmake)
  cmake_parse_arguments(PRE_IMAGE_CMAKE "" "IMAGE" "IMAGES" ${ARGN})

  if(NOT "${PRE_IMAGE_CMAKE_IMAGE}" STREQUAL "mcuboot" OR NOT SB_CONFIG_BOOTLOADER_MCUBOOT)
    return()
  endif()

  set_property(
    DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_BINARY_DIR}/mcuboot/CMakeCache.txt
    ${CMAKE_BINARY_DIR}/mcuboot/zephyr/.config
    )
endfunction(${SYSBUILD_CURRENT_MODULE_NAME}_pre_image_cmake)

function(${SYSBUILD_CURRENT_MODULE_NAME}_post_image_cmake)
  cmake_parse_arguments(POST_IMAGE_CMAKE "" "IMAGE" "IMAGES" ${ARGN})

  if(NOT "${POST_IMAGE_CMAKE_IMAGE}" STREQUAL "mcuboot" OR NOT SB_CONFIG_BOOTLOADER_MCUBOOT)
    return()
  endif()

  set(auto_images)
  if("${SB_CONFIG_MCUBOOT_IMAGES_ROM_END_OFFSET_AUTO}" STREQUAL "")
    UpdateableImage_Get(auto_images ALL)

    # If the list of updateable images is empty, look for the "MAIN" image type.
    list(LENGTH auto_images num_auto_images)
    if(num_auto_images EQUAL 0)
      foreach(image ${IMAGES})
        set(app_type)
        get_property(app_type TARGET ${image} PROPERTY APP_TYPE)

        if("${app_type}" STREQUAL "MAIN")
          list(APPEND auto_images ${image})
        endif()
      endforeach()
    endif()
  else()
    set(auto_images "${SB_CONFIG_MCUBOOT_IMAGES_ROM_END_OFFSET_AUTO}")
  endif()

  foreach(image ${auto_images})
    set(mcuboot_image_footer_size)
    set(mcuboot_image_upgrade_footer_size)
    sysbuild_get(mcuboot_image_footer_size IMAGE mcuboot CACHE)
    sysbuild_get(mcuboot_image_upgrade_footer_size IMAGE mcuboot CACHE)
    math(EXPR mcuboot_image_footer_size "${mcuboot_image_footer_size}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR mcuboot_image_upgrade_footer_size "${mcuboot_image_upgrade_footer_size}" OUTPUT_FORMAT HEXADECIMAL)

    set_property(TARGET ${image} APPEND_STRING PROPERTY CONFIG "CONFIG_ROM_END_OFFSET=${mcuboot_image_footer_size}\n")
    set_property(TARGET ${image} APPEND_STRING PROPERTY CONFIG "CONFIG_MCUBOOT_UPDATE_FOOTER_SIZE=${mcuboot_image_upgrade_footer_size}\n")
  endforeach()
endfunction(${SYSBUILD_CURRENT_MODULE_NAME}_post_image_cmake)
