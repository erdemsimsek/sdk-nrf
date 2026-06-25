/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief TFM IOCTL API header.
 */


#ifndef TFM_IOCTL_API_H__
#define TFM_IOCTL_API_H__

/**
 * @defgroup tfm_ioctl_api TFM IOCTL API
 * @{
 *
 */

#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <tfm_platform_api.h>
#include <hal/nrf_gpio.h>

/* Include core IOCTL services */
#include <tfm_ioctl_core_api.h>

#include <zephyr/autoconf.h>

#if CONFIG_FW_INFO
#include <fw_info_bare.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/* Board specific IOCTL services can be added here */
enum tfm_platform_ioctl_reqest_types_t {
	TFM_PLATFORM_IOCTL_FW_INFO = TFM_PLATFORM_IOCTL_CORE_LAST,
	TFM_PLATFORM_IOCTL_NS_FAULT,
	TFM_PLATFORM_IOCTL_RAM_CTRL,
};

#if CONFIG_FW_INFO

/** @brief Argument list for each platform firmware info service.
 */
struct tfm_fw_info_args_t {
	void *fw_address;
	struct fw_info *info;
};

/** @brief Output list for each platform firmware info service
 */
struct tfm_fw_info_out_t {
	uint32_t result;
};

/** Search for the fw_info structure in firmware image located at address.
 *
 * @param[in]   fw_address  Address where firmware image is stored.
 * @param[out]  info        Pointer to where found info is to be written.
 *
 * @retval 0        If successful.
 * @retval -EINVAL  If info is NULL or if no info is found.
 * @retval -EPERM   If the TF-M platform service request failed.
 */
int tfm_platform_firmware_info(uint32_t fw_address, struct fw_info *info);

/** Check if S0 is the active B1 slot.
 *
 * @param[in]   s0_address Address of s0 slot.
 * @param[in]   s1_address Address of s1 slot.
 * @param[out]  s0_active  Set to 'true' if s0 is active slot, 'false' otherwise
 *
 * @retval 0        If successful.
 * @retval -EINVAL  If info for both slots could not be found.
 * @retval -EPERM   If the TF-M platform service request failed.
 */
int tfm_platform_s0_active(uint32_t s0_address, uint32_t s1_address,
			   bool *s0_active);

#endif /* CONFIG_FW_INFO */

/** @brief Bitmask of SPU events */
enum tfm_spu_events {
	TFM_SPU_EVENT_RAMACCERR = 1 << 0,
	TFM_SPU_EVENT_FLASHACCERR = 1 << 1,
	TFM_SPU_EVENT_PERIPHACCERR = 1 << 2,
};

/** @brief Copy of exception frame on stack. */
struct tfm_ns_fault_service_handler_context_frame {
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t xpsr;
};

/** @brief Copy of callee saved registers. */
struct tfm_ns_fault_service_handler_context_registers {
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;
};

/** @brief Additional fault status information */
struct tfm_ns_fault_service_handler_context_status {
	uint32_t msp;
	uint32_t psp;
	uint32_t exc_return;
	uint32_t control;
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t sfsr;
	uint32_t bfar;
	uint32_t mmfar;
	uint32_t sfar;
	uint32_t spu_events;
	uint32_t vectactive;
};

/** @brief Non-secure fault service callback context argument. */
struct tfm_ns_fault_service_handler_context {
	bool valid;
	struct tfm_ns_fault_service_handler_context_registers registers;
	struct tfm_ns_fault_service_handler_context_frame frame;
	struct tfm_ns_fault_service_handler_context_status status;
};

/** @brief Non-secure fault service callback type. */
typedef void (*tfm_ns_fault_service_handler_callback)(void);

/** @brief Non-secure fault service arguments.*/
struct tfm_ns_fault_service_args {
	struct tfm_ns_fault_service_handler_context  *context;
	tfm_ns_fault_service_handler_callback callback;
};

/** @brief Output list for each nonsecure_fault platform service
 */
struct tfm_ns_fault_service_out {
	uint32_t result;
};

/** Search for the fw_info structure in firmware image located at address.
 *
 * @param[in] context  Pointer to callback context information, stored in non-secure memory.
 * @param[in] callback Callback to non-secure function to be called from secure fault handler.
 *
 * @retval 0        If successful.
 * @retval -EINVAL  If input arguments are invalid.
 */
int tfm_platform_ns_fault_set_handler(struct tfm_ns_fault_service_handler_context *context,
				      tfm_ns_fault_service_handler_callback callback);

/** @brief RAM-control service operation type. */
enum tfm_ram_ctrl_op {
	/** System ON power (MEMCONF CONTROL) — applied immediately. */
	TFM_RAM_CTRL_OP_POWER,
	/** System OFF retention (MEMCONF RET) — recorded now, applied at system off. */
	TFM_RAM_CTRL_OP_RETAIN,
	/** Read back MEMCONF POWER[0].CONTROL and RET (diagnostic). addr/len ignored. */
	TFM_RAM_CTRL_OP_DUMP,
};

/** @brief Argument list for the RAM-control service. */
struct tfm_ram_ctrl_args_t {
	uint32_t op;   /* enum tfm_ram_ctrl_op */
	uint32_t addr; /* RAM region start (must lie within NS-owned RAM) */
	uint32_t len;  /* RAM region length in bytes */
	uint32_t on;   /* powered/retained (true) or not (false) */
};

/** @brief Output for the RAM-control service. */
struct tfm_ram_ctrl_out_t {
	uint32_t result;
	uint32_t control;     /* MEMCONF POWER[0].CONTROL snapshot (DUMP op) */
	uint32_t ret;         /* MEMCONF POWER[0].RET snapshot (DUMP op) */
	uint32_t ret2;        /* MEMCONF POWER[0].RET2 snapshot (DUMP op) */
	uint32_t ret_planned; /* cached retention plan: mask of 32 KiB sections that
			       * WILL be retained at System OFF (DUMP op)
			       */
};

/** Power up/down a non-secure RAM range in System ON (MEMCONF CONTROL, immediate).
 *
 * @param[in] addr  Start address of the range (must be within NS-owned RAM).
 * @param[in] len   Length of the range in bytes.
 * @param[in] on    true to power up, false to power down.
 *
 * @retval 0        If successful.
 * @retval -EINVAL  If the range is not entirely within NS-owned RAM.
 * @retval -EPERM   If the TF-M platform service request failed.
 */
int nrf_ram_ctrl_svc_power_set(uintptr_t addr, size_t len, bool on);

/** Mark/unmark a non-secure RAM range for retention across System OFF (MEMCONF RET).
 *  Recorded in the secure domain and applied when the system enters System OFF
 *  via the TF-M system-off service.
 *
 * @param[in] addr  Start address of the range (must be within NS-owned RAM).
 * @param[in] len   Length of the range in bytes.
 * @param[in] on    true to retain across System OFF, false to drop retention.
 *
 * @retval 0        If successful.
 * @retval -EINVAL  If the range is not within NS-owned RAM or the shadow is full.
 * @retval -EPERM   If the TF-M platform service request failed.
 */
int nrf_ram_ctrl_svc_retention_set(uintptr_t addr, size_t len, bool on);

/** Read back the secure MEMCONF POWER[0] CONTROL and RET registers (diagnostic).
 *  Each bit i corresponds to a 32 KiB RAM section: CONTROL bit = powered (System ON),
 *  RET bit = retained (System OFF). Lets the non-secure app observe RAM power/retention
 *  state that it cannot read directly (MEMCONF is secure).
 *
 * @param[out] control      MEMCONF POWER[0].CONTROL value.
 * @param[out] ret          MEMCONF POWER[0].RET value.
 * @param[out] ret2         MEMCONF POWER[0].RET2 value.
 * @param[out] ret_planned  Cached retention plan: mask of 32 KiB sections that
 *                          will be retained at System OFF. May be NULL.
 *
 * @retval 0        If successful.
 * @retval -EPERM   If the TF-M platform service request failed.
 */
int nrf_ram_ctrl_svc_dump(uint32_t *control, uint32_t *ret, uint32_t *ret2,
			  uint32_t *ret_planned);


#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* TFM_IOCTL_API_H__ */
