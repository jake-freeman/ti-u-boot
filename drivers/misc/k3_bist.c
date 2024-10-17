// SPDX-License-Identifier: GPL-2.0+
/*
 * Texas Instruments' BIST (Built-In Self-Test) driver
 *
 * Copyright (C) 2024 Texas Instruments Incorporated - https://www.ti.com/
 *      Neha Malcom Francis <n-francis@ti.com>
 *
 */

#include <dm.h>
#include <errno.h>
#include <clk.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <asm/arch/hardware.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <remoteproc.h>
#include <power-domain.h>

#include "k3_bist_static_data.h"

/* PBIST Timeout Value */
#define PBIST_MAX_TIMEOUT		10000

/**
 * struct k3_bist_privdata - K3 BIST structure
 * @dev: device pointer
 * @base: base of register set
 * @instance: PBIST instance number
 * @intr_num: corresponding interrupt ID of the PBIST instance
 */
struct k3_bist_privdata {
	struct udevice *dev;
	void *base;
	u32 instance;
	u32 intr_num;
};

static struct k3_bist_privdata *k3_bist_priv;

/**
 * check_mcu_pbist_results() - Check HW POST results
 *
 * Function to check whether HW Power-On Self Test, i.e. POST has run
 * successfully on the MCU domain. HW POST runs in hardware, before the ROM
 * code starts. It can be selected whether to run or not by MCU_BOOTMODE[09:08]
 * pins.
 *
 * Return: 0 if all went fine, else corresponding error.
 */
int check_mcu_pbist_results(void)
{
	bool is_done, timed_out;
	u32 mask;
	u32 post_reg_val, shift;

	/* Read HW POST status register */
	post_reg_val = readl(WKUP_CTRL_MMR0_BASE + WKUP_CTRL_MMR_POST_STAT);

	/* Check if HW POST PBIST was performed */
	shift = WKUP_CTRL_MMR_POST_STAT_PBIST_DONE_SHIFT;
	is_done = (((post_reg_val >> shift) & 0x1u) == 0x1u) ? (bool)true : (bool)false;

	if (!is_done) {
		/* HW POST: PBIST not completed, check if it timed out */
		shift = WKUP_CTRL_MMR_POST_STAT_PBIST_TIMEOUT_SHIFT;
		timed_out = (((post_reg_val >> shift) & 0x1u) == 0x1u) ? (bool)true : (bool)false;

		if (!timed_out) {
			debug("%s: PBIST was not performed at all on this device for this core\n",
			      __func__);
			return -EINVAL;
		} else {
			debug("%s: PBIST was attempted but timed out for this section\n", __func__);
			return -ETIMEDOUT;
		}
	} else {
		/* HW POST: PBIST was completed on this device, check the result */
		mask = WKUP_CTRL_MMR_POST_STAT_PBIST_FAIL_MASK;

		if ((post_reg_val & mask) != 0) {
			debug("%s: PBIST was completed, but the test failed\n", __func__);
			return -EINVAL;
		} else {
			debug("%s: HW POST PBIST completed, test passed\n", __func__);
		}
	}

	return 0;
}

/**
 * configure_processor_state() - Configure processor to correct state
 *
 * Function to configure processor under test to correct state for SW-initiated
 * PBIST
 * @dev: BIST device
 * @index: corresponding index of the core in the cores-under-test list
 * @turnoff: true if core is needed to be turned off
 *
 * Return: 0 if all went fine, else corresponding error.
 */
int configure_processor_state(struct udevice *dev, int index, bool turnoff)
{
	struct ofnode_phandle_args args;
	int ret;
	struct udevice *dev_core;

	ret = dev_read_phandle_with_args(dev, "cores-under-test", NULL, 0, index, &args);
	if (ret) {
		debug("%s: dev_read_phandle_with_args failed: %d\n", __func__,
		      ret);
		return ret;
	}
	ret =  uclass_get_device_by_ofnode(UCLASS_REMOTEPROC, args.node, &dev_core);
	if (ret) {
		debug("%s: uclass_get_device_by_of_offset failed: %d\n",
		      __func__, ret);
		return ret;
	}

	if (turnoff) {
		struct power_domain pwrdmn;
		struct clk fclk;

		ret = power_domain_get_by_index(dev_core, &pwrdmn, 0);
		if (ret) {
			dev_err(dev, "failed to get power domain for the core %d\n", ret);
			return ret;
		}

		ret = clk_get_by_index(dev_core, 0, &fclk);
		if (ret) {
			dev_err(dev, "failed to get clock for the core %d\n", ret);
			return ret;
		}

		ret = power_domain_off(&pwrdmn);
		if (ret) {
			dev_err(dev, "failed to power OFF the core %d\n", ret);
			return ret;
		}

		ret = power_domain_free(&pwrdmn);
		if (ret) {
			dev_err(dev, "failed to free the core %d\n", ret);
			return ret;
		}
		ret = clk_disable(&fclk);
		if (ret) {
			dev_err(dev, "failed to disable clock of the core %d\n", ret);
			return ret;
		}
	}
	return 0;
}

/**
 * pbist_self_test() - Run PBIST_TEST on specified cores
 * @config: pbist_config structure for PBIST test
 *
 * Function to run PBIST_TEST
 *
 * Return: 0 if all went fine, else corresponding error.
 */
int pbist_self_test(struct pbist_config *config)
{
	struct udevice *dev = k3_bist_priv->dev;
	void *base = k3_bist_priv->base;
	u32 intr_num = k3_bist_priv->intr_num;
	bool test_result = true;
	u32 timeout_count = 0;

	/* Turns on PBIST clock in PBIST ACTivate register */
	writel(PBIST_PACT_PACT_MASK, base + PBIST_PACT);

	/* Set Margin mode register for Test mode */
	writel(PBIST_TEST_MODE, base + PBIST_MARGIN_MODE);

	/* Zero out Loop counter 0 */
	writel(0x0, base + PBIST_L0);

	/* Set algorithm bitmap */
	writel(config->algorithms_bit_map, base + PBIST_ALGO);

	/* Set Memory group bitmap */
	writel(config->memory_groups_bit_map, base + PBIST_RINFO);

	/* Zero out override register */
	writel(config->override, base + PBIST_OVER);

	/* Set Scramble value - 64 bit*/
	writel(config->scramble_value_lo, base + PBIST_SCR_LO);
	writel(config->scramble_value_hi, base + PBIST_SCR_HI);

	/* Set DLR register for ROM based testing and Config Access */
	writel(PBIST_DLR_DLR0_ROM_MASK |
		   PBIST_DLR_DLR0_CAM_MASK, base + PBIST_DLR);

	udelay(1000);

	while ((!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) &&
	       (timeout_count++ < PBIST_MAX_TIMEOUT)) {
		udelay(100);
	}

	if (!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) {
		test_result = false;
		debug("%s(dev=%p): test failed\n", __func__, dev);
		return -EINVAL;
	} else {
		debug("%s(dev=%p): test passed\n", __func__, dev);
	}

	writel(0xffffffff, VIM_STS(intr_num));

	return 0;
}

/**
 * pbist_neg_self_test() - Run PBIST_negTEST on specified cores
 * @config: pbist_config_neg structure for PBIST negative test
 *
 * Function to run PBIST failure insertion test
 *
 * Return: 0 if all went fine, else corresponding error.
 */
int pbist_neg_self_test(struct pbist_config_neg *config)
{
	struct udevice *dev = k3_bist_priv->dev;
	void *base = k3_bist_priv->base;
	u32 intr_num = k3_bist_priv->intr_num;
	bool test_result = true;
	u32 timeout_count = 0;

	/* Turns on PBIST clock in PBIST ACTivate register */
	writel(PBIST_PACT_PACT_MASK, base + PBIST_PACT);

	/* Set Margin mode register for Test mode */
	writel(PBIST_FAILURE_INSERTION_TEST_MODE, base + PBIST_MARGIN_MODE);

	/* Zero out Loop counter 0 */
	writel(0x0, base + PBIST_L0);

	/* Set DLR register */
	writel(0x10, base + PBIST_DLR);

	/* Set Registers*/
	writel(config->RF0L, base + PBIST_RF0L);
	writel(config->RF0U, base + PBIST_RF0U);
	writel(config->RF1L, base + PBIST_RF1L);
	writel(config->RF1U, base + PBIST_RF1U);
	writel(config->RF2L, base + PBIST_RF2L);
	writel(config->RF2U, base + PBIST_RF2U);
	writel(config->RF3L, base + PBIST_RF3L);
	writel(config->RF3U, base + PBIST_RF3U);
	writel(config->RF4L, base + PBIST_RF4L);
	writel(config->RF4U, base + PBIST_RF4U);
	writel(config->RF5L, base + PBIST_RF5L);
	writel(config->RF5U, base + PBIST_RF5U);
	writel(config->RF6L, base + PBIST_RF6L);
	writel(config->RF6U, base + PBIST_RF6U);
	writel(config->RF7L, base + PBIST_RF7L);
	writel(config->RF7U, base + PBIST_RF7U);
	writel(config->RF8L, base + PBIST_RF8L);
	writel(config->RF8U, base + PBIST_RF8U);
	writel(config->RF9L, base + PBIST_RF9L);
	writel(config->RF9U, base + PBIST_RF9U);
	writel(config->RF10L, base + PBIST_RF10L);
	writel(config->RF10U, base + PBIST_RF10U);
	writel(config->D, base + PBIST_D);
	writel(config->E, base + PBIST_E);

	writel(config->CA2, base + PBIST_CA2);
	writel(config->CL0, base + PBIST_CL0);
	writel(config->CA3, base + PBIST_CA3);
	writel(config->I0, base + PBIST_I0);
	writel(config->CL1, base + PBIST_CL1);
	writel(config->I3, base + PBIST_I3);
	writel(config->I2, base + PBIST_I2);
	writel(config->CL2, base + PBIST_CL2);
	writel(config->CA1, base + PBIST_CA1);
	writel(config->CA0, base + PBIST_CA0);
	writel(config->CL3, base + PBIST_CL3);
	writel(config->I1, base + PBIST_I1);
	writel(config->RAMT, base + PBIST_RAMT);
	writel(config->CSR, base + PBIST_CSR);
	writel(config->CMS, base + PBIST_CMS);

	writel(0x00000009, base + PBIST_STR);

	/* Start PBIST */
	writel(0x00000001, base + PBIST_STR);

	udelay(1000);

	while ((!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) &&
	       (timeout_count++ < PBIST_MAX_TIMEOUT)) {
		udelay(100);
	}

	if (!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) {
		test_result = false;
		debug("%s(dev=%p): test failed\n", __func__, dev);
		return -EINVAL;
	} else {
		debug("%s(dev=%p): test passed\n", __func__, dev);
	}

	writel(0xffffffff, VIM_STS(intr_num));

	return 0;
}

/**
 * pbist_rom_self_test() - Run PBIST_ROM_TEST on specified cores
 * @config: pbist_config_rom structure for PBIST negative test
 *
 * Function to run PBIST test of ROM
 *
 * Return: 0 if all went fine, else corresponding error.
 */
int pbist_rom_self_test(struct pbist_config_rom *config)
{
	struct udevice *dev = k3_bist_priv->dev;
	void *base = k3_bist_priv->base;
	u32 intr_num = k3_bist_priv->intr_num;
	bool test_result = true;
	u32 timeout_count = 0;

	/* Turns on PBIST clock in PBIST ACTivate register */
	writel(0x1, base + PBIST_PACT);

	/* Set Margin mode register for Test mode */
	writel(0xf, base + PBIST_MARGIN_MODE);

	/* Zero out Loop counter 0 */
	writel(0x0, base + PBIST_L0);

	/* Set DLR register */
	writel(0x310, base + PBIST_DLR);

	/* Set Registers*/
	writel(config->RF0L, base + PBIST_RF0L);
	writel(config->RF0U, base + PBIST_RF0U);
	writel(config->RF1L, base + PBIST_RF1L);
	writel(config->RF1U, base + PBIST_RF1U);
	writel(config->RF2L, base + PBIST_RF2L);
	writel(config->RF2U, base + PBIST_RF2U);
	writel(config->RF3L, base + PBIST_RF3L);
	writel(config->RF3U, base + PBIST_RF3U);
	writel(config->RF4L, base + PBIST_RF4L);
	writel(config->RF4U, base + PBIST_RF4U);

	writel(config->D, base + PBIST_D);
	writel(config->E, base + PBIST_E);
	writel(config->CA2, base + PBIST_CA2);
	writel(config->CL0, base + PBIST_CL0);
	writel(config->CA3, base + PBIST_CA3);
	writel(config->I0, base + PBIST_I0);
	writel(config->CL1, base + PBIST_CL1);
	writel(config->I3, base + PBIST_I3);
	writel(config->I2, base + PBIST_I2);
	writel(config->CL2, base + PBIST_CL2);
	writel(config->CA1, base + PBIST_CA1);
	writel(config->CA0, base + PBIST_CA0);
	writel(config->CL3, base + PBIST_CL3);
	writel(config->I1, base + PBIST_I1);
	writel(config->RAMT, base + PBIST_RAMT);
	writel(config->CSR, base + PBIST_CSR);
	writel(config->CMS, base + PBIST_CMS);

	writel(0x00000009, base + PBIST_STR);

	/* Start PBIST */
	writel(0x00000001, base + PBIST_STR);

	udelay(1000);

	while ((!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) &&
	       (timeout_count++ < PBIST_MAX_TIMEOUT)) {
		udelay(100);
	}

	if (!(readl(VIM_RAW(intr_num)) & VIM_RAW_MASK(intr_num))) {
		test_result = false;
		debug("%s(dev=%p): test failed\n", __func__, dev);
		return -EINVAL;
	} else {
		debug("%s(dev=%p): test passed\n", __func__, dev);
	}

	writel(0xffffffff, VIM_STS(intr_num));

	return 0;
}

/**
 * k3_bist_probe() - Basic probe
 * @dev: corresponding BIST device
 *
 * Parses BIST info from device tree, and configures the module accordingly.
 * Return: 0 if all goes good, else appropriate error message.
 */
static int k3_bist_probe(struct udevice *dev)
{
	int ret = 0, i, j;
	struct k3_bist_privdata *priv = dev_get_priv(dev);
	struct pbist_inst_info *info;

	int num_runs;

	debug("%s(dev=%p)\n", __func__, dev);

	priv = dev_get_priv(dev);
	if (!priv)
		return -ENODEV;

	priv->dev = dev;

	k3_bist_priv = priv;

	ret = dev_read_u32(dev, "ti,bist-instance", &priv->instance);
	if (!priv->instance)
		return -ENODEV;

	/* Check whether HW POST successfully completely PBIST on the MCU domain
	 * MCU_BOOTMODE[09:08] indicates whether HW POST was bypassed or not
	 */
	ret = readl(CTRL_MMR_CFG0_WKUP_BOOTCFG);
	if (((ret & WKUP_BOOTCFG_MCU_BOOTMODE_POST_MASK) >>
		  WKUP_BOOTCFG_MCU_BOOTMODE_POST_SHIFT) != 0x3) {
		ret = check_mcu_pbist_results();
		if (ret) {
			dev_err(dev, "HW POST failed to run successfully %d\n", ret);
			hang();
		}
	} else {
		dev_err(dev, "HW POST was bypassed, check MCU_BOOTMODE[09:08]\n");
		return -EINVAL;
	}

	priv->base = dev_remap_addr_index(dev, 0);
	if (!priv->base)
		return -ENODEV;

	/* Probe the cores under test */
	for (i = 0; ; i++) {
		ret = configure_processor_state(dev, i, false);
		if (ret)
			break;
	}

	if (!i) {
		dev_err(dev, "%s: Acquiring the core failed. ret = %d\n", __func__, ret);
		return ret;
	}

	switch (priv->instance) {
	case PBIST14_INSTANCE:
		info = &pbist14_inst_info;
		priv->intr_num = info->intr_num;
		break;
	default:
		dev_err(dev, "%s: PBIST instance %d not supported\n", __func__, priv->instance);
		return -ENODEV;
	};

	/* Run PBIST test */
	num_runs = info->num_pbist_runs;

	for (j = 0; j < num_runs; j++) {
		ret = pbist_self_test(&info->pbist_config_run[j]);
		if (ret) {
			dev_err(dev, "PBIST test failed %d\n", ret);
			hang();
		}
	}

	/* Run PBIST failure insertion test */
	ret = pbist_neg_self_test(&info->pbist_neg_config_run);
	if (ret) {
		dev_err(dev, "PBIST negative test failed%d\n", ret);
		hang();
	}

	/* Run PBIST test on ROM */
	num_runs = info->num_pbist_rom_test_runs;

	for (j = 0; j < num_runs; j++) {
		ret = pbist_rom_self_test(&info->pbist_rom_test_config_run[j]);
		if (ret) {
			dev_err(dev, "ROM PBIST test failed%d\n", ret);
			hang();
		}
	}

	/* Power off cores under test */
	while (i--) {
		ret = configure_processor_state(dev, i, true);
		if (ret)
			break;
	}

	if (i) {
		dev_err(dev, "%s: Stopping the core failed. ret = %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static const struct udevice_id k3_bist_ids[] = {
	{ .compatible = "ti,j784s4-bist" },
	{}
};

U_BOOT_DRIVER(k3_bist) = {
	.name = "k3_bist",
	.of_match = k3_bist_ids,
	.id = UCLASS_MISC,
	.probe = k3_bist_probe,
	.priv_auto = sizeof(struct k3_bist_privdata),
};
