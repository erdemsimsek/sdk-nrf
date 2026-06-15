/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Position-fixed link of the UMAC patch blob.
 *
 * ORIGIN MUST equal wifi_umac_patch_partition base in
 * boards/nrf7120dk_nrf7120_cpuapp.overlay:
 *   LMAC origin 0x3D2800 + LMAC region 0x4000 = 0x3D6800.
 */

MEMORY
{
	PATCH (rx) : ORIGIN = 0x003D6800, LENGTH = 16K
}

ENTRY(patch_entry)

SECTIONS
{
	. = ORIGIN(PATCH);

	.patch : {
		KEEP(*(.patch_entry))
		*(.text .text.*)
		*(.rodata .rodata.*)
	} > PATCH

	/DISCARD/ : {
		*(.ARM.exidx*)
		*(.ARM.extab*)
		*(.comment)
		*(.note*)
	}
}
