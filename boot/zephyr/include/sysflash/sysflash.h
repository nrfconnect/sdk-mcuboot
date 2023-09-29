/* Manual version of auto-generated version. */

#ifndef __SYSFLASH_H__
#define __SYSFLASH_H__

#if USE_PARTITION_MANAGER
#include <pm_config.h>
#include <mcuboot_config/mcuboot_config.h>

#ifndef CONFIG_SINGLE_APPLICATION_SLOT

#if (MCUBOOT_IMAGE_NUMBER == 1)

#define FLASH_AREA_IMAGE_PRIMARY(x)    PM_MCUBOOT_PRIMARY_ID
#define FLASH_AREA_IMAGE_SECONDARY(x)  PM_MCUBOOT_SECONDARY_ID

#elif (MCUBOOT_IMAGE_NUMBER == 2)

/* If B0 is present then two bootloaders are present, and we must use
 * a single secondary slot for both primary slots.
 */
#if defined(PM_B0_ADDRESS)
extern uint32_t _image_1_primary_slot_id[];
#endif
#if defined(PM_B0_ADDRESS) && defined(CONFIG_NRF53_MULTI_IMAGE_UPDATE)
#define FLASH_AREA_IMAGE_PRIMARY(x)          \
        ((x == 0) ?                          \
           PM_MCUBOOT_PRIMARY_ID :           \
         (x == 1) ?                          \
           PM_MCUBOOT_PRIMARY_1_ID :         \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x)        \
        ((x == 0) ?                          \
           PM_MCUBOOT_SECONDARY_ID:          \
        (x == 1) ?                           \
           PM_MCUBOOT_SECONDARY_1_ID:        \
           255 )
#elif defined(PM_B0_ADDRESS)

#define FLASH_AREA_IMAGE_PRIMARY(x)            \
        ((x == 0) ?                            \
           PM_MCUBOOT_PRIMARY_ID :             \
         (x == 1) ?                            \
          (uint32_t)_image_1_primary_slot_id : \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x) \
        ((x == 0) ?                   \
            PM_MCUBOOT_SECONDARY_ID:  \
        (x == 1) ?                    \
           PM_MCUBOOT_SECONDARY_ID:   \
           255 )
#else

#define FLASH_AREA_IMAGE_PRIMARY(x)          \
        ((x == 0) ?                          \
           PM_MCUBOOT_PRIMARY_ID :           \
         (x == 1) ?                          \
           PM_MCUBOOT_PRIMARY_1_ID :         \
           255 )

#define FLASH_AREA_IMAGE_SECONDARY(x) \
        ((x == 0) ?                   \
           PM_MCUBOOT_SECONDARY_ID:   \
        (x == 1) ?                    \
           PM_MCUBOOT_SECONDARY_1_ID: \
           255 )

#endif /* PM_B0_ADDRESS */

#endif
#define FLASH_AREA_IMAGE_SCRATCH    PM_MCUBOOT_SCRATCH_ID

#else /* CONFIG_SINGLE_APPLICATION_SLOT */

#define FLASH_AREA_IMAGE_PRIMARY(x)	PM_MCUBOOT_PRIMARY_ID
#define FLASH_AREA_IMAGE_SECONDARY(x)	PM_MCUBOOT_PRIMARY_ID
/* NOTE: Scratch parition is not used by single image DFU but some of
 * functions in common files reference it, so the definitions has been
 * provided to allow compilation of common units.
 */
#define FLASH_AREA_IMAGE_SCRATCH       0

#endif /* CONFIG_SINGLE_APPLICATION_SLOT */

#else

#include <zephyr/devicetree.h>
#include <mcuboot_config/mcuboot_config.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>

#ifndef CONFIG_SINGLE_APPLICATION_SLOT

#if (MCUBOOT_IMAGE_NUMBER == 1)
/*
 * NOTE: the definition below returns the same values for true/false on
 * purpose, to avoid having to mark x as non-used by all callers when
 * running in single image mode.
 */
#define FLASH_AREA_IMAGE_PRIMARY(x)    (((x) == 0) ?                \
                                         FIXED_PARTITION_ID(slot0_partition) : \
                                         FIXED_PARTITION_ID(slot0_partition))
#define FLASH_AREA_IMAGE_SECONDARY(x)  (((x) == 0) ?                \
                                         FIXED_PARTITION_ID(slot1_partition) : \
                                         FIXED_PARTITION_ID(slot1_partition))
#elif (MCUBOOT_IMAGE_NUMBER == 2)
/* MCUBoot currently supports only up to 2 updateable firmware images.
 * If the number of the current image is greater than MCUBOOT_IMAGE_NUMBER - 1
 * then a dummy value will be assigned to the flash area macros.
 */
#define FLASH_AREA_IMAGE_PRIMARY(x)    (((x) == 0) ?                \
                                         FIXED_PARTITION_ID(slot0_partition) : \
                                        ((x) == 1) ?                \
                                         FIXED_PARTITION_ID(slot2_partition) : \
                                         255)
#define FLASH_AREA_IMAGE_SECONDARY(x)  (((x) == 0) ?                \
                                         FIXED_PARTITION_ID(slot1_partition) : \
                                        ((x) == 1) ?                \
                                         FIXED_PARTITION_ID(slot3_partition) : \
                                         255)
#else
#error "Image slot and flash area mapping is not defined"
#endif

#if !defined(CONFIG_BOOT_SWAP_USING_MOVE)
#define FLASH_AREA_IMAGE_SCRATCH    FIXED_PARTITION_ID(scratch_partition)
#endif

#else /* CONFIG_SINGLE_APPLICATION_SLOT */

#define FLASH_AREA_IMAGE_PRIMARY(x)	FIXED_PARTITION_ID(slot0_partition)
#define FLASH_AREA_IMAGE_SECONDARY(x)	FIXED_PARTITION_ID(slot0_partition)

#endif /* CONFIG_SINGLE_APPLICATION_SLOT */

#endif /* USE_PARTITION_MANAGER */

#endif /* __SYSFLASH_H__ */
