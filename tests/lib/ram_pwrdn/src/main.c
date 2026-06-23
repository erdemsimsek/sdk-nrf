/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <malloc.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <ram_pwrdn.h>

#if defined(CONFIG_SOC_NRF52840) || defined(CONFIG_SOC_NRF52833)
#include <hal/nrf_power.h>

/* ===== nRF52 (POWER RAMPOWER) test helpers ===== */

struct bank_section {
	uint8_t bank_id;
	uint8_t sect_id;
};

static struct bank_section bank_section(uint8_t bank_id, uint8_t section_id)
{
	struct bank_section ret = { .bank_id = bank_id, .sect_id = section_id };

	return ret;
}

static bool check_section_up(uint8_t bank_id, uint8_t section_id)
{
	uint32_t mask = nrf_power_rampower_mask_get(NRF_POWER, bank_id);

	return mask & (NRF_POWER_RAMPOWER_S0POWER_MASK << section_id);
}

static bool check_section_range(struct bank_section first, struct bank_section last, bool up)
{
	for (uint8_t bank_id = first.bank_id; bank_id <= last.bank_id; ++bank_id) {
		uint8_t first_sect_id = bank_id == first.bank_id ? first.sect_id : 0;
		uint8_t last_sect_id = bank_id == last.bank_id ? last.sect_id : 1;

		for (uint8_t sect_id = first_sect_id; sect_id <= last_sect_id; ++sect_id) {
			if (check_section_up(bank_id, sect_id) != up) {
				return false;
			}
		}
	}

	return true;
}

static void teardown(void *f)
{
	const uintptr_t RAM_START_ADDR = 0x20000000UL;
	const uintptr_t RAM_END_ADDR = 0x20040000UL;

	power_up_ram(RAM_START_ADDR, RAM_END_ADDR);
}

/* ===== nRF52 test cases ===== */

ZTEST(ram_pwrdn, test_manual_power_control)
{
	const uintptr_t RAM_START_ADDR = 0x20000000UL;
	const uintptr_t RAM_END_ADDR = 0x20040000UL;
	const uintptr_t RAM_BANK_SECTION_SIZE = 0x1000UL;
	const uintptr_t RAM_BANK8_ADDR = 0x20010000UL;
	const uintptr_t RAM_BANK8_SECTION_SIZE = 0x8000UL;

	/* Power up all sections */
	power_up_ram(RAM_START_ADDR, RAM_END_ADDR);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 5), true),
		     "Enabling all RAM sections");

	/* Verify that powering down part of RAM section is not effective */
	power_down_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE + 1, RAM_END_ADDR);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 5), true),
		     "Disabling part of RAM section");

	/* Verify that powering down entire RAM section works */
	power_down_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE, RAM_END_ADDR);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 4), true),
		     "Disabling single RAM section (disabled too much)");
	zassert_true(check_section_range(bank_section(8, 5), bank_section(8, 5), false),
		     "Disabling single RAM section (failed)");

	/* Verify that powering down RAM section plus one byte has the same effect */
	power_down_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE, RAM_END_ADDR);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 4), true),
		     "Disabling more than RAM section (disabled too much)");
	zassert_true(check_section_range(bank_section(8, 5), bank_section(8, 5), false),
		     "Disabling more than RAM section (failed)");

	/* Power down last three sections */
	power_down_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE * 3, RAM_END_ADDR);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 2), true),
		     "Disabling three RAM sections (disabled too much)");
	zassert_true(check_section_range(bank_section(8, 3), bank_section(8, 5), false),
		     "Disabling three RAM sections (failed)");

	/* Verify that powering up one byte is enough to enable entire RAM section */
	power_up_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE * 3,
		     RAM_END_ADDR - RAM_BANK8_SECTION_SIZE * 3 + 1);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 3), true),
		     "Enabling one byte (failed)");
	zassert_true(check_section_range(bank_section(8, 4), bank_section(8, 5), false),
		     "Enabling one byte (enabled too much)");

	/* Verify that powering up entire RAM section has the same effect */
	power_up_ram(RAM_END_ADDR - RAM_BANK8_SECTION_SIZE * 3,
		     RAM_END_ADDR - RAM_BANK8_SECTION_SIZE * 2);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(8, 3), true),
		     "Enabling single RAM section (failed)");
	zassert_true(check_section_range(bank_section(8, 4), bank_section(8, 5), false),
		     "Enabling single RAM section (enabled too much)");

	/* Power down sections on the border between two banks */
	power_down_ram(RAM_BANK8_ADDR - RAM_BANK_SECTION_SIZE - 1,
		       RAM_BANK8_ADDR + RAM_BANK8_SECTION_SIZE);
	zassert_true(check_section_range(bank_section(0, 0), bank_section(7, 0), true),
		     "Disabling sections on two banks (disabled too much)");
	zassert_true(check_section_range(bank_section(7, 1), bank_section(8, 0), false),
		     "Disabling sections on two banks (failed)");
	zassert_true(check_section_range(bank_section(8, 1), bank_section(8, 3), true),
		     "Disabling sections on two banks (disabled too much)");
}

#elif defined(CONFIG_SOC_SERIES_NRF71)
#include <hal/nrf_memconf.h>

/* ===== nRF71 (MEMCONF) test helpers =====
 *
 * nRF7120 has 32 KiB sections in a single MEMCONF POWER[0] block. The
 * ram_pwrdn library manages RAM00 (sections 0-15) and RAM01 (sections 16-23)
 * only; RAM02 (sections 24-27, Wi-Fi IPC) and RAM03 (sections 28-31, FLPR /
 * boot ROM / KMU / CRACEN) are intentionally outside the managed range and must
 * never be powered down.
 */

#define NRF71_RAM_START_ADDR  0x20000000UL
#define NRF71_SECTION_SIZE    0x8000UL
#define NRF71_MANAGED_SECTS   24U /* RAM00 + RAM01 */
#define NRF71_MANAGED_END     (NRF71_RAM_START_ADDR + NRF71_MANAGED_SECTS * NRF71_SECTION_SIZE)
#define NRF71_RAM02_ADDR      NRF71_MANAGED_END /* 0x200C0000 */

static bool check_section_up(uint8_t section_id)
{
	uint32_t control = NRF_MEMCONF->POWER[0].CONTROL;

	return control & (1UL << (MEMCONF_POWER_CONTROL_MEM0_Pos + section_id));
}

/* Verify all sections in [first, last] match the expected power state. */
static bool check_section_range(uint8_t first, uint8_t last, bool up)
{
	for (uint8_t sect = first; sect <= last; ++sect) {
		if (check_section_up(sect) != up) {
			return false;
		}
	}

	return true;
}

static void teardown(void *f)
{
	power_up_ram(NRF71_RAM_START_ADDR, NRF71_MANAGED_END);
}

/* ===== nRF71 test cases ===== */

ZTEST(ram_pwrdn, test_manual_power_control)
{
	/* Power up all managed sections */
	power_up_ram(NRF71_RAM_START_ADDR, NRF71_MANAGED_END);
	zassert_true(check_section_range(0, 23, true), "Enabling all managed RAM sections");

	/* Powering down only part of a section must not power it down */
	power_down_ram(NRF71_MANAGED_END - NRF71_SECTION_SIZE + 1, NRF71_MANAGED_END);
	zassert_true(check_section_range(0, 23, true), "Disabling part of a RAM section");

	/* Powering down the whole last managed section (23) works */
	power_down_ram(NRF71_MANAGED_END - NRF71_SECTION_SIZE, NRF71_MANAGED_END);
	zassert_true(check_section_range(0, 22, true), "Disabling last section (disabled too much)");
	zassert_true(check_section_range(23, 23, false), "Disabling last section (failed)");

	/* Powering up a single byte re-enables the entire section */
	power_up_ram(NRF71_MANAGED_END - NRF71_SECTION_SIZE,
		     NRF71_MANAGED_END - NRF71_SECTION_SIZE + 1);
	zassert_true(check_section_range(0, 23, true), "Enabling one byte re-enables section");

	/* Power down the last three managed sections (21, 22, 23) */
	power_down_ram(NRF71_MANAGED_END - 3 * NRF71_SECTION_SIZE, NRF71_MANAGED_END);
	zassert_true(check_section_range(0, 20, true), "Disabling three sections (disabled too much)");
	zassert_true(check_section_range(21, 23, false), "Disabling three sections (failed)");
	power_up_ram(NRF71_RAM_START_ADDR, NRF71_MANAGED_END);

	/* A request that lands entirely in RAM02 must be a no-op (RAM02 is not
	 * managed, so the Wi-Fi IPC / reserved RAM is protected from power-down).
	 */
	power_down_ram(NRF71_RAM02_ADDR, NRF71_RAM02_ADDR + NRF71_SECTION_SIZE);
	zassert_true(check_section_range(0, 23, true), "RAM02 power-down request must be a no-op");

	/* A request crossing the RAM01->RAM02 boundary powers down only the last
	 * managed section (23) and clamps at the managed end; RAM02 is untouched.
	 */
	power_down_ram(NRF71_MANAGED_END - NRF71_SECTION_SIZE,
		       NRF71_RAM02_ADDR + NRF71_SECTION_SIZE);
	zassert_true(check_section_range(0, 22, true), "Boundary power-down (disabled too much)");
	zassert_true(check_section_range(23, 23, false), "Boundary power-down (failed)");
}

#else
#error "ram_pwrdn test is not supported on the current platform"
#endif

ZTEST_SUITE(ram_pwrdn, NULL, NULL, NULL, teardown, NULL);
