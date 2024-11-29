/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2024 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef _MACH_LPM_H_
#define _MACH_LPM_H_

#include <linux/compiler_attributes.h>
#include <stdbool.h>

#if IS_ENABLED(CONFIG_K3_IODDR)
bool wkup_ctrl_is_lpm_exit(void);
void wkup_ctrl_ddrss_pmctrl_deassert_retention(void);
#else
static bool __maybe_unused wkup_ctrl_is_lpm_exit(void)
{
	return false;
}

static void __maybe_unused wkup_ctrl_ddrss_pmctrl_deassert_retention(void) {}
#endif

#endif /* _MACH_LPM_H_ */
