/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

#if defined(CONFIG_RAM_POWER_DOWN_LIBRARY)
#include <ram_pwrdn.h>
#endif

#if !defined(CONFIG_SOC_NRF7120)
#error "This diagnostic sample supports nRF7120 only"
#endif

#define IDLE_TIME	 K_SECONDS(5)
#define POWERED_RAM_SIZE (128U * 1024U)

/*
 * Internal nRF7120 ENGA registers not exposed by the public HAL.
 * See README.rst for the MDK and internal-datasheet source of each address.
 */
#define NRF7120_REGULATORS_BASE		    ((uintptr_t)NRF_REGULATORS)
#define NRF7120_REGULATORS_DCDCEN_ADDR	    (NRF7120_REGULATORS_BASE + 0x540U)
#define NRF7120_REGULATORS_ENABLE_ADDR	    (NRF7120_REGULATORS_BASE + 0x544U)
#define NRF7120_REGULATORS_CONFIG_ADDR	    (NRF7120_REGULATORS_BASE + 0x548U)
#define NRF7120_REGULATORS_ELVCONFIG_ADDR   (NRF7120_REGULATORS_BASE + 0x550U)
#define NRF7120_REGULATORS_PORBORRESET_ADDR (NRF7120_REGULATORS_BASE + 0x584U)
#define NRF7120_REGULATORS_A2A_ADDR	    (NRF7120_REGULATORS_BASE + 0x588U)
#define NRF7120_REGULATORS_ROM_ADDR	    (NRF7120_REGULATORS_BASE + 0x58CU)

#define NRF7120_CLOCK_BASE		    ((uintptr_t)NRF_CLOCK)
#define NRF7120_CLOCK_XO_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x400U)
#define NRF7120_CLOCK_XO_ALWAYSRUN_ADDR	    (NRF7120_CLOCK_BASE + 0x404U)
#define NRF7120_CLOCK_XO_RUN_ADDR	    (NRF7120_CLOCK_BASE + 0x408U)
#define NRF7120_CLOCK_XO_STAT_ADDR	    (NRF7120_CLOCK_BASE + 0x40CU)
#define NRF7120_CLOCK_PLL_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x420U)
#define NRF7120_CLOCK_PLL_ALWAYSRUN_ADDR    (NRF7120_CLOCK_BASE + 0x424U)
#define NRF7120_CLOCK_PLL_RUN_ADDR	    (NRF7120_CLOCK_BASE + 0x428U)
#define NRF7120_CLOCK_PLL_STAT_ADDR	    (NRF7120_CLOCK_BASE + 0x42CU)
#define NRF7120_CLOCK_LFCLK_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x440U)
#define NRF7120_CLOCK_LFCLK_ALWAYSRUN_ADDR  (NRF7120_CLOCK_BASE + 0x444U)
#define NRF7120_CLOCK_LFCLK_RUN_ADDR	    (NRF7120_CLOCK_BASE + 0x448U)
#define NRF7120_CLOCK_LFCLK_STAT_ADDR	    (NRF7120_CLOCK_BASE + 0x44CU)
#define NRF7120_CLOCK_LFCLK_SRCCOPY_ADDR    (NRF7120_CLOCK_BASE + 0x450U)
#define NRF7120_CLOCK_PLL24M_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x460U)
#define NRF7120_CLOCK_PLL24M_ALWAYSRUN_ADDR (NRF7120_CLOCK_BASE + 0x464U)
#define NRF7120_CLOCK_PLL24M_RUN_ADDR	    (NRF7120_CLOCK_BASE + 0x468U)
#define NRF7120_CLOCK_PLL24M_STAT_ADDR	    (NRF7120_CLOCK_BASE + 0x46CU)
#define NRF7120_CLOCK_AUXPLL_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x480U)
#define NRF7120_CLOCK_AUXPLL_ALWAYSRUN_ADDR (NRF7120_CLOCK_BASE + 0x484U)
#define NRF7120_CLOCK_AUXPLL_RUN_ADDR	    (NRF7120_CLOCK_BASE + 0x488U)
#define NRF7120_CLOCK_AUXPLL_STAT_ADDR	    (NRF7120_CLOCK_BASE + 0x48CU)

#define NRF7120_HVBUCK_BASE		0x5012D000UL
#define NRF7120_HVBUCK_EVENTS_LP2HP_ADDR   (NRF7120_HVBUCK_BASE + 0x108U)
#define NRF7120_HVBUCK_EVENTS_HP2LP_ADDR   (NRF7120_HVBUCK_BASE + 0x10CU)
#define NRF7120_HVBUCK_EVENTS_HP2PWM_ADDR  (NRF7120_HVBUCK_BASE + 0x110U)
#define NRF7120_HVBUCK_EVENTS_PWM2HP_ADDR  (NRF7120_HVBUCK_BASE + 0x114U)
#define NRF7120_HVBUCK_STATUS_ADDR	(NRF7120_HVBUCK_BASE + 0x400U)
#define NRF7120_HVBUCK_STATUSANA_ADDR      (NRF7120_HVBUCK_BASE + 0x404U)
#define NRF7120_HVBUCK_FSMSTATEMMI_ADDR    (NRF7120_HVBUCK_BASE + 0x408U)
#define NRF7120_HVBUCK_VOUT0V65_ADDR	(NRF7120_HVBUCK_BASE + 0x40CU)
#define NRF7120_HVBUCK_VOUT0V8LP_ADDR	(NRF7120_HVBUCK_BASE + 0x410U)
#define NRF7120_HVBUCK_VOUT0V8HP_ADDR	(NRF7120_HVBUCK_BASE + 0x414U)
#define NRF7120_HVBUCK_VOUTUPSCALE_ADDR (NRF7120_HVBUCK_BASE + 0x418U)
#define NRF7120_HVBUCK_ITHRESHOLD_ADDR     (NRF7120_HVBUCK_BASE + 0x428U)
#define NRF7120_HVBUCK_IHYSTERESIS_ADDR    (NRF7120_HVBUCK_BASE + 0x42CU)
#define NRF7120_HVBUCK_MODECTRL_ADDR	(NRF7120_HVBUCK_BASE + 0x434U)
#define NRF7120_HVBUCK_VOLTAGECTRL_ADDR (NRF7120_HVBUCK_BASE + 0x438U)
#define NRF7120_HVBUCK_SWREADY_ADDR	(NRF7120_HVBUCK_BASE + 0x450U)
#define NRF7120_HVBUCK_CONFIG_CFGC_ADDR    (NRF7120_HVBUCK_BASE + 0x800U)
#define NRF7120_HVBUCK_CONFIG_CFG1_ADDR    (NRF7120_HVBUCK_BASE + 0x804U)

#define NRF7120_SAADC_BASE		0x500D5000UL
#define NRF7120_SAADC_PCRMSTATUS_ADDR	(NRF7120_SAADC_BASE + 0x404U)
#define NRF7120_SAADC_PCRMREQ_ADDR	(NRF7120_SAADC_BASE + 0x5FCU)

#define NRF7120_SREGS30_BASE	      0x5010F000UL
#define NRF7120_SREGS30_PDSELECT_ADDR (NRF7120_SREGS30_BASE + 0x780U)

#define NRF7120_WIFI_RPUPBUS_BASE		 0x48080000UL
#define NRF7120_WIFI_CLOCKRESETCTRL_OFFSET	 0x1B000UL
#define NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_OFFSET 0x160UL
#define NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_ADDR                                                     \
	(NRF7120_WIFI_RPUPBUS_BASE + NRF7120_WIFI_CLOCKRESETCTRL_OFFSET +                          \
	 NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_OFFSET)
#define NRF7120_WIFI_AUTOCGCORE_MASK (1UL << 28)

#if defined(CONFIG_NRF7120_IDLE_DIAGNOSTICS)
static uint32_t reg_read32(uintptr_t address)
{
	return *(volatile uint32_t *)address;
}

static void reg_write32(uintptr_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void print_power_snapshot(const char *phase)
{
	printk("%s: CONSTLATSTAT=0x%08x ELVCONFIG=0x%08x "
	       "HV_STATUS=0x%08x HV_MODECTRL=0x%08x "
	       "HV_VOLTAGECTRL=0x%08x HV_SWREADY=0x%08x\n",
	       phase, NRF_POWER->CONSTLATSTAT, reg_read32(NRF7120_REGULATORS_ELVCONFIG_ADDR),
	       reg_read32(NRF7120_HVBUCK_STATUS_ADDR), reg_read32(NRF7120_HVBUCK_MODECTRL_ADDR),
	       reg_read32(NRF7120_HVBUCK_VOLTAGECTRL_ADDR),
	       reg_read32(NRF7120_HVBUCK_SWREADY_ADDR));

	printk("%s: MEMCONF0 CONTROL=0x%08x RET=0x%08x RET2=0x%08x "
	       "MEMCONF1 CONTROL=0x%08x RET=0x%08x RET2=0x%08x\n",
	       phase, NRF_MEMCONF->POWER[0].CONTROL, NRF_MEMCONF->POWER[0].RET,
	       NRF_MEMCONF->POWER[0].RET2, NRF_MEMCONF->POWER[1].CONTROL, NRF_MEMCONF->POWER[1].RET,
	       NRF_MEMCONF->POWER[1].RET2);

	printk("%s: GRTC_MODE=0x%08x GRTC_SYSCOUNTER0_ACTIVE=0x%08x "
	       "LFXO_STATUS=0x%08x LFXO_MODE=0x%08x\n",
	       phase, NRF_GRTC->MODE, NRF_GRTC->SYSCOUNTER[0].ACTIVE, NRF_LFXO->STATUS,
	       NRF_LFXO->MODE);

	printk("%s: REG_DCDCEN=0x%08x REG_ENABLE=0x%08x "
	       "REG_CONFIG=0x%08x PORBORRESET=0x%08x "
	       "A2A=0x%08x ROM=0x%08x\n",
	       phase, reg_read32(NRF7120_REGULATORS_DCDCEN_ADDR),
	       reg_read32(NRF7120_REGULATORS_ENABLE_ADDR),
	       reg_read32(NRF7120_REGULATORS_CONFIG_ADDR),
	       reg_read32(NRF7120_REGULATORS_PORBORRESET_ADDR),
	       reg_read32(NRF7120_REGULATORS_A2A_ADDR), reg_read32(NRF7120_REGULATORS_ROM_ADDR));

	printk("%s: CLK_XO SRC=0x%08x ALWAYS=0x%08x RUN=0x%08x STAT=0x%08x "
	       "PLL SRC=0x%08x ALWAYS=0x%08x RUN=0x%08x STAT=0x%08x\n",
	       phase, reg_read32(NRF7120_CLOCK_XO_SRC_ADDR),
	       reg_read32(NRF7120_CLOCK_XO_ALWAYSRUN_ADDR), reg_read32(NRF7120_CLOCK_XO_RUN_ADDR),
	       reg_read32(NRF7120_CLOCK_XO_STAT_ADDR), reg_read32(NRF7120_CLOCK_PLL_SRC_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL_ALWAYSRUN_ADDR), reg_read32(NRF7120_CLOCK_PLL_RUN_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL_STAT_ADDR));

	printk("%s: CLK_LF SRC=0x%08x ALWAYS=0x%08x RUN=0x%08x "
	       "STAT=0x%08x COPY=0x%08x PLL24 SRC=0x%08x ALWAYS=0x%08x "
	       "RUN=0x%08x STAT=0x%08x\n",
	       phase, reg_read32(NRF7120_CLOCK_LFCLK_SRC_ADDR),
	       reg_read32(NRF7120_CLOCK_LFCLK_ALWAYSRUN_ADDR),
	       reg_read32(NRF7120_CLOCK_LFCLK_RUN_ADDR), reg_read32(NRF7120_CLOCK_LFCLK_STAT_ADDR),
	       reg_read32(NRF7120_CLOCK_LFCLK_SRCCOPY_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL24M_SRC_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL24M_ALWAYSRUN_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL24M_RUN_ADDR),
	       reg_read32(NRF7120_CLOCK_PLL24M_STAT_ADDR));

	printk("%s: CLK_AUX SRC=0x%08x ALWAYS=0x%08x RUN=0x%08x STAT=0x%08x\n", phase,
	       reg_read32(NRF7120_CLOCK_AUXPLL_SRC_ADDR),
	       reg_read32(NRF7120_CLOCK_AUXPLL_ALWAYSRUN_ADDR),
	       reg_read32(NRF7120_CLOCK_AUXPLL_RUN_ADDR),
	       reg_read32(NRF7120_CLOCK_AUXPLL_STAT_ADDR));

	uint32_t modectrl = reg_read32(NRF7120_HVBUCK_MODECTRL_ADDR);

	printk("%s: HV_VOUT0V65=0x%08x HV_VOUT0V8LP=0x%08x "
	       "HV_VOUT0V8HP=0x%08x HV_VOUTUPSCALE=0x%08x\n",
	       phase, reg_read32(NRF7120_HVBUCK_VOUT0V65_ADDR),
	       reg_read32(NRF7120_HVBUCK_VOUT0V8LP_ADDR),
	       reg_read32(NRF7120_HVBUCK_VOUT0V8HP_ADDR),
	       reg_read32(NRF7120_HVBUCK_VOUTUPSCALE_ADDR));
	printk("%s: HV_MODECTRL_VAL=%u BLOCK_MODE_LP=%u BLOCK_MODE_ULV=%u "
	       "BLOCK_MODE_PWM=%u (raw=0x%08x)\n",
	       phase, modectrl & 0x3U, (modectrl >> 8) & 0x1U, (modectrl >> 9) & 0x1U,
	       (modectrl >> 10) & 0x1U, modectrl);

	printk("%s: HV_STATUSANA=0x%08x HV_FSMSTATEMMI=0x%08x "
	       "HV_CFGC=0x%08x HV_CFG1=0x%08x\n",
	       phase, reg_read32(NRF7120_HVBUCK_STATUSANA_ADDR),
	       reg_read32(NRF7120_HVBUCK_FSMSTATEMMI_ADDR),
	       reg_read32(NRF7120_HVBUCK_CONFIG_CFGC_ADDR),
	       reg_read32(NRF7120_HVBUCK_CONFIG_CFG1_ADDR));
	printk("%s: HV_ITHRESHOLD=0x%08x HV_IHYSTERESIS=0x%08x "
	       "HV_EVT_LP2HP=%u HV_EVT_HP2LP=%u "
	       "HV_EVT_HP2PWM=%u HV_EVT_PWM2HP=%u\n",
	       phase, reg_read32(NRF7120_HVBUCK_ITHRESHOLD_ADDR),
	       reg_read32(NRF7120_HVBUCK_IHYSTERESIS_ADDR),
	       reg_read32(NRF7120_HVBUCK_EVENTS_LP2HP_ADDR),
	       reg_read32(NRF7120_HVBUCK_EVENTS_HP2LP_ADDR),
	       reg_read32(NRF7120_HVBUCK_EVENTS_HP2PWM_ADDR),
	       reg_read32(NRF7120_HVBUCK_EVENTS_PWM2HP_ADDR));

	printk("%s: SAADC_PCRMREQ=0x%08x SAADC_PCRMSTATUS=0x%08x\n", phase,
	       reg_read32(NRF7120_SAADC_PCRMREQ_ADDR),
	       reg_read32(NRF7120_SAADC_PCRMSTATUS_ADDR));
}
#endif

#if defined(CONFIG_NRF7120_PDSELECT_DIAGNOSTICS)
static void configure_pdselect(void)
{
	volatile uint32_t *const pdselect = (volatile uint32_t *)NRF7120_SREGS30_PDSELECT_ADDR;

	/* PIN0/P0.09 is disabled. PIN1/P0.10 carries the selected signal. */
	*pdselect = (CONFIG_NRF7120_PDSELECT_SIGNAL & 0xFU) << 4;
	__DSB();

#if defined(CONFIG_SERIAL)
	printk("PDSELECT enabled: P0.10 selector=%d register=0x%08x\n",
	       CONFIG_NRF7120_PDSELECT_SIGNAL, *pdselect);
#endif
}
#endif

#if defined(CONFIG_NRF7120_WIFI_AUTOCGCORE_CLEAR)
static void clear_wifi_autocgcore(void)
{
	volatile uint32_t *const autocg1 =
		(volatile uint32_t *)NRF7120_WIFI_CLOCKGATECTRLAUTOCG1_ADDR;
	uint32_t before = *autocg1;

	*autocg1 = before & ~NRF7120_WIFI_AUTOCGCORE_MASK;
	__DSB();

#if defined(CONFIG_SERIAL)
	printk("Wi-Fi AUTOCGCORE cleared: before=0x%08x after=0x%08x\n", before, *autocg1);
#endif
}
#endif

static void configure_ram(void)
{
#if defined(CONFIG_NRF7120_RAM_128K_ONLY)
	uintptr_t ram_start = DT_REG_ADDR(DT_CHOSEN(zephyr_sram));
	uintptr_t ram_end = ram_start + DT_REG_SIZE(DT_CHOSEN(zephyr_sram));

#if defined(CONFIG_SERIAL)
	printk("Keeping the first 128 KiB of application RAM powered\n");
#endif
	power_down_ram(ram_start + POWERED_RAM_SIZE, ram_end);
#elif defined(CONFIG_SERIAL)
	printk("No RAM power-down request\n");
#endif
}

#if defined(CONFIG_SERIAL)
static void run_timed_measurement(const struct device *console)
{
	int err;

#if defined(CONFIG_NRF7120_IDLE_PHASE_MARKER)
	/* High from here on: the device is running, not idling. */
	nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(0, 0));
	nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(0, 0));
#endif

	while (true) {
#if defined(CONFIG_NRF7120_IDLE_DIAGNOSTICS)
		print_power_snapshot("pre-idle");
		nrf_power_event_clear(NRF_POWER, NRF_POWER_EVENT_SLEEPENTER);
#endif
#if defined(CONFIG_NRF7120_HVBUCK_EVENT_CLEAR)
		reg_write32(NRF7120_HVBUCK_EVENTS_LP2HP_ADDR, 0);
		reg_write32(NRF7120_HVBUCK_EVENTS_HP2LP_ADDR, 0);
		reg_write32(NRF7120_HVBUCK_EVENTS_HP2PWM_ADDR, 0);
		reg_write32(NRF7120_HVBUCK_EVENTS_PWM2HP_ADDR, 0);
		printk("post-clear: HV_EVT_LP2HP=%u HV_EVT_HP2LP=%u "
		       "HV_EVT_HP2PWM=%u HV_EVT_PWM2HP=%u\n",
		       reg_read32(NRF7120_HVBUCK_EVENTS_LP2HP_ADDR),
		       reg_read32(NRF7120_HVBUCK_EVENTS_HP2LP_ADDR),
		       reg_read32(NRF7120_HVBUCK_EVENTS_HP2PWM_ADDR),
		       reg_read32(NRF7120_HVBUCK_EVENTS_PWM2HP_ADDR));
#endif

		printk("Entering System ON idle for 5 seconds\n");

#if defined(CONFIG_NRF7120_IDLE_PHASE_MARKER)
		/*
		 * Generate the falling edge, then disconnect the pad so the
		 * marker does not keep a GPIO output active during idle.
		 */
		nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(0, 0));
		nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(0, 0));
#endif

		err = pm_device_action_run(console, PM_DEVICE_ACTION_SUSPEND);
		if (err != 0) {
			printk("Failed to suspend console: %d\n", err);
			return;
		}

		k_sleep(IDLE_TIME);

		err = pm_device_action_run(console, PM_DEVICE_ACTION_RESUME);
		if (err != 0) {
			return;
		}

#if defined(CONFIG_NRF7120_IDLE_PHASE_MARKER)
		/* Rising edge: k_sleep/WFI returned, the device is running again. */
		nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(0, 0));
		nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(0, 0));
#endif

#if defined(CONFIG_NRF7120_IDLE_DIAGNOSTICS)
		printk("post-idle: SLEEPENTER=%u\n",
		       nrf_power_event_check(NRF_POWER, NRF_POWER_EVENT_SLEEPENTER));
		print_power_snapshot("post-idle");
#endif
		printk("Woke from System ON idle\n");
	}
}
#endif

int main(void)
{
#if defined(CONFIG_SERIAL)
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(console)) {
		return 0;
	}
#endif

	configure_ram();

#if defined(CONFIG_NRF7120_PDSELECT_DIAGNOSTICS)
	configure_pdselect();
#endif

#if defined(CONFIG_NRF7120_WIFI_AUTOCGCORE_CLEAR)
	clear_wifi_autocgcore();
#endif

#if defined(CONFIG_NRF7120_FORCE_LOWPWR)
	nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_LOWPWR);
#if defined(CONFIG_SERIAL)
	printk("POWER.TASKS_LOWPWR triggered\n");
#endif
#endif

#if defined(CONFIG_SERIAL)
	run_timed_measurement(console);
#endif

#if defined(CONFIG_NRF7120_SLEEP_FOREVER)
	k_sleep(K_FOREVER);
#endif

	return 0;
}
