/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Stand-in Wi-Fi ROM patch blob (LMAC or UMAC) for the nRF7120 patch DFU PoC.
 *
 * Built bare-metal (no Zephyr, no libc) and linked position-fixed at a fixed
 * origin inside the MCUboot image-1 slot. The application calls patch_entry()
 * directly at that origin and prints the returned version + message, proving
 * the patch code executes and changes across a DFU update.
 *
 * WIFI_PATCH_VERSION and PATCH_NAME are injected by the build (see
 * sysbuild.cmake); the fallbacks below only apply to a stand-alone compile.
 */

#include <stdint.h>

#ifndef WIFI_PATCH_VERSION
#define WIFI_PATCH_VERSION 1
#endif

#ifndef PATCH_NAME
#define PATCH_NAME PATCH
#endif

#define _STR(x) #x
#define STR(x) _STR(x)

struct patch_info {
	uint32_t version;
	const char *msg;
};

/*
 * The entry must be the first thing in the blob so it lands exactly at the
 * sub-application origin the application jumps to.
 */
__attribute__((section(".patch_entry"), used, noinline))
const struct patch_info *patch_entry(void)
{
	static const struct patch_info pi = {
		.version = WIFI_PATCH_VERSION,
		.msg = "Hello World from " STR(PATCH_NAME),
	};

	return &pi;
}
