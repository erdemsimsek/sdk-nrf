.. _nrf7120_system_on_idle:

nRF7120 System ON idle diagnostics
##################################

Overview
********

This sample provides reproducible nRF7120 System ON idle power measurements
and diagnostic variants. It was created to investigate a device that entered
Cortex-M33 WFI but remained in the ``System ON CPU`` domain state instead of
the expected ``System ON idle`` state.

The sample is a small set of independent, composable pieces:

* **P0.10** reports any single internal power domain or regulator signal,
  selected per build.
* **P0.00** tracks System ON run versus System ON idle: high while running,
  low (and disconnected) while idling, directly bracketing every WFI
  entry/exit.
* **128 KiB RAM retention**, the **Wi-Fi ``AUTOCGCORE`` fix**, and
  **``POWER.TASKS_LOWPWR``** are each independent, optional toggles.
* A **diagnostic build** prints the full POWER/REGULATORS/HVBUCK/MEMCONF/
  GRTC/CLOCK/SAADC register state before and after every idle window.
* A **quiet build** disables UART and every other peripheral not needed for
  the measurement itself, for a stable current reading.

.. warning::

   This is an nRF7120 engineering diagnostic. It accesses internal ENGA
   registers that are not exposed by the public HAL. The Wi-Fi workaround and
   HVBUCK event-clear operations are diagnostic experiments, not production
   recommendations.

Requirements
************

Hardware
========

* nRF7120 DK.
* PPK2 or another current measurement instrument.
* Oscilloscope for measuring the post-inductor digital buck rail.
* Logic analyzer for P0.10 (selected domain) and P0.00 (run/idle marker).

Board target
============

Use:

.. code-block:: console

   nrf7120dk/nrf7120/cpuapp

Signal connections
===================

P0.10
   Verified external output for ``SREGS30.PDSELECT.PIN1``. A high level means
   the selected active-high status signal is asserted. Enabled by
   `CONFIG_NRF7120_PDSELECT_DIAGNOSTICS`_ and selected by
   `CONFIG_NRF7120_PDSELECT_SIGNAL`_.

P0.00
   High while System ON is running, low and disconnected while System ON is
   idling. Enabled by `CONFIG_NRF7120_IDLE_PHASE_MARKER`_.

P0.09
   ``PDSELECT.PIN0`` according to architecture documentation, but this pin is
   also the DK ``SWPWR`` supply for the Wi-Fi antenna switch. It did not
   reproduce the expected PD_MCU waveform in hardware tests and is not used
   by this sample.

Expected System ON idle state
==============================

The nRF7120 architecture power-state table says that System ON idle has only
``PD_AO`` and ``PD_MAIN`` powered. ``PD_LP``, ``PD_PERIPH``, ``PD_MCU``,
``PD_RADIO``, ``PD_WIFI``, and ``PD_MCU_AUX`` should be off, subject to their
documented retention configuration.

The minimum-current acceptance condition used during the investigation was:

* 2.0 microampere target, 3.0 microampere limit.
* Digital buck rail at approximately 0.65 V.
* No MHz clock running.
* GRTC and a 32 kHz source running.
* 128 KiB application RAM retained/powered.

Source layout
*************

.. code-block:: none

   nrf/samples/zephyr/boards/nordic/system_on_idle/
   |-- CMakeLists.txt
   |-- Kconfig
   |-- README.rst
   |-- prj.conf
   |-- sample.yaml
   |-- configs/
   `-- src/main.c

``src/main.c``
   Implements RAM configuration, the P0.10/PDSELECT and P0.00/run-idle
   markers, the Wi-Fi workaround, ``LOWPWR``, register snapshots, and the
   timed measurement loop.

``Kconfig``
   Defines all application-specific options.

``configs/*.conf``
   Small configuration fragments. A build combines only the fragments needed
   for one experiment.

``sample.yaml``
   Defines representative build-only Twister configurations.

Application execution order
****************************

1. Optionally request RAM power-down above the first 128 KiB.
2. Optionally route an internal power-domain/regulator signal to P0.10.
3. Optionally clear Wi-Fi ``AUTOCGCORE``.
4. Optionally trigger ``POWER.TASKS_LOWPWR``.
5. Enter exactly one selected idle strategy:

   * the timed loop, with P0.00 marker and/or register diagnostics, when
     ``CONFIG_SERIAL=y``;
   * ``k_sleep(K_FOREVER)`` when ``CONFIG_NRF7120_SLEEP_FOREVER=y``;
   * return from ``main()`` otherwise.

Kconfig options
****************

CONFIG_NRF7120_IDLE_DIAGNOSTICS
================================

Requires ``CONFIG_SERIAL=y``. Pure read-only register snapshot: enabling
this option alone never writes to any peripheral other than the two
unconditional, pre-existing actions already part of the timed loop (clearing
``POWER.EVENTS_SLEEPENTER`` and suspending/resuming the console device for
the measurement itself).

Prints these snapshots before and after each five-second ``k_sleep``:

* ``POWER.CONSTLATSTAT`` and ``POWER.EVENTS_SLEEPENTER``.
* ``REGULATORS.ELVCONFIG`` and startup configuration
  (``DCDCEN``/``REGULATORSENABLE``/``CONFIG``/``PORBORRESET``/``A2A``/``ROM``).
* MEMCONF power and retention masks.
* GRTC and LFXO state.
* CLOCK source, ``ALWAYSRUN``, ``RUN``, and ``STAT`` fields for XO, PLL,
  LFCLK, PLL24M, and AUXPLL.
* HVBUCK ``STATUS``, decoded ``MODECTRL`` (``VAL``/``BLOCK_MODE_LP``/
  ``BLOCK_MODE_ULV``/``BLOCK_MODE_PWM``), ``VOLTAGECTRL``, and ``SWREADY``.
* HVBUCK ``VOUT0V65``, ``VOUT0V8LP``, ``VOUT0V8HP``, and ``VOUTUPSCALE`` — the
  4 voltage-target codes for ULV/LP/HP/upscale mode. The internal datasheet
  gives no formula for these fields, but a live-hardware cross-check found
  one: ``VOUT0V8HP`` reads code ``10``, and ``0.6 V + 10 x 25 mV = 0.85 V``
  matches every buck-rail measurement taken throughout this investigation.
  Applying the same formula to ``VOUT0V65`` (code ``3``) gives ``0.675 V`` —
  already close to the ~0.65 V ELV target, meaning this register is *not*
  sitting at its documented unsafe reset value of ``0x00`` on this hardware.
* HVBUCK ``STATUSANA`` and ``FSMSTATEMMI``. ``STATUSANA``'s three bits
  (``READY_HVBUCK``/``SETTLED_MODE_HVBUCK``/``SETTLED_HVBUCK``) have no
  documented 0/1 semantics; by naming convention, all-1s reads as "healthy
  and settled." ``FSMSTATEMMI`` (the internal FSM state, bits [5:0]) has no
  documented enum at all.
* HVBUCK ``CONFIG.CFGC`` and ``CONFIG.CFG1``. Worth knowing: ``CFGC`` bit 13
  (``SEL_TH_P10``, name-only in the datasheet, no functional description) has
  been observed set (``0x00002000``) on real silicon despite a documented
  reset value of ``0x00000000`` — a real discrepancy, not a read error.
* HVBUCK ``ITHRESHOLD`` and ``IHYSTERESIS`` — the current-based thresholds
  gating the HP/LP boundary. Observed as ``10``/``1`` (mA-scale).
* HVBUCK ``EVENTS_LP2HP``, ``EVENTS_HP2LP``, ``EVENTS_HP2PWM``, and
  ``EVENTS_PWM2HP`` (read-only here; see
  `CONFIG_NRF7120_HVBUCK_EVENT_CLEAR`_ to also clear them per cycle).
* ``SAADC.PCRMREQ`` and ``SAADC.PCRMSTATUS`` — SAADC's own "request clean
  power from PCRM" bit and its acknowledge status. The datasheet's HVBUCK
  mode-transition diagram lists a "clean power request from PCRM" as one of
  the triggers forcing the state machine back toward HP, and SAADC is the
  only peripheral with an exact-name match for that mechanism. Hardware
  testing found ``PCRMREQ=0`` at all times — ruled out as the cause of the
  HP/LP bounce described below, not confirmed as it.

The post-idle snapshot occurs after UART resume. It proves persistent
configuration and whether the sleep event occurred, but it is not an
instantaneous snapshot taken while the CPU is powered down.
``POWER.EVENTS_SLEEPENTER=1`` means the CPU entered WFI/WFE. It does not
prove that PAC powered PD_MCU down.

CONFIG_NRF7120_HVBUCK_EVENT_CLEAR
==================================

Requires ``CONFIG_NRF7120_IDLE_DIAGNOSTICS``. This is the *only* write this
sample makes to the buck converter's own peripheral state, so it is opt-in
and default-off.

Enabled, it writes zero to ``EVENTS_LP2HP``, ``EVENTS_HP2LP``,
``EVENTS_HP2PWM``, and ``EVENTS_PWM2HP`` right before each idle window, then
immediately reads them back and prints a ``post-clear:`` line to prove the
clear actually took effect before anything else can set them again.

This is what revealed, and is needed to reproduce, the sample's most
significant HVBUCK finding: with this option enabled, ``EVENTS_HP2LP`` *and*
``EVENTS_LP2HP`` both fire on every single idle cycle, without exception —
proof of a genuine ``HP -> LP -> HP`` transition attempt on every cycle, one
too fast for ``STATUS`` (always sampled after it has already reverted) or an
oscilloscope (no visible dip at normal timescales) to catch directly. See
``NRF7120_HVBUCK_HP_MODE_OBSERVATION.md`` at the workspace root for the full
write-up.

CONFIG_NRF7120_FORCE_LOWPWR
============================

Executes:

.. code-block:: c

   nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_LOWPWR);

The datasheet says LOWPWR selects variable latency: during sleep, oscillators
can stop when nothing requests clocks, and regulators can stop when nothing
requests power. Hardware A/B testing found no measurable difference with or
without this option (~1.21 mA either way) — it is kept as a real, one-shot
hardware request worth trying, not because it is known to matter.

CONFIG_NRF7120_PDSELECT_DIAGNOSTICS
====================================

Enables the P0.10 PDSELECT output.

The code disables PIN0/P0.09 and writes the selected value into PIN1:

.. code-block:: c

   *pdselect = (CONFIG_NRF7120_PDSELECT_SIGNAL & 0xFU) << 4;

The register is:

.. code-block:: none

   SREGS30.PDSELECT = 0x5010F780

CONFIG_NRF7120_PDSELECT_SIGNAL
===============================

Chooses the active-high signal routed to P0.10:

.. list-table::
   :header-rows: 1

   * - Value
     - Signal
     - Interpretation when P0.10 is high
   * - 0
     - Disabled
     - P0.10 is not driven by PDSELECT
   * - 1
     - PD_MAIN
     - PD_MAIN is powered
   * - 2
     - PD_LP
     - PD_LP is powered
   * - 3
     - PD_PERIPH
     - PD_PERIPH is powered
   * - 4
     - PD_MCU
     - PD_MCU is powered
   * - 5
     - PD_RADIO
     - PD_RADIO is powered
   * - 6
     - PD_CRACEN/PD_MCU_AUX
     - Auxiliary/CRACEN domain is powered
   * - 7
     - PD_WIFI
     - PD_WIFI is powered
   * - 8
     - PwrAboveElv
     - Supply is above ELV
   * - 9
     - HVBUCK 1.1 V acknowledge
     - HVBUCK acknowledges 1.1 V
   * - 10
     - HVBUCK 1.0 V acknowledge
     - HVBUCK acknowledges 1.0 V
   * - 11
     - InLpMode
     - HVBUCK is in LP mode
   * - 12
     - HVBUCK PWM acknowledge
     - HVBUCK is in PWM mode
   * - 13
     - Helper-LDO acknowledge
     - Helper LDO acknowledge is asserted

CONFIG_NRF7120_IDLE_PHASE_MARKER
=================================

Requires UART and GPIO.

P0.00 is set high and connected as an output as soon as the timed loop
starts (so a rising edge shortly after power-up also proves the application
booted):

.. code-block:: c

   nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(0, 0));
   nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(0, 0));

Immediately before every ``k_sleep``/WFI call, P0.00 is driven low and
returned to its disconnected default configuration so an active GPIO output
cannot itself hold a power domain on:

.. code-block:: c

   nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(0, 0));
   nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(0, 0));

Immediately after ``k_sleep`` returns, it is driven high and reconnected
again. Logic-analyzer interpretation:

* P0.00 high: System ON running.
* Falling edge: about to enter System ON idle (WFI).
* P0.00 disconnected: System ON idle (WFI).
* Rising edge: WFI exited, System ON running again.

``POWER.EVENTS_SLEEPENTER`` remains the software proof that WFI/WFE was
executed; this marker is an external, logic-analyzer-visible reference for
exactly when.

CONFIG_NRF7120_SLEEP_FOREVER
==============================

Requires UART to be disabled.

Calls:

.. code-block:: c

   k_sleep(K_FOREVER);

This keeps the normal Zephyr scheduler and idle-thread implementation with no
periodic application wakeup at all — the configuration to use for an actual
quiet current measurement (combine with ``configs/quiet.conf`` and
``configs/sloppy_idle.conf``).

CONFIG_NRF7120_WIFI_AUTOCGCORE_CLEAR
=====================================

Applies the confirmed WZN-9779 workaround.

Register derivation:

.. code-block:: none

   Wi-Fi RPUPBUS base                  0x48080000
   WIFICORECLOCKRESETCTRL offset       0x0001B000
   CLOCKGATECTRLAUTOCG1 offset         0x00000160
   Final address                       0x4809B160
   AUTOCGCORE bit                      28

The code preserves all other bits:

.. code-block:: c

   uint32_t before = *autocg1;
   *autocg1 = before & ~(1UL << 28);

Observed result: ``PD_WIFI`` changed from high to low and current decreased
from approximately 1.20 mA to 365--440 microamperes. ``PD_MCU``, ``PD_LP``,
and ``PD_PERIPH`` have since been separately confirmed correctly releasing
during idle with this fix applied (see the domain marker builds below).

CONFIG_SYSTEM_CLOCK_SLOPPY_IDLE
================================

Enabled by ``configs/sloppy_idle.conf`` (standard Zephyr option, not defined
by this sample's own ``Kconfig``). When disabled, Zephyr periodically wakes
during a no-deadline idle to maintain accurate uptime; enabling it allows the
timer driver to treat ``K_FOREVER`` as truly deadline-free, at the cost of
possible uptime skew. Needed alongside `CONFIG_NRF7120_SLEEP_FOREVER`_ for an
uninterrupted quiet measurement.

Configuration fragments
*************************

Common fragments
=================

``configs/quiet.conf``
   Disables serial, console, UART console, ``printk``, device PM for the
   console, and both Zephyr/NCS boot banners. Use for stable current
   measurements. There is no UART output, and the P0.00/P0.10 markers are
   not meaningful in this mode (there is no repeating loop to observe).

``configs/ram_128k.conf``
   Enables the RAM power-down library and requests power-down above the first
   128 KiB of application RAM. Also forces RAM on before reboot.

``configs/diagnostics.conf``
   Enables the timed read-only register snapshots. Requires UART.

``configs/force_lowpwr.conf``
   Triggers ``POWER.TASKS_LOWPWR`` before the selected idle path.

``configs/idle_phase_marker.conf``
   Enables the P0.00 run/idle marker. Requires UART (the timed loop).

``configs/hvbuck_event_clear.conf``
   Opt-in: clears HVBUCK's four mode-change events before each idle window
   and prints the post-clear readback. The only fragment in this sample that
   writes to the buck converter's own peripheral state. See
   `CONFIG_NRF7120_HVBUCK_EVENT_CLEAR`_.

``configs/sleep_forever.conf``
   Blocks the main thread with ``k_sleep(K_FOREVER)``. Combine with
   ``quiet.conf``.

``configs/sloppy_idle.conf``
   Enables deadline-free timer shutdown with uptime-skew semantics.

``configs/wifi_autocgcore_clear.conf``
   Applies the WZN-9779 Wi-Fi core-clock workaround.

PDSELECT fragments
====================

Each of these enables PDSELECT and chooses one signal for P0.10:

.. list-table::
   :header-rows: 1

   * - File
     - Selector
     - Signal
   * - ``configs/pd_main.conf``
     - 1
     - PD_MAIN
   * - ``configs/pd_lp.conf``
     - 2
     - PD_LP
   * - ``configs/pd_periph.conf``
     - 3
     - PD_PERIPH
   * - ``configs/pd_mcu.conf``
     - 4
     - PD_MCU
   * - ``configs/pd_radio.conf``
     - 5
     - PD_RADIO
   * - ``configs/pd_cracen.conf``
     - 6
     - PD_CRACEN/PD_MCU_AUX
   * - ``configs/pd_wifi.conf``
     - 7
     - PD_WIFI
   * - ``configs/pwr_above_elv.conf``
     - 8
     - PwrAboveElv
   * - ``configs/in_lp_mode.conf``
     - 11
     - HVBUCK InLpMode

Building
********

Run commands from the workspace root.

Application path:

.. code-block:: console

   nrf/samples/zephyr/boards/nordic/system_on_idle

Configuration fragments are merged from left to right. The examples below
use complete pristine builds so that stale Kconfig state cannot carry between
experiments.

Domain markers (P0.00 idle timing + P0.10 domain signal)
============================================================

Three builds, one per domain, all including the confirmed Wi-Fi workaround.
Each drives **P0.00 high while running and low while idling**, and **P0.10
with the selected domain's live status** — put a logic analyzer on both:

.. code-block:: none

   D0 -> P0.00  high = running, low = System ON idle (WFI)
   D1 -> P0.10  the selected domain (PD_MCU / PD_LP / PD_PERIPH below)

PD_MCU:

.. code-block:: console

   west build -p always \
     -b nrf7120dk/nrf7120/cpuapp \
     nrf/samples/zephyr/boards/nordic/system_on_idle \
     -d build_nrf7120_system_on_idle_domain_marker_pd_mcu \
     -- \
     -DEXTRA_CONF_FILE="configs/ram_128k.conf;configs/diagnostics.conf;configs/force_lowpwr.conf;configs/wifi_autocgcore_clear.conf;configs/pd_mcu.conf;configs/idle_phase_marker.conf"

PD_LP:

.. code-block:: console

   west build -p always \
     -b nrf7120dk/nrf7120/cpuapp \
     nrf/samples/zephyr/boards/nordic/system_on_idle \
     -d build_nrf7120_system_on_idle_domain_marker_pd_lp \
     -- \
     -DEXTRA_CONF_FILE="configs/ram_128k.conf;configs/diagnostics.conf;configs/force_lowpwr.conf;configs/wifi_autocgcore_clear.conf;configs/pd_lp.conf;configs/idle_phase_marker.conf"

PD_PERIPH:

.. code-block:: console

   west build -p always \
     -b nrf7120dk/nrf7120/cpuapp \
     nrf/samples/zephyr/boards/nordic/system_on_idle \
     -d build_nrf7120_system_on_idle_domain_marker_pd_periph \
     -- \
     -DEXTRA_CONF_FILE="configs/ram_128k.conf;configs/diagnostics.conf;configs/force_lowpwr.conf;configs/wifi_autocgcore_clear.conf;configs/pd_periph.conf;configs/idle_phase_marker.conf"

Current result on all three (hardware-confirmed): P0.10 drops low during
every idle window and rises again for every active burst — the first time in
this investigation all three domains have been observed correctly releasing.

Buck converter register dump
=============================

Prints the full HVBUCK/SAADC read-only register snapshot described under
`CONFIG_NRF7120_IDLE_DIAGNOSTICS`_ every five seconds:

.. code-block:: console

   west build -p always \
     -b nrf7120dk/nrf7120/cpuapp \
     nrf/samples/zephyr/boards/nordic/system_on_idle \
     -d build_nrf7120_system_on_idle_diagnostics \
     -- \
     -DEXTRA_CONF_FILE="configs/ram_128k.conf;configs/diagnostics.conf;configs/force_lowpwr.conf"

Required observation:

.. code-block:: none

   post-idle: SLEEPENTER=1

Add ``configs/hvbuck_event_clear.conf`` to the fragment list to also see
per-cycle ``post-clear:`` lines and detect whether a real HP<->LP mode
transition happened during that specific idle window (see
`CONFIG_NRF7120_HVBUCK_EVENT_CLEAR`_) — this is the build that revealed the
every-cycle bounce written up in ``NRF7120_HVBUCK_HP_MODE_OBSERVATION.md``.

Quiet domain monitor
=====================

For an actual current/voltage measurement rather than a UART trace. Replace
``configs/pd_mcu.conf`` with any PDSELECT fragment from the table above:

.. code-block:: console

   west build -p always \
     -b nrf7120dk/nrf7120/cpuapp \
     nrf/samples/zephyr/boards/nordic/system_on_idle \
     -d build_nrf7120_system_on_idle_quiet_pdmcu \
     -- \
     -DEXTRA_CONF_FILE="configs/quiet.conf;configs/ram_128k.conf;configs/force_lowpwr.conf;configs/sleep_forever.conf;configs/sloppy_idle.conf;configs/wifi_autocgcore_clear.conf;configs/pd_mcu.conf"

There is no UART output. Probe P0.10 and measure current/buck voltage.

Flashing
********

.. code-block:: console

   west flash -d <build-directory>

.. important::

   **After flashing, unplug and replug the target's USB-C cable before
   taking any current or voltage reading.** This was found to matter:
   measuring immediately after a flash cycle (debugger session still
   recently active) and measuring after a full power cycle with no debug
   session attached have produced very different, and very differently
   trustworthy, results. Repeat the unplug/replug cycle more than once
   before trusting a low reading — a single good sample is a lead, not
   evidence.

Observed results
*****************

.. list-table::
   :header-rows: 1

   * - Variant
     - Main observation
   * - Timed diagnostics
     - ``SLEEPENTER=1``; approximately 1.2 mA floor
   * - Quiet 128 KiB + LOWPWR
     - Approximately 1.2 mA; buck 0.85--0.9 V
   * - PD_LP / PD_PERIPH / PD_MCU (domain marker builds)
     - All three confirmed releasing during idle, with the Wi-Fi fix applied
       — the first time in this investigation all three have been observed
       correctly releasing
   * - PD_WIFI before workaround
     - High
   * - PD_WIFI after AUTOCGCORE workaround
     - Low; current approximately 365--440 microamperes
   * - HVBUCK ``STATUS``
     - Always decodes to ``HPHyst`` in steady-state samples, even with all
       three domains above confirmed releasing
   * - HVBUCK event monitor (opt-in, ``hvbuck_event_clear.conf``)
     - ``EVENTS_HP2LP`` and ``EVENTS_LP2HP`` both fire every single idle
       cycle, without exception — a real, repeatable HP->LP->HP transition
       too fast for ``STATUS`` or a scope to catch directly
   * - ``SAADC.PCRMREQ``
     - Reads 0 at all times; ruled out as the cause of the bounce above

See ``NRF7120_HVBUCK_HP_MODE_OBSERVATION.md`` at the workspace root for the
full write-up of the last three rows, and the other ``NRF7120_*.md`` files
there for the complete history of this investigation, including earlier
isolation tests (ROMDONE, direct-WFI, grouped/SAADC peripheral removal) that
are no longer part of this simplified sample but whose results are still
recorded there.

Known limitations
*******************

* P0.10 PDSELECT reports domain/regulator state but not the owner of a power
  request.
* ``SLEEPENTER`` proves WFI/WFE entry but not domain shutdown.
* Top-level CLOCK ``RUN`` means a START task was triggered; it does not expose
  every automatic HCLK/PCLK consumer.
* UART snapshots occur before sleep or after UART resume, not during WFI.
* The complete PD_MCU PAC request-owner status is not exposed by the
  registers identified so far.
* ``FSMSTATEMMI`` and ``CONFIG.CFGC`` bit 13 (``SEL_TH_P10``) have no
  documented decode anywhere in the internal datasheet.
* There is no dedicated "entered ULV" event or FSM state. Per the datasheet's
  own HVBUCK event list and mode-transition diagram, ULV is a voltage-
  selection outcome inside ``LPHyst`` mode (``VOUT0V65`` selected instead of
  ``VOUT0V8LP``), not a distinct transition — so ``EVENTS_HP2LP`` firing is
  the most granular signal this peripheral can ever give; it cannot by
  itself distinguish a 0.8 V LP excursion from a 0.65 V ULV excursion.

Source authority
*****************

Hardware-specific values in this sample come from:

* nRF7120 Internal Datasheet v0.6.10 supplied by the user, including the
  REGULATORS/HVBUCK/SAADC register chapters and the HVBUCK mode-transition
  and voltage-selection diagrams.
* Local nRF7120 ENGA MDK.
* Architect-provided nRF7120 power-state and PDSELECT information.
* WZN-9779 and related internal issues.

nRF54L is only a shared architectural/software cross-reference for generic
Cortex-M WFI and MEMCONF concepts.

The following are divergent and must not be copied from nRF54L to nRF7120:

* RRAM versus MRAM power control.
* Regulator and ELV registers.
* Wi-Fi.
* Clocks and clock instances.
* GPIO mapping.
* Power-domain implementation.
* Register and memory maps.
