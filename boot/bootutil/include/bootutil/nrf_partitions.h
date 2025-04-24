/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_NSIB_PARTITIONS_
#define H_NSIB_PARTITIONS_

#if defined(CONFIG_PARTITION_MANAGER_ENABLED)

#include <pm_config.h>

#define MCUBOOT_PRIMARY_ADDRESS PM_MCUBOOT_PRIMARY_ADDRESS
#define MCUBOOT_ADDRESS PM_MCUBOOT_ADDRESS

#if defined(CONFIG_SECURE_BOOT)
#define NSIB_S0_SIZE PM_S0_SIZE
#define NSIB_S1_SIZE PM_S1_SIZE
#define NSIB_S0_ID PM_S0_ID
#define NSIB_S1_ID PM_S1_ID
#define NSIB_S0_ADDRESS PM_S0_ADDRESS
#define NSIB_S1_ADDRESS PM_S1_ADDRESS
#endif /* defined(CONFIG_SECURE_BOOT) */

#else /* defined(CONFIG_PARTITION_MANAGER_ENABLED) */

#define MCUBOOT_CONTAINER_NODE DT_GPARENT(DT_NODELABEL(boot_partition))
#define MCUBOOT_CONTAINER_ADDRESS DT_REG_ADDR(MCUBOOT_CONTAINER_NODE)
#define MCUBOOT_SIZE DT_REG_SIZE(DT_NODELABEL(boot_partition))
#define MCUBOOT_OFFSET DT_REG_ADDR(DT_NODELABEL(boot_partition))
#define MCUBOOT_ADDRESS (MCUBOOT_CONTAINER_ADDRESS + MCUBOOT_OFFSET)

#define MCUBOOT_PRIMARY_CONTAINER_NODE DT_GPARENT(DT_NODELABEL(slot0_partition))
#define MCUBOOT_PRIMARY_CONTAINER_ADDRESS DT_REG_ADDR(MCUBOOT_PRIMARY_CONTAINER_NODE)
#define MCUBOOT_PRIMARY_OFFSET DT_REG_ADDR(DT_NODELABEL(slot0_partition))
#define MCUBOOT_PRIMARY_ADDRESS (MCUBOOT_PRIMARY_CONTAINER_ADDRESS + MCUBOOT_PRIMARY_OFFSET)

#if defined(CONFIG_SECURE_BOOT)
#define NSIB_S0_SIZE MCUBOOT_SIZE
#define NSIB_S0_ID FIXED_PARTITION_ID(boot_partition)
#define NSIB_S0_ADDRESS MCUBOOT_ADDRESS

#define NSIB_S1_CONTAINER_NODE DT_GPARENT(DT_NODELABEL(s1_partition))
#define NSIB_S1_CONTAINER_ADDRESS DT_REG_ADDR(NSIB_S1_CONTAINER_NODE)
#define NSIB_S1_SIZE DT_REG_SIZE(DT_NODELABEL(s1_partition))
#define NSIB_S1_ID FIXED_PARTITION_ID(s1_partition)
#define NSIB_S1_OFFSET DT_REG_ADDR(DT_NODELABEL(s1_partition))
#define NSIB_S1_ADDRESS (NSIB_S1_CONTAINER_ADDRESS + NSIB_S1_OFFSET)

#endif /* defined(CONFIG_SECURE_BOOT) */

#endif /* defined(CONFIG_PARTITION_MANAGER_ENABLED) */

#endif /* H_NSIB_PARTITIONS_ */
