/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Position-fixed link of the LMAC patch blob.
 *
 * ORIGIN MUST equal wifi_lmac_patch_partition base in
 * boards/nrf7120dk_nrf7120_cpuapp.overlay:
 *   slot2 base 0x3D2000 + MCUboot header 0x800 = 0x3D2800.
 */

MEMORY
{
	PATCH (rx) : ORIGIN = 0x003D2800, LENGTH = 16K
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
