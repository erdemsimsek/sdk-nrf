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

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_POWER_SNAPSHOT_DIAGNOSTICS)
#include <hal/nrf_power.h>
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

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_MRAM_POWERDOWN_TEST) || \
	defined(CONFIG_SAMPLE_POWER_CONSUMPTION_POWER_SNAPSHOT_DIAGNOSTICS)
#define NRF7120_HVBUCK_BASE	   0x5012D000UL
#define NRF7120_HVBUCK_STATUS_ADDR (NRF7120_HVBUCK_BASE + 0x400U)
#endif

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_MRAM_POWERDOWN_TEST)
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

	/* Poll a few more times instead of a single before/after snapshot --
	 * the first flash of this test caught POWER.STATUS mid-transition
	 * (PowerDownSeq, 6), not yet at the final OffTrimRetain (7).
	 */
	for (int i = 0; i < 5; i++) {
		printk("MRAMC.POWER.STATUS after[%d]=0x%08x HVBUCK.STATUS after[%d]=0x%08x\n", i,
		       NRF_MRAMC->POWER.STATUS, i, *(volatile uint32_t *)NRF7120_HVBUCK_STATUS_ADDR);
		k_msleep(10);
	}
}
#endif

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_POWER_SNAPSHOT_DIAGNOSTICS)
/*
 * Comprehensive read-only register dump, reusing every address already
 * proven and address-confirmed in the system_on_idle diagnostic sample
 * (branch nrf7120-system_off-system_on_idle-pdk), plus two registers not
 * yet read anywhere in that investigation: LFXO.EVENTS_ERRORSTARTING/
 * EVENTS_ERRORRUNNING (public HAL, directly relevant per Jira SLT2-5693's
 * own still-unresolved "ERRORSTARTING fires unexpectedly" finding on a
 * related chip). Intended methodology: capture this immediately after a
 * west flash showing the elevated-current symptom, capture it again
 * after a genuine power cycle showing the good result, and diff the two
 * outputs -- whichever register(s) differ is the empirical answer to
 * which analog macro was left in limbo.
 */
static uint32_t reg_read32(uintptr_t address)
{
	return *(volatile uint32_t *)address;
}

#define NRF7120_REGULATORS_BASE	    ((uintptr_t)NRF_REGULATORS)
#define NRF7120_REGULATORS_DCDCEN_ADDR	    (NRF7120_REGULATORS_BASE + 0x540U)
#define NRF7120_REGULATORS_ENABLE_ADDR	    (NRF7120_REGULATORS_BASE + 0x544U)
#define NRF7120_REGULATORS_CONFIG_ADDR	    (NRF7120_REGULATORS_BASE + 0x548U)
#define NRF7120_REGULATORS_ELVCONFIG_ADDR   (NRF7120_REGULATORS_BASE + 0x550U)
#define NRF7120_REGULATORS_PORBORRESET_ADDR (NRF7120_REGULATORS_BASE + 0x584U)
#define NRF7120_REGULATORS_A2A_ADDR	    (NRF7120_REGULATORS_BASE + 0x588U)
#define NRF7120_REGULATORS_ROM_ADDR	    (NRF7120_REGULATORS_BASE + 0x58CU)

#define NRF7120_CLOCK_BASE		    ((uintptr_t)NRF_CLOCK)
#define NRF7120_CLOCK_XO_SRC_ADDR	    (NRF7120_CLOCK_BASE + 0x400U)
#define NRF7120_CLOCK_XO_ALWAYSRUN_ADDR    (NRF7120_CLOCK_BASE + 0x404U)
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

#define NRF7120_HVBUCK_EVENTS_LP2HP_ADDR  (NRF7120_HVBUCK_BASE + 0x108U)
#define NRF7120_HVBUCK_EVENTS_HP2LP_ADDR  (NRF7120_HVBUCK_BASE + 0x10CU)
#define NRF7120_HVBUCK_EVENTS_HP2PWM_ADDR (NRF7120_HVBUCK_BASE + 0x110U)
#define NRF7120_HVBUCK_EVENTS_PWM2HP_ADDR (NRF7120_HVBUCK_BASE + 0x114U)
#define NRF7120_HVBUCK_STATUSANA_ADDR	  (NRF7120_HVBUCK_BASE + 0x404U)
#define NRF7120_HVBUCK_FSMSTATEMMI_ADDR   (NRF7120_HVBUCK_BASE + 0x408U)
#define NRF7120_HVBUCK_VOUT0V65_ADDR	  (NRF7120_HVBUCK_BASE + 0x40CU)
#define NRF7120_HVBUCK_VOUT0V8LP_ADDR	  (NRF7120_HVBUCK_BASE + 0x410U)
#define NRF7120_HVBUCK_VOUT0V8HP_ADDR	  (NRF7120_HVBUCK_BASE + 0x414U)
#define NRF7120_HVBUCK_VOUTUPSCALE_ADDR  (NRF7120_HVBUCK_BASE + 0x418U)
#define NRF7120_HVBUCK_ITHRESHOLD_ADDR	  (NRF7120_HVBUCK_BASE + 0x428U)
#define NRF7120_HVBUCK_IHYSTERESIS_ADDR   (NRF7120_HVBUCK_BASE + 0x42CU)
#define NRF7120_HVBUCK_MODECTRL_ADDR	  (NRF7120_HVBUCK_BASE + 0x434U)
#define NRF7120_HVBUCK_VOLTAGECTRL_ADDR   (NRF7120_HVBUCK_BASE + 0x438U)
#define NRF7120_HVBUCK_SWREADY_ADDR	  (NRF7120_HVBUCK_BASE + 0x450U)
#define NRF7120_HVBUCK_CONFIG_CFGC_ADDR   (NRF7120_HVBUCK_BASE + 0x800U)
#define NRF7120_HVBUCK_CONFIG_CFG1_ADDR   (NRF7120_HVBUCK_BASE + 0x804U)

#define NRF7120_SAADC_BASE	      0x500D5000UL
#define NRF7120_SAADC_PCRMSTATUS_ADDR (NRF7120_SAADC_BASE + 0x404U)
#define NRF7120_SAADC_PCRMREQ_ADDR    (NRF7120_SAADC_BASE + 0x5FCU)

#define NRF7120_OSCRFR_BASE	     0x5012A000UL
#define NRF7120_OSCRFR_TRIM_OSC_ADDR (NRF7120_OSCRFR_BASE + 0x440U)
#define NRF7120_OSCRFR_MIRROR_ADDR   (NRF7120_OSCRFR_BASE + 0x480U)

#define NRF7120_VDETAO0V8_BASE			  0x5012C000UL
#define NRF7120_VDETAO0V8_CONFIG_BROWNOUTLP_ADDR (NRF7120_VDETAO0V8_BASE + 0x41CU)

#define NRF7120_LDOHLP0V8_BASE		     0x50127000UL
#define NRF7120_LDOHLP0V8_VOUTHPHELPER_ADDR (NRF7120_LDOHLP0V8_BASE + 0x48CU)

/* LFXO.STATUSANA is internal-only, not in the public HAL struct. */
#define NRF7120_LFXO_STATUSANA_ADDR ((uintptr_t)NRF_LFXO + 0x404U)

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

	printk("%s: GRTC_MODE=0x%08x GRTC_SYSCOUNTER0_ACTIVE=0x%08x GRTC_CLKCFG=0x%08x "
	       "LFXO_STATUS=0x%08x LFXO_MODE=0x%08x\n",
	       phase, NRF_GRTC->MODE, NRF_GRTC->SYSCOUNTER[0].ACTIVE, NRF_GRTC->CLKCFG,
	       NRF_LFXO->STATUS, NRF_LFXO->MODE);

	printk("%s: LFXO_CLOAD=0x%08x LFXO_AMPLITUDECTRL=0x%08x LFXO_PWRUPCTRL=0x%08x\n",
	       phase, NRF_LFXO->CLOAD, NRF_LFXO->AMPLITUDECTRL, NRF_LFXO->PWRUPCTRL);

	/* Not yet read anywhere else in this investigation -- directly
	 * relevant per Jira SLT2-5693's own unresolved "ERRORSTARTING fires
	 * unexpectedly" finding on a related chip.
	 */
	printk("%s: LFXO_EVENTS_ERRORSTARTING=%u LFXO_EVENTS_ERRORRUNNING=%u\n", phase,
	       NRF_LFXO->EVENTS_ERRORSTARTING, NRF_LFXO->EVENTS_ERRORRUNNING);

	uint32_t lfxo_statusana = reg_read32(NRF7120_LFXO_STATUSANA_ADDR);

	printk("%s: LFXO_STATUSANA=0x%08x READY=%u SETTLED=%u PIXOACK=%u "
	       "PDSETTLED=%u PDOUTLOWER=%u PDOUTUPPER=%u\n",
	       phase, lfxo_statusana, lfxo_statusana & 0x1U, (lfxo_statusana >> 1) & 0x1U,
	       (lfxo_statusana >> 2) & 0x3U, (lfxo_statusana >> 4) & 0x1U,
	       (lfxo_statusana >> 5) & 0x1U, (lfxo_statusana >> 6) & 0x1U);

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
	       reg_read32(NRF7120_SAADC_PCRMREQ_ADDR), reg_read32(NRF7120_SAADC_PCRMSTATUS_ADDR));

	printk("%s: OSCRFR.MIRROR=0x%08x OSCRFR.TRIM.OSC=0x%08x "
	       "VDETAO0V8.CONFIG.BROWNOUTLP=0x%08x LDOHLP0V8.VOUTHPHELPER=0x%08x\n",
	       phase, reg_read32(NRF7120_OSCRFR_MIRROR_ADDR),
	       reg_read32(NRF7120_OSCRFR_TRIM_OSC_ADDR),
	       reg_read32(NRF7120_VDETAO0V8_CONFIG_BROWNOUTLP_ADDR),
	       reg_read32(NRF7120_LDOHLP0V8_VOUTHPHELPER_ADDR));
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

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_POWER_SNAPSHOT_DIAGNOSTICS)
	print_power_snapshot("boot");
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

#if defined(CONFIG_SAMPLE_POWER_CONSUMPTION_POWER_SNAPSHOT_DIAGNOSTICS)
		/* The "boot" snapshot is taken before the very first k_sleep()
		 * -- HVBUCK is still in plain HPHyst either way at that point,
		 * regardless of whether this run ends up stuck or clean. If
		 * the actual difference only becomes register-visible once a
		 * real idle/ULV transition has been attempted, this is where
		 * it would show up instead.
		 */
		print_power_snapshot("post-idle");
#endif
#endif
	}

	return 0;
}
