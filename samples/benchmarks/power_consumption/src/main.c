/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#if defined(CONFIG_RAM_POWER_DOWN_LIBRARY)
#include <ram_pwrdn.h>
#endif

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_MRAM_POWERDOWN_TEST)
#include <hal/nrf_mramc.h>
#endif

#define IDLE_TIME K_SECONDS(CONFIG_SAMPLE_POWER_CONSUMPTION_IDLE_SECONDS)

/*
 * WZN-9779: this bit is a source of a prolonged Wi-Fi core-clock
 * request (observed even after debugger disconnect), which keeps
 * HVBUCK from settling into its lowest-power state during idle.
 */
#define NRF7120_WIFI_RPUPBUS_BASE		 0x48080000UL
#define NRF7120_WIFI_CLOCKRESETCTRL_OFFSET	 0x1B000UL
#define NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_OFFSET 0x160UL
#define NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_ADDR                                                    \
	(NRF7120_WIFI_RPUPBUS_BASE + NRF7120_WIFI_CLOCKRESETCTRL_OFFSET +                          \
	 NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_OFFSET)
#define NRF7120_WIFI_AUTOCGCORE_MASK (1UL << 28)

static void clear_wifi_autocgcore(void)
{
	volatile uint32_t *const autocg1 =
		(volatile uint32_t *)NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_ADDR;

	*autocg1 &= ~NRF7120_WIFI_AUTOCGCORE_MASK;
}

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_DIAGNOSTICS)
/*
 * Write SREGS30.PDSELECT.PIN1 to route the selected power-domain status
 * signal to P0.10, matching the signal numbering used elsewhere for this
 * SoC's PDSELECT mux: 2 PD_LP, 3 PD_PERIPH, 4 PD_MCU, 7 PD_WIFI,
 * 8 PwrAboveElv ("high means not in ELV"). PIN0/P0.09 is left disabled (0).
 */
#define NRF7120_SREGS30_BASE	      0x5010F000UL
#define NRF7120_SREGS30_PDSELECT_ADDR (NRF7120_SREGS30_BASE + 0x780U)

static void configure_pdselect(void)
{
#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_SIGNAL_PD_LP)
	uint32_t signal = 2U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_SIGNAL_PD_PERIPH)
	uint32_t signal = 3U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_SIGNAL_PD_MCU)
	uint32_t signal = 4U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_SIGNAL_PD_WIFI)
	uint32_t signal = 7U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_SIGNAL_PWR_ABOVE_ELV)
	uint32_t signal = 8U;
#endif
	volatile uint32_t *const pdselect = (volatile uint32_t *)NRF7120_SREGS30_PDSELECT_ADDR;

	*pdselect = (signal & 0xFU) << 4;
}
#endif

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_MRAM_POWERDOWN_TEST)
#define NRF7120_HVBUCK_BASE	   0x5012D000UL
#define NRF7120_HVBUCK_STATUS_ADDR (NRF7120_HVBUCK_BASE + 0x400U)

static void test_mram_powerdown(void)
{
	nrf_mramc_power_autopowerdown_t autopowerdown;

	nrf_mramc_power_autopowerdown_get(NRF_MRAMC, &autopowerdown);
	printk("MRAMC.POWER.AUTOPOWERDOWN: enable=%u power_down_cfg=%u timeout_value=%u\n",
	       autopowerdown.enable, autopowerdown.power_down_cfg, autopowerdown.timeout_value);
	printk("MRAMC.POWER.STATUS before=0x%08x HVBUCK.STATUS before=0x%08x\n",
	       NRF_MRAMC->POWER.STATUS, *(volatile uint32_t *)NRF7120_HVBUCK_STATUS_ADDR);

	/* WARNING: nRF7120 executes code directly out of MRAM (XIP) --
	 * zephyr,flash/zephyr,code-partition both point into this same MRAM
	 * region. This write, and every instruction after it (including the
	 * printk below), must itself be fetched from MRAM -- if the hardware
	 * does not transparently stall/resume bus access across the
	 * power-down transition, the device hangs or crashes the instant
	 * power-down completes. That is the deliberate point of this test:
	 * either outcome is real, useful data about MRAM's behavior during
	 * a power transition.
	 */
	nrf_mramc_power_init_set(NRF_MRAMC, NRF_MRAMC_POWER_INIT_MODE_DOWN_TRIM_RET);

	printk("MRAMC.POWER.STATUS after=0x%08x HVBUCK.STATUS after=0x%08x\n",
	       NRF_MRAMC->POWER.STATUS, *(volatile uint32_t *)NRF7120_HVBUCK_STATUS_ADDR);
}
#endif

static void configure_ram_retention(void)
{
#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_UNUSED_ONLY)
	power_down_unused_ram();
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_64K) || \
	defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_128K) || \
	defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_256K) || \
	defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_512K)
	uintptr_t ram_start = DT_REG_ADDR(DT_CHOSEN(zephyr_sram));
	uintptr_t ram_end = ram_start + DT_REG_SIZE(DT_CHOSEN(zephyr_sram));

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_64K)
	uintptr_t retained_size = 64U * 1024U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_128K)
	uintptr_t retained_size = 128U * 1024U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_256K)
	uintptr_t retained_size = 256U * 1024U;
#elif defined(CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_512K)
	uintptr_t retained_size = 512U * 1024U;
#endif

	power_down_ram(ram_start + retained_size, ram_end);
#endif
	/* CONFIG_SAMPLE_POWER_CONSUMPTION_RAM_RETAIN_FULL (default): no
	 * power-down call at all -- every byte of application RAM stays retained.
	 */
}

int main(void)
{
#if defined(CONFIG_SERIAL)
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	int err;

	if (!device_is_ready(console)) {
		return 0;
	}

	printk("\nPower consumption demo ready.\n"
	       "Switch the board's power off and back on now to get correct "
	       "power consumption readings.\n\n");
#endif

	clear_wifi_autocgcore();
	configure_ram_retention();

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_PDSELECT_DIAGNOSTICS)
	configure_pdselect();
#endif

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_MRAM_POWERDOWN_TEST)
	test_mram_powerdown();
#endif

#if defined(CONFIG_SERIAL)
	uint32_t wakeups = 0;
#endif

	while (true) {
#if defined(CONFIG_SERIAL)
		/* Suspend the console (UART) before sleeping -- otherwise it
		 * stays fully active (and keeps whatever clock it depends on
		 * requested) for the whole "idle" window, which is the
		 * single biggest cause of elevated idle current in a sample
		 * like this.
		 */
		err = pm_device_action_run(console, PM_DEVICE_ACTION_SUSPEND);
		if (err != 0) {
			printk("Failed to suspend console: %d\n", err);
			return 0;
		}
#endif

		k_sleep(IDLE_TIME);

#if defined(CONFIG_SERIAL)
		err = pm_device_action_run(console, PM_DEVICE_ACTION_RESUME);
		if (err != 0) {
			return 0;
		}

		wakeups++;
		printk("Woken up %u time(s)\n", wakeups);
#endif
	}

	return 0;
}
