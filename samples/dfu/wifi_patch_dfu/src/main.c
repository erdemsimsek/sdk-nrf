/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * nRF7120 Wi-Fi patch DFU PoC - application (MCUboot image 0).
 *
 * The Wi-Fi patch is a separate, MCUboot-signed image 1 (slot2/slot3) that
 * MCUboot validates and swaps but never boots. This application:
 *   1. reads the patch image's MCUboot header (proves the on-flash image),
 *   2. calls the LMAC and UMAC entry functions at their fixed origins
 *      (proves the patch *code* runs), printing the version each returns,
 *   3. confirms image 1 so MCUboot keeps it after a test-swap.
 *
 * Update the patch over UART (no debugger) with mcumgr:
 *   mcumgr ... image upload -n 1 wifi_patch.signed.bin
 *   mcumgr ... image test <hash> ; mcumgr ... reset
 * After reboot the printed version changes -> the patch was updated via MCUboot.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>

/* image 1 primary slot and the two sub-application origins (from the overlay). */
#define PATCH_SLOT_ID	FIXED_PARTITION_ID(slot2_partition)
#define LMAC_ENTRY_ADDR	((uint32_t)DT_REG_ADDR(DT_NODELABEL(wifi_lmac_patch_partition)))
#define UMAC_ENTRY_ADDR	((uint32_t)DT_REG_ADDR(DT_NODELABEL(wifi_umac_patch_partition)))

/* ABI contract with the patch blob (see wifi_patch/patch.c). */
struct patch_info {
	uint32_t version;
	const char *msg;
};

typedef const struct patch_info *(*patch_entry_fn_t)(void);

static void call_patch(const char *core, uint32_t addr)
{
	/* OR 1: Cortex-M is Thumb-only; the call target must have bit 0 set. */
	patch_entry_fn_t entry = (patch_entry_fn_t)(addr | 1u);
	const struct patch_info *pi = entry();

	printk("%s entry @ 0x%08x -> Version Info: %u (%s)\n",
	       core, addr, pi->version, pi->msg);
}

int main(void)
{
	struct mcuboot_img_header mh;
	int rc;

	printk("\n*** nRF7120 Wi-Fi patch DFU PoC (app built %s %s) ***\n",
	       __DATE__, __TIME__);

	/* (1) MCUboot header of the patch image: validates it and gives the
	 * version (the "on-flash image" proof). A non-zero rc means there is no
	 * valid signed patch in the slot, so do not jump into it.
	 */
	rc = boot_read_bank_header(PATCH_SLOT_ID, &mh, sizeof(mh));
	if (rc != 0 || mh.mcuboot_version != 1) {
		printk("Patch image not present/valid (rc %d); skipping patch calls\n", rc);
		return 0;
	}
	printk("Patch image (image 1) MCUboot version: %u.%u.%u+%u, size %u\n",
	       mh.h.v1.sem_ver.major, mh.h.v1.sem_ver.minor,
	       mh.h.v1.sem_ver.revision, mh.h.v1.sem_ver.build_num,
	       mh.h.v1.image_size);

	/* (2) Run the patch code and show the versions it reports. */
	call_patch("LMAC", LMAC_ENTRY_ADDR);
	call_patch("UMAC", UMAC_ENTRY_ADDR);

	/* (3) Keep image 1 after a test-swap so the update sticks. */
	rc = boot_write_img_confirmed_multi(1);
	printk("Confirm image 1: %s (rc %d)\n", rc == 0 ? "ok" : "skipped", rc);

	printk("Ready. Upload a new patch over UART: mcumgr ... image upload -n 1 <bin>\n");
	return 0;
}
