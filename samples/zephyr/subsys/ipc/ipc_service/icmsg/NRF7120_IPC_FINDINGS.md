# nRF7120 IPC ICMsg Porting - Detailed Findings and Issues

## Executive Summary

This document details the investigation and resolution of IPC (Inter-Process Communication) issues encountered while porting the `icmsg` sample to the nRF7120 chip. The primary issue was that the FLPR (Fast Lightweight Peripheral Processor) core was unable to receive mailbox interrupts from the Application core, causing the IPC handshake to fail.

**Root Cause**: A mismatch between the VPR task array indices and the CLIC interrupt numbers on nRF7120, combined with a driver bug that incorrectly calculated array indices.

---

## Table of Contents

1. [Background](#background)
2. [Initial Symptoms](#initial-symptoms)
3. [Investigation Process](#investigation-process)
4. [Root Cause Analysis](#root-cause-analysis)
5. [The Driver Bug](#the-driver-bug)
6. [Solution](#solution)
7. [Files Modified](#files-modified)
8. [Lessons Learned](#lessons-learned)

---

## 1. Background

### What is ICMsg?

ICMsg (Inter-Core Messaging) is a lightweight IPC backend in Zephyr that enables message passing between CPU cores using:
- **Shared memory regions** (`sram_rx`, `sram_tx`) for data transfer
- **Mailboxes (MBOX)** for signaling/interrupt notification between cores

### Architecture Overview

```
┌─────────────────────┐                    ┌─────────────────────┐
│     cpuapp (ARM)    │                    │   cpuflpr (RISC-V)  │
│                     │                    │                     │
│  ┌───────────────┐  │    Shared SRAM     │  ┌───────────────┐  │
│  │  IPC Service  │  │  ←─────────────→   │  │  IPC Service  │  │
│  └───────┬───────┘  │                    │  └───────┬───────┘  │
│          │          │                    │          │          │
│  ┌───────▼───────┐  │                    │  ┌───────▼───────┐  │
│  │ cpuapp_vevif  │  │   VEVIF Tasks      │  │ cpuflpr_vevif │  │
│  │   _tx (task)  │──┼──────────────────→─┼──│   _rx (task)  │  │
│  │   _rx (event) │←─┼────────────────────┼──│   _tx (event) │  │
│  └───────────────┘  │                    │  └───────────────┘  │
└─────────────────────┘                    └─────────────────────┘
```

### VEVIF (VPR Event Interface)

The VPR (Vector Processing Register) provides VEVIF for inter-core communication:
- **Task-TX**: Triggers tasks on the remote VPR (cpuapp → cpuflpr)
- **Task-RX**: Receives task triggers as interrupts (cpuflpr receives from cpuapp)
- **Event-TX**: Sends events to the remote core (cpuflpr → cpuapp)
- **Event-RX**: Receives events as interrupts (cpuapp receives from cpuflpr)

---

## 2. Initial Symptoms

### Symptom 1: No Bidirectional Communication

```
# cpuapp output:
[00:00:00.044,059] <inf> host: IPC-service HOST demo started
[00:00:00.044,188] <inf> host: Ep bounded
[00:00:00.044,286] <inf> host: Perform sends for 1000 [ms]
[00:00:01.044,096] <inf> host: Sent 1776 [Bytes] over 1000 [ms]
[00:00:01.544,263] <inf> host: Received 0 [Bytes] in total  ← NO DATA RECEIVED

# cpuflpr output:
[00:00:00.007,526] <inf> remote: IPC-service REMOTE demo started
                                                            ← NO "Ep bounded" message!
```

### Symptom 2: cpuflpr Stuck at Endpoint Registration

The cpuflpr core was stuck waiting at `ipc_service_register_endpoint()`, never receiving the handshake signal from cpuapp.

### Symptom 3: Interrupt Not Enabled

Debug output revealed:
```
ICMsg: mbox_init returned 0      ← mbox_init succeeded
Interrupt 21 enabled: 0          ← BUT interrupt is NOT enabled!
```

---

## 3. Investigation Process

### Step 1: Verify cpuapp is Sending

Added debug to `mbox_nrf_vevif_task_tx.c`:
```c
printk("VEVIF TX: triggered task %d on VPR %p\n", id, config->vpr);
```

**Result**: cpuapp WAS triggering tasks correctly:
```
VEVIF TX: triggered task 21 on VPR 0x5004c000
```

### Step 2: Verify cpuflpr mbox_init

Added debug to `icmsg.c`:
```c
printk("ICMsg: calling mbox_init\n");
ret = mbox_init(conf, dev_data);
printk("ICMsg: mbox_init returned %d\n", ret);
```

**Result**: mbox_init returned 0 (success), but interrupt still not enabled.

### Step 3: Check Interrupt Enable

Added debug after endpoint registration:
```c
printk("Interrupt 21 enabled: %d\n", irq_is_enabled(21));
```

**Result**: `Interrupt 21 enabled: 0` - The interrupt was never enabled despite mbox_init succeeding.

### Step 4: Trace the Driver Code Path

Examined `mbox_nrf_vevif_task_rx.c`:

```c
static int vevif_task_rx_set_enabled(const struct device *dev, uint32_t id, bool enable)
{
    uint8_t idx = id - TASKS_IDX_MIN;  // <-- THE BUG IS HERE
    ...
    irq_enable(vevif_irqs[idx]);
}
```

---

## 4. Root Cause Analysis

### The Chip-Specific Difference

The VPR (Vector Processing Register) has chip-specific task trigger indices:

| Chip | `VPR_TASKS_TRIGGER_MinIndex` | `VPR_TASKS_TRIGGER_MaxIndex` | CLIC IRQs Available |
|------|------------------------------|------------------------------|---------------------|
| nRF54L15 | **16** | 22 | 16-22 (VPRCLIC_16-22_IRQn) |
| nRF7120 | **0** | 31 | 16-22 (VPRCLIC_16-22_IRQn) |

### Critical Finding: CLIC IRQs are 16-22 on BOTH Chips

Looking at `nrf7120_enga_flpr.h`:
```c
VPRCLIC_16_IRQn = 16,
VPRCLIC_17_IRQn = 17,
VPRCLIC_18_IRQn = 18,
VPRCLIC_19_IRQn = 19,
VPRCLIC_20_IRQn = 20,
VPRCLIC_21_IRQn = 21,
VPRCLIC_22_IRQn = 22,
```

**Key Insight**: Even though nRF7120's `VPR_TASKS_TRIGGER_MinIndex = 0` (meaning VPR task array indices 0-31 are valid), the **CLIC interrupt numbers** for VEVIF are still **16-22**!

The `MinIndex` defines the task array bounds, NOT the interrupt numbers.

---

## 5. The Driver Bug

### The Problematic Code

In `mbox_nrf_vevif_task_rx.c`:

```c
#define TASKS_IDX_MIN NRF_VPR_TASKS_TRIGGER_MIN  // = 0 on nRF7120, 16 on nRF54L15

static const uint8_t vevif_irqs[VEVIF_TASKS_NUM] = {
    LISTIFY(DT_NUM_IRQS(DT_DRV_INST(0)), VEVIF_IRQN, (,))
};  // = {16, 17, 18, 19, 20, 21, 22} - 7 elements

static int vevif_task_rx_set_enabled(...)
{
    uint8_t idx = id - TASKS_IDX_MIN;  // THE BUG
    ...
    irq_enable(vevif_irqs[idx]);
}
```

### How the Bug Manifests

**On nRF54L15** (MinIndex = 16):
```
Channel 21: idx = 21 - 16 = 5
vevif_irqs[5] = 21 ✓ (within bounds, correct IRQ)
```

**On nRF7120** (MinIndex = 0):
```
Channel 21: idx = 21 - 0 = 21
vevif_irqs[21] = ??? ✗ (OUT OF BOUNDS! Array only has 7 elements)
```

### Visual Representation

```
vevif_irqs array (7 elements):
┌────┬────┬────┬────┬────┬────┬────┐
│ 16 │ 17 │ 18 │ 19 │ 20 │ 21 │ 22 │
└────┴────┴────┴────┴────┴────┴────┘
  [0]  [1]  [2]  [3]  [4]  [5]  [6]

nRF54L15 (MinIndex=16):
  Channel 21 → idx = 21-16 = 5 → vevif_irqs[5] = 21 ✓

nRF7120 (MinIndex=0):
  Channel 21 → idx = 21-0 = 21 → vevif_irqs[21] = ??? (garbage/crash)
```

### Why irq_enable() Didn't Crash

Reading out-of-bounds from the array returned garbage memory, which was likely a small number or zero. Calling `irq_enable()` with an invalid IRQ number simply did nothing (no error, but also no interrupt enabled).

---

## 6. Solution

### The Fix: Position-Based Index Calculation

Instead of using `id - TASKS_IDX_MIN`, we calculate the position of the task within the enabled tasks mask using popcount (population count = count of set bits):

```c
/**
 * Convert task ID to array index based on position in tasks-mask.
 *
 * Example with mask 0x007f0000 (bits 16-22):
 *   - id=16 -> idx=0 (no set bits below bit 16)
 *   - id=17 -> idx=1 (one set bit below: bit 16)
 *   - id=21 -> idx=5 (five set bits below: bits 16-20)
 */
static inline uint8_t vevif_task_to_idx(uint32_t id)
{
    uint32_t mask_below = VEVIF_TASKS_MASK & ((1UL << id) - 1);
    return __builtin_popcount(mask_below);
}
```

### How It Works

For `VEVIF_TASKS_MASK = 0x007f0000` (bits 16-22 set):

```
id=16: mask_below = 0x007f0000 & 0x0000ffff = 0x00000000
       popcount(0x00000000) = 0 → idx = 0 ✓

id=17: mask_below = 0x007f0000 & 0x0001ffff = 0x00010000
       popcount(0x00010000) = 1 → idx = 1 ✓

id=21: mask_below = 0x007f0000 & 0x001fffff = 0x001f0000
       popcount(0x001f0000) = 5 → idx = 5 ✓
```

This correctly maps channel 21 to array index 5, which contains IRQ number 21.

---

## 7. Files Modified

### Driver Fix
- `zephyr/drivers/mbox/mbox_nrf_vevif_task_rx.c`
  - Added `vevif_task_to_idx()` helper function
  - Updated `vevif_task_rx_isr()`, `vevif_task_rx_register_callback()`, and `vevif_task_rx_set_enabled()` to use the new function

### Devicetree Files (Reverted to Correct Values)
- `nrf/dts/riscv/nordic/nrf7120_enga_cpuflpr.dtsi`
  - Interrupts: 16-22 (CLIC IRQ numbers)
  - tasks-mask: 0x007f0000 (bits 16-22)
  - events-mask: 0x00100000 (bit 20)

- `nrf/dts/arm/nordic/nrf7120_enga_cpuapp.dtsi`
  - tasks-mask: 0x007f0000
  - events-mask: 0x00100000

### Overlay Files (Reverted to Correct Channels)
- `nrf7120pdk_nrf7120_cpuapp.overlay`: mboxes channels 20/21
- `nrf7120pdk_nrf7120_cpuflpr.overlay`: mboxes channels 21/20
- `nrf7120pdk_nrf7120_cpuapp_icbmsg.overlay`: mboxes channels 20/21
- `nrf7120pdk_nrf7120_cpuflpr_icbmsg.overlay`: mboxes channels 21/20

---

## 8. Lessons Learned

### 1. Task Index ≠ Interrupt Number

The VPR `TASKS_TRIGGER_MinIndex` defines the valid range of task array indices, NOT the corresponding interrupt numbers. On nRF7120, tasks 0-31 are valid indices, but only tasks 16-22 have corresponding CLIC interrupts.

### 2. Devicetree Defines Hardware, Not Software Abstraction

The devicetree `interrupts` property should contain the **actual hardware IRQ numbers** (16-22), not arbitrary numbers. The driver must correctly map task channels to these IRQ numbers.

### 3. Array Bounds Bugs Can Be Silent

Accessing `vevif_irqs[21]` when the array only has 7 elements didn't crash - it just read garbage. This made the bug harder to diagnose because `mbox_init()` returned success.

### 4. Debug Output is Essential

Adding strategic `printk()` statements was crucial for diagnosing the issue:
- Confirmed cpuapp was sending
- Confirmed mbox_init succeeded
- Revealed the interrupt was never enabled

### 5. Compare Working vs Non-Working Configurations

Comparing nRF54L15 (working) with nRF7120 (not working) revealed the chip-specific differences that exposed the driver bug.

---

## Appendix: Quick Reference

### Correct Configuration Summary

| Item | cpuapp | cpuflpr |
|------|--------|---------|
| VEVIF RX channel | 20 | 21 |
| VEVIF TX channel | 21 | 20 |
| IRQ for RX | 76 (VPR00_IRQn) | 21 (VPRCLIC_21_IRQn) |
| tasks-mask | 0x007f0000 | 0x007f0000 |
| events-mask | 0x00100000 | 0x00100000 |

### Build Command

```bash
cd /home/ers4/WORK/zephyr_nrf7120
rm -rf build
source .venv/bin/activate
west build -b nrf7120pdk/nrf7120/cpuapp \
    nrf/samples/zephyr/subsys/ipc/ipc_service/icmsg \
    --sysbuild -- -Dicmsg_SNIPPET=nordic-flpr
```









