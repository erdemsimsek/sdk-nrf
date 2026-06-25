/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <tfm_platform_api.h>
#include <tfm_ioctl_api.h>

#include <zephyr/autoconf.h>
#include <errno.h>

static int status2err(enum tfm_platform_err_t status, uint32_t result)
{
	switch (status) {
	case TFM_PLATFORM_ERR_INVALID_PARAM:
		return -EINVAL;
	case TFM_PLATFORM_ERR_NOT_SUPPORTED:
		return -ENOTSUP;
	case TFM_PLATFORM_ERR_SUCCESS:
		if (result == 0) {
			return 0;
		}
		/* Fallthrough */
	default:
		return -EPERM;
	}

	if (result != 0) {
		return -EINVAL;
	}

	return 0;
}

#if CONFIG_FW_INFO
int tfm_platform_firmware_info(uint32_t fw_address, struct fw_info *info)
{
	enum tfm_platform_err_t ret;
	psa_invec in_vec;
	psa_outvec out_vec;
	struct tfm_fw_info_args_t args;
	struct tfm_fw_info_out_t out;

	in_vec.base = (const void *)&args;
	in_vec.len = sizeof(args);

	out_vec.base = (void *)&out;
	out_vec.len = sizeof(out);

	args.fw_address = (void *)fw_address;
	args.info = info;

	ret = tfm_platform_ioctl(TFM_PLATFORM_IOCTL_FW_INFO, &in_vec, &out_vec);

	return status2err(ret, out.result);
}

/* Don't include the function if it is mocked for tests */
#ifndef CONFIG_MOCK_TFM_PLATFORM_S0_FUNCTIONS
int tfm_platform_s0_active(uint32_t s0_address, uint32_t s1_address, bool *s0_active)
{
	struct fw_info s0, s1;
	bool s0_valid, s1_valid;
	int err;

	err = tfm_platform_firmware_info(s0_address, &s0);
	s0_valid = err == 0 && s0.valid == CONFIG_FW_INFO_VALID_VAL;

	err = tfm_platform_firmware_info(s1_address, &s1);
	s1_valid = err == 0 && s1.valid == CONFIG_FW_INFO_VALID_VAL;

	if (!s1_valid && !s0_valid) {
		return -EINVAL;
	} else if (!s1_valid) {
		*s0_active = true;
	} else if (!s0_valid) {
		*s0_active = false;
	} else {
		*s0_active = s0.version >= s1.version;
	}

	return 0;
}
#endif /* not defined(CONFIG_MOCK_TFM_PLATFORM_S0_FUNCTIONS) */
#endif

int tfm_platform_ns_fault_set_handler(struct tfm_ns_fault_service_handler_context *context,
				      tfm_ns_fault_service_handler_callback callback)
{
	enum tfm_platform_err_t ret;
	psa_invec in_vec;
	psa_outvec out_vec;
	struct tfm_ns_fault_service_args args;
	struct tfm_ns_fault_service_out out;

	in_vec.base = (const void *)&args;
	in_vec.len = sizeof(args);

	out_vec.base = (void *)&out;
	out_vec.len = sizeof(out);

	args.context = context;
	args.callback = callback;

	ret = tfm_platform_ioctl(TFM_PLATFORM_IOCTL_NS_FAULT, &in_vec, &out_vec);

	return status2err(ret, out.result);
}

static int ram_ctrl_set(enum tfm_ram_ctrl_op op, uintptr_t addr, size_t len, bool on)
{
	enum tfm_platform_err_t ret;
	psa_invec in_vec;
	psa_outvec out_vec;
	struct tfm_ram_ctrl_args_t args;
	struct tfm_ram_ctrl_out_t out;

	in_vec.base = (const void *)&args;
	in_vec.len = sizeof(args);

	out_vec.base = (void *)&out;
	out_vec.len = sizeof(out);

	args.op = (uint32_t)op;
	args.addr = (uint32_t)addr;
	args.len = (uint32_t)len;
	args.on = (uint32_t)on;

	ret = tfm_platform_ioctl(TFM_PLATFORM_IOCTL_RAM_CTRL, &in_vec, &out_vec);

	return status2err(ret, out.result);
}

int nrf_ram_ctrl_svc_power_set(uintptr_t addr, size_t len, bool on)
{
	return ram_ctrl_set(TFM_RAM_CTRL_OP_POWER, addr, len, on);
}

int nrf_ram_ctrl_svc_retention_set(uintptr_t addr, size_t len, bool on)
{
	return ram_ctrl_set(TFM_RAM_CTRL_OP_RETAIN, addr, len, on);
}

int nrf_ram_ctrl_svc_dump(uint32_t *control, uint32_t *ret, uint32_t *ret2,
			  uint32_t *ret_planned)
{
	enum tfm_platform_err_t status;
	psa_invec in_vec;
	psa_outvec out_vec;
	struct tfm_ram_ctrl_args_t args;
	struct tfm_ram_ctrl_out_t out = {0};
	int err;

	in_vec.base = (const void *)&args;
	in_vec.len = sizeof(args);
	out_vec.base = (void *)&out;
	out_vec.len = sizeof(out);

	args.op = (uint32_t)TFM_RAM_CTRL_OP_DUMP;
	args.addr = 0;
	args.len = 0;
	args.on = 0;

	status = tfm_platform_ioctl(TFM_PLATFORM_IOCTL_RAM_CTRL, &in_vec, &out_vec);
	err = status2err(status, out.result);
	if (err == 0) {
		if (control != NULL) {
			*control = out.control;
		}
		if (ret != NULL) {
			*ret = out.ret;
		}
		if (ret2 != NULL) {
			*ret2 = out.ret2;
		}
		if (ret_planned != NULL) {
			*ret_planned = out.ret_planned;
		}
	}

	return err;
}
