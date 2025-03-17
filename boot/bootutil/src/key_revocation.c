/*
 * Copyright (c) 2025, Nordic Semiconductor ASA
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

#include <bootutil/key_revocation.h>

extern void exec_revoke(void);

static uint8_t ready_to_revoke;

void allow_revoke(void)
{
	ready_to_revoke = 1;
}

int revoke(void)
{
	if(ready_to_revoke) {
		return exec_revoke();
	}
	return 1;
}
