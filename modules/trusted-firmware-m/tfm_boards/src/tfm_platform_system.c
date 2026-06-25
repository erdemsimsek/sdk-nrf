/*
 * Copyright (c) 2021 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <platform/include/tfm_platform_system.h>
#include <cmsis.h>
#include <stdio.h>
#include <tfm_ioctl_api.h>
#include <string.h>
#include <arm_cmse.h>

#include "tfm_ioctl_api.h"
#include "tfm_platform_hal_ioctl.h"
#include <tfm_hal_isolation.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_regulators.h>
#include <hal/nrf_memconf.h>
#include <helpers/nrfx_ram_ctrl.h>

#include "handle_attr.h"

#if NRF_ALLOW_NON_SECURE_FAULT_HANDLING
#include "ns_fault_service.h"
#endif /* CONFIG_TFM_ALLOW_NON_SECURE_FAULT_HANDLING */

#if TFM_NRF_RAM_CTRL_SERVICE
#include "region_defs.h"

/* True only if [addr, addr+len) lies entirely within the non-secure RAM window.
 * The address is a RAM coordinate for nrfx_ram_ctrl; it is never dereferenced,
 * so no tfm_hal_memory_check is required.
 */
static bool ram_ctrl_range_is_ns(uint32_t addr, uint32_t len)
{
	if (len == 0U || addr > (UINT32_MAX - len)) {
		return false;
	}

	return (addr >= NS_DATA_START) && ((addr + len - 1U) <= NS_DATA_LIMIT);
}

/* RAM section granularity for the retention cache (32 KiB on nRF54L/nRF7120). */
#define RAM_CTRL_SECTION_SIZE 0x8000U

/* Cache of NS-requested System OFF retention, as a mask of 32 KiB RAM sections
 * (bit i => section i of MEMCONF.POWER[0]). The power-saving default is "retain
 * nothing": tfm_platform_hal_system_off() drops ALL retention and then
 * re-enables exactly this set. Lives in secure RAM and is rebuilt by the NS app
 * on every boot (a System OFF wake is a cold start, so the cache does not - and
 * need not - survive).
 */
static uint32_t s_ret_section_mask;

static uint32_t ram_ctrl_range_to_section_mask(uint32_t addr, uint32_t len)
{
	uint32_t first, last, count;

	if (len == 0U || addr < NRF_MEMORY_RAM_BASE) {
		return 0U;
	}
	first = (addr - NRF_MEMORY_RAM_BASE) / RAM_CTRL_SECTION_SIZE;
	last = (addr + len - 1U - NRF_MEMORY_RAM_BASE) / RAM_CTRL_SECTION_SIZE;
	if (last > 31U) {
		return 0U;
	}
	count = last - first + 1U;
	if (count >= 32U) {
		return 0xFFFFFFFFU;
	}
	return ((1U << count) - 1U) << first;
}

static enum tfm_platform_err_t tfm_platform_hal_ram_ctrl_service(psa_invec *in_vec,
								 psa_outvec *out_vec)
{
	struct tfm_ram_ctrl_args_t *args;
	struct tfm_ram_ctrl_out_t *out;

	if (in_vec->len != sizeof(struct tfm_ram_ctrl_args_t) ||
	    out_vec->len != sizeof(struct tfm_ram_ctrl_out_t)) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	args = (struct tfm_ram_ctrl_args_t *)in_vec->base;
	out = (struct tfm_ram_ctrl_out_t *)out_vec->base;
	out->result = -1;
	out->control = 0;
	out->ret = 0;
	out->ret2 = 0;
	out->ret_planned = 0;

	/* Diagnostic read-back of the (secure) MEMCONF registers plus the cached
	 * retention plan; no range to validate. Note: while running (System ON) the
	 * RET register still reads its reset value - the cache (ret_planned) is what
	 * will actually be applied at System OFF.
	 */
	if (args->op == TFM_RAM_CTRL_OP_DUMP) {
		out->control = NRF_MEMCONF->POWER[0].CONTROL;
		out->ret = NRF_MEMCONF->POWER[0].RET;
		out->ret2 = NRF_MEMCONF->POWER[0].RET2;
		out->ret_planned = s_ret_section_mask;
		out->result = 0;
		return TFM_PLATFORM_ERR_SUCCESS;
	}

	/* Allow-list: reject anything not entirely within NS-owned RAM. This
	 * blocks NS from touching secure/KMU/CRACEN/RAM03 sections.
	 */
	if (!ram_ctrl_range_is_ns(args->addr, args->len)) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	switch (args->op) {
	case TFM_RAM_CTRL_OP_POWER: {
		/* System ON power-down/up: write CONTROL immediately. */
		nrfx_ram_ctrl_power_enable_set((void *)(uintptr_t)args->addr, args->len,
					       args->on != 0U);
		break;
	}
	case TFM_RAM_CTRL_OP_RETAIN: {
		/* System OFF retention: record the request in the secure cache. We do
		 * NOT touch RET here - the authoritative apply happens at System OFF,
		 * where we first drop ALL retention (power-saving default) and then
		 * re-enable only the cached sections.
		 */
		uint32_t m = ram_ctrl_range_to_section_mask(args->addr, args->len);

		if (args->on != 0U) {
			s_ret_section_mask |= m;
		} else {
			s_ret_section_mask &= ~m;
		}
		break;
	}
	default:
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	out->result = 0;
	return TFM_PLATFORM_ERR_SUCCESS;
}
#endif /* TFM_NRF_RAM_CTRL_SERVICE */

void tfm_platform_hal_system_reset(void)
{
	/* Reset the system */
	NVIC_SystemReset();
}

#if TFM_NRF_SYSTEM_OFF_SERVICE
enum tfm_platform_err_t tfm_platform_hal_system_off(void)
{
	__disable_irq();

	/* Power-saving default: drop retention for ALL RAM so nothing is retained
	 * across System OFF unless explicitly requested.
	 */
	nrfx_ram_ctrl_retention_enable_all_set(false);

#if TFM_NRF_RAM_CTRL_SERVICE
	/* Re-enable retention only for the sections the non-secure app requested
	 * via nrf_ram_ctrl_svc_retention_set() (accumulated in s_ret_section_mask).
	 * Everything else stays unretained to minimize System OFF current.
	 */
	for (uint32_t i = 0U; i < 32U; i++) {
		if (s_ret_section_mask & (1U << i)) {
			nrfx_ram_ctrl_retention_enable_set(
				(void *)(uintptr_t)(NRF_MEMORY_RAM_BASE + i * RAM_CTRL_SECTION_SIZE),
				RAM_CTRL_SECTION_SIZE, true);
		}
	}
#endif

	nrf_regulators_system_off(NRF_REGULATORS);

	/* This should be unreachable */
	return TFM_PLATFORM_ERR_SYSTEM_ERROR;
}
#endif /* TFM_NRF_SYSTEM_OFF_SERVICE */

#if CONFIG_FW_INFO
static enum tfm_platform_err_t tfm_platform_hal_fw_info_service(psa_invec *in_vec,
								psa_outvec *out_vec)
{
	const struct fw_info *tfm_info;
	struct tfm_fw_info_args_t *args;
	struct tfm_fw_info_out_t *out;
	enum tfm_hal_status_t status;
	enum tfm_platform_err_t err;
	uint32_t attr = TFM_HAL_ACCESS_WRITABLE | TFM_HAL_ACCESS_READABLE | TFM_HAL_ACCESS_NS;
	uintptr_t boundary = (1 << HANDLE_ATTR_NS_POS) & HANDLE_ATTR_NS_MASK;

	if (in_vec->len != sizeof(struct tfm_fw_info_args_t) ||
	    out_vec->len != sizeof(struct tfm_fw_info_out_t)) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	args = (struct tfm_fw_info_args_t *)in_vec->base;
	out = (struct tfm_fw_info_out_t *)out_vec->base;

	/* Assume failure, unless valid region is hit in the loop */
	out->result = -1;
	err = TFM_PLATFORM_ERR_INVALID_PARAM;

	if (args->info == NULL) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	status =
		tfm_hal_memory_check(boundary, (uintptr_t)args->info, sizeof(struct fw_info), attr);
	if (status != TFM_HAL_SUCCESS) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	tfm_info = fw_info_find((uintptr_t)args->fw_address);
	if (tfm_info != NULL) {
		memcpy(args->info, tfm_info, sizeof(struct fw_info));
		out->result = 0;
		err = TFM_PLATFORM_ERR_SUCCESS;
	}

	return err;
}
#endif

#if NRF_ALLOW_NON_SECURE_FAULT_HANDLING
static enum tfm_platform_err_t tfm_platform_hal_ns_fault_service(const psa_invec *in_vec,
								 const psa_outvec *out_vec)
{
	struct tfm_ns_fault_service_args *args;
	struct tfm_ns_fault_service_out *out;
	enum tfm_hal_status_t status;

	uint32_t attr_context =
		TFM_HAL_ACCESS_WRITABLE | TFM_HAL_ACCESS_READABLE | TFM_HAL_ACCESS_NS;

	uint32_t attr_callback =
		TFM_HAL_ACCESS_EXECUTABLE | TFM_HAL_ACCESS_READABLE | TFM_HAL_ACCESS_NS;

	uintptr_t boundary = (1 << HANDLE_ATTR_NS_POS) & HANDLE_ATTR_NS_MASK;

	if (in_vec->len != sizeof(struct tfm_ns_fault_service_args) ||
	    out_vec->len != sizeof(struct tfm_ns_fault_service_out)) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	args = (struct tfm_ns_fault_service_args *)in_vec->base;
	out = (struct tfm_ns_fault_service_out *)out_vec->base;
	out->result = -1;

	if (args->context == NULL || args->callback == NULL) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	status = tfm_hal_memory_check(boundary, (uintptr_t)args->context,
				      sizeof(struct tfm_ns_fault_service_handler_context),
				      attr_context);
	if (status != TFM_HAL_SUCCESS) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	status = tfm_hal_memory_check(boundary, (uintptr_t)args->callback,
				      sizeof(tfm_ns_fault_service_handler_callback), attr_callback);
	if (status != TFM_HAL_SUCCESS) {
		return TFM_PLATFORM_ERR_INVALID_PARAM;
	}

	out->result = ns_fault_service_set_handler(args->context, args->callback);

	return TFM_PLATFORM_ERR_SUCCESS;
}
#endif /* NRF_ALLOW_NON_SECURE_FAULT_HANDLING */

enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request, psa_invec *in_vec,
					       psa_outvec *out_vec)
{
	/* Core IOCTL services */
	switch (request) {
	case TFM_PLATFORM_IOCTL_READ_SERVICE:
		return tfm_platform_hal_read_service(in_vec, out_vec);
	case TFM_PLATFORM_IOCTL_WRITE32_SERVICE:
		return tfm_platform_hal_write32_service(in_vec, out_vec);
#if defined(GPIO_PIN_CNF_MCUSEL_Msk)
	case TFM_PLATFORM_IOCTL_GPIO_SERVICE:
		return tfm_platform_hal_gpio_service(in_vec, out_vec);
#endif /* defined(GPIO_PIN_CNF_MCUSEL_Msk) */
#if TFM_NRF_MRAMC_SERVICE
	case TFM_PLATFORM_IOCTL_MRAMC_INIT_SERVICE:
		return tfm_platform_hal_mramc_init_service();
	case TFM_PLATFORM_IOCTL_MRAMC_SET_WEN_SERVICE:
		return tfm_platform_hal_mramc_set_wen_service(in_vec);
#endif

		/* Board specific IOCTL services */
#if CONFIG_FW_INFO
	case TFM_PLATFORM_IOCTL_FW_INFO:
		return tfm_platform_hal_fw_info_service(in_vec, out_vec);
#endif
#if NRF_ALLOW_NON_SECURE_FAULT_HANDLING
	case TFM_PLATFORM_IOCTL_NS_FAULT:
		return tfm_platform_hal_ns_fault_service(in_vec, out_vec);
#endif
#if TFM_NRF_RAM_CTRL_SERVICE
	case TFM_PLATFORM_IOCTL_RAM_CTRL:
		return tfm_platform_hal_ram_ctrl_service(in_vec, out_vec);
#endif
	/* Not a supported IOCTL service.*/
	default:
		return TFM_PLATFORM_ERR_NOT_SUPPORTED;
	}
}
