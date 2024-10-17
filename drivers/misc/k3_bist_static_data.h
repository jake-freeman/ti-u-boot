/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Static Data for Texas Instruments' BIST (Built-In Self-Test) driver
 *
 * Copyright (C) 2024 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __K3_BIST_STATIC_DATA_H
#define __K3_BIST_STATIC_DATA_H

#define PBIST_NUM_RUNS	2
#define PBIST_ROM_NUM_RUNS 13
#define PBIST14_DFT_PBIST_CPU_0_INTR_NUM 311

/* VIM Registers */
#define VIM_STS_BASE			0x40f80404
#define VIM_RAW_BASE			0x40f80400

#define VIM_STS(i)		(VIM_STS_BASE + (i) / 32 * 0x20)
#define VIM_RAW(i)		(VIM_RAW_BASE + (i) / 32 * 0x20)
#define VIM_RAW_MASK(i)		(BIT((i) % 32))

/* PBIST Registers and Flags*/
#define PBIST_RF0L			0x00000000
#define PBIST_RF1L			0x00000004
#define PBIST_RF2L			0x00000008
#define PBIST_RF3L			0x0000000C
#define PBIST_RF4L			0x0000010
#define PBIST_RF5L			0x0000014
#define PBIST_RF6L			0x0000018
#define PBIST_RF7L			0x000001C
#define PBIST_RF8L			0x0000020
#define PBIST_RF9L			0x0000024
#define PBIST_RF10L			0x0000028
#define PBIST_RF11L			0x000002C
#define PBIST_RF12L			0x0000030
#define PBIST_RF13L			0x0000034
#define PBIST_RF14L			0x0000038
#define PBIST_RF15L			0x000003C
#define PBIST_RF0U			0x0000040
#define PBIST_RF1U			0x0000044
#define PBIST_RF2U			0x0000048
#define PBIST_RF3U			0x000004C
#define PBIST_RF4U			0x0000050
#define PBIST_RF5U			0x0000054
#define PBIST_RF6U			0x0000058
#define PBIST_RF7U			0x000005C
#define PBIST_RF8U			0x0000060
#define PBIST_RF9U			0x0000064
#define PBIST_RF10U			0x0000068
#define PBIST_RF11U			0x000006C
#define PBIST_RF12U			0x0000070
#define PBIST_RF13U			0x0000074
#define PBIST_RF14U			0x0000078
#define PBIST_RF15U			0x000007C
#define PBIST_A0			0x0000100
#define PBIST_A1			0x0000104
#define PBIST_A2			0x0000108
#define PBIST_A3			0x000010C
#define PBIST_L0			0x0000110
#define PBIST_L1			0x0000114
#define PBIST_L2			0x0000118
#define PBIST_L3			0x000011C
#define PBIST_D				0x0000120
#define PBIST_E				0x0000124
#define PBIST_CA0			0x0000130
#define PBIST_CA1			0x0000134
#define PBIST_CA2			0x0000138
#define PBIST_CA3			0x000013C
#define PBIST_CL0			0x0000140
#define PBIST_CL1			0x0000144
#define PBIST_CL2			0x0000148
#define PBIST_CL3			0x000014C
#define PBIST_I0			0x0000150
#define PBIST_I1			0x0000154
#define PBIST_I2			0x0000158
#define PBIST_I3			0x000015C
#define PBIST_RAMT			0x0000160
#define PBIST_DLR			0x0000164
#define PBIST_CMS			0x0000168
#define PBIST_STR			0x000016C
#define PBIST_SCR			0x0000170
#define PBIST_SCR_LO			0x0000170
#define PBIST_SCR_HI			0x0000174
#define PBIST_CSR			0x0000178
#define PBIST_FDLY			0x000017C
#define PBIST_PACT			0x0000180
#define PBIST_PID			0x0000184
#define PBIST_OVER			0x0000188
#define PBIST_FSPBIST_RF		0x0000190
#define PBIST_FSRC			0x0000198
#define PBIST_FSRA			0x00001A0
#define PBIST_FSRDL0			0x00001A8
#define PBIST_FSRDL1			0x00001B0
#define PBIST_MARGIN_MODE		0x00001B4
#define PBIST_WRENZ			0x00001B8
#define PBIST_PAGE_PGS			0x00001BC
#define PBIST_ROM			0x00001C0
#define PBIST_ALGO			0x00001C4
#define PBIST_RINFO			0x00001C8

#define PBIST_MARGIN_MODE_WRITE_MASK	0x00000003
#define PBIST_MARGIN_MODE_READ_SHIFT	0x00000002
#define PBIST_MARGIN_MODE_READ_MASK	0x0000000C
#define PBIST_PACT_PACT_MASK		0x00000001
#define PBIST_DLR_DLR0_ROM_MASK		0x00000004
#define PBIST_DLR_DLR0_CAM_MASK		0x00000010
#define PBIST_NOT_DONE			0
#define PBIST_DONE			1

/* PBIST test mode */
#define PBIST_TEST_MODE (PBIST_MARGIN_MODE_WRITE_MASK \
			 | (1 << PBIST_MARGIN_MODE_READ_SHIFT))

/* PBIST Failure Insertion test mode */
#define PBIST_FAILURE_INSERTION_TEST_MODE (PBIST_MARGIN_MODE_WRITE_MASK \
					   | PBIST_MARGIN_MODE_READ_MASK)

/*
 * struct pbist_config - Structure for different configuration used for PBIST
 * @override: Override value for memory configuration
 * @algorithms_bit_map: Bitmap to select algorithms to use for test
 * @memory_groups_bit_map: Bitmap to select memory groups to run test on
 * @scramble_value_lo: Lower scramble value to be used for test
 * @scramble_value_hi: Higher scramble value to be used for test
 */
struct pbist_config {
	u32 override;
	u32 algorithms_bit_map;
	u64 memory_groups_bit_map;
	u32 scramble_value_lo;
	u32 scramble_value_hi;
};

/*
 * struct pbist_config_neg - Structure for different configuration used for PBIST
 * for the failure insertion test to generate negative result
 * @CA0: Failure insertion value for CA0
 * @CA1: Failure insertion value for CA1
 * @CA2: Failure insertion value for CA2
 * @CA3: Failure insertion value for CA3
 * @CL0: Failure insertion value for CL0
 * @CL1: Failure insertion value for CL1
 * @CL2: Failure insertion value for CL2
 * @CL3: Failure insertion value for CL3
 * @CMS: Failure insertion value for CMS
 * @CSR: Failure insertion value for CSR
 * @I0: Failure insertion value for I0
 * @I1: Failure insertion value for I1
 * @I2: Failure insertion value for I2
 * @I3: Failure insertion value for I3
 * @RAMT: Failure insertion value for RAMT
 * @RF0L: Failure insertion value for RF0L
 * @RF0U: Failure insertion value for RF0U
 * @RF1L: Failure insertion value for RF1L
 * @RF1U: Failure insertion value for RF1U
 * @RF2L: Failure insertion value for RF2L
 * @RF2U: Failure insertion value for RF2U
 * @RF3L: Failure insertion value for RF3L
 * @RF3U: Failure insertion value for RF3U
 * @RF4L: Failure insertion value for RF4L
 * @RF4U: Failure insertion value for RF4U
 * @RF5L: Failure insertion value for RF5L
 * @RF5U: Failure insertion value for RF5U
 * @RF6L: Failure insertion value for RF6L
 * @RF6U: Failure insertion value for RF6U
 * @RF7L: Failure insertion value for RF7L
 * @RF7U: Failure insertion value for RF7U
 * @RF8L: Failure insertion value for RF8L
 * @RF8U: Failure insertion value for RF8U
 * @RF9L: Failure insertion value for RF9L
 * @RF9U: Failure insertion value for RF9U
 * @RF10L: Failure insertion value for RF10L
 * @RF10U: Failure insertion value for RF10U
 * @D: Failure insertion value for D
 * @E: Failure insertion value for E
 */
struct pbist_config_neg {
	u32 CA0;
	u32 CA1;
	u32 CA2;
	u32 CA3;
	u32 CL0;
	u32 CL1;
	u32 CL2;
	u32 CL3;
	u32 CMS;
	u32 CSR;
	u32 I0;
	u32 I1;
	u32 I2;
	u32 I3;
	u32 RAMT;
	u32 RF0L;
	u32 RF0U;
	u32 RF1L;
	u32 RF1U;
	u32 RF2L;
	u32 RF2U;
	u32 RF3L;
	u32 RF3U;
	u32 RF4L;
	u32 RF4U;
	u32 RF5L;
	u32 RF5U;
	u32 RF6L;
	u32 RF6U;
	u32 RF7L;
	u32 RF7U;
	u32 RF8L;
	u32 RF8U;
	u32 RF9L;
	u32 RF9U;
	u32 RF10L;
	u32 RF10U;
	u32 D;
	u32 E;
};

/*
 * struct pbist_config_rom - Structure for different configuration used for PBIST
 * test of ROM
 * @D: ROM test value for D
 * @E: ROM test value for E
 * @CA2: ROM test value for CA2
 * @CL0: ROM test value for CL0
 * @CA3: ROM test value for CA3
 * @I0: ROM test value for I0
 * @CL1: ROM test value for CL1
 * @I3: ROM test value for I3
 * @I2: ROM test value for I2
 * @CL2: ROM test value for CL2
 * @CA1: ROM test value for CA1
 * @CA0: ROM test value for CA0
 * @CL3: ROM test value for CL3
 * @I1: ROM test value for I1
 * @RAMT: ROM test value for RAMT
 * @CSR: ROM test value for CSR
 * @CMS: ROM test value for CMS
 * @RF0L: Rom test value for RF0L
 * @RF0U: Rom test value for RF0U
 * @RF1L: Rom test value for RF1L
 * @RF1U: Rom test value for RF1U
 * @RF2L: Rom test value for RF2L
 * @RF2U: Rom test value for RF2U
 * @RF3L: Rom test value for RF3L
 * @RF3U: Rom test value for RF3U
 * @RF4L: Rom test value for RF4L
 * @RF4U: Rom test value for RF4U
 */
struct pbist_config_rom {
	u32 D;
	u32 E;
	u32 CA2;
	u32 CL0;
	u32 CA3;
	u32 I0;
	u32 CL1;
	u32 I3;
	u32 I2;
	u32 CL2;
	u32 CA1;
	u32 CA0;
	u32 CL3;
	u32 I1;
	u32 RAMT;
	u32 CSR;
	u32 CMS;
	u32 RF0L;
	u32 RF0U;
	u32 RF1L;
	u32 RF1U;
	u32 RF2L;
	u32 RF2U;
	u32 RF3L;
	u32 RF3U;
	u32 RF4L;
	u32 RF4U;
};

/*
 * struct pbist_inst_info - Structure for different configuration used for PBIST
 * @num_pbist_runs: Number of runs of PBIST test
 * @intr_num: Interrupt number triggered by this PBIST instance to MCU R5 VIM
 * @pbist_config_run: Configuration for PBIST test
 * @pbist_neg_config_run: Configuration for PBIST negative test
 * @num_pbist_rom_test_runs: Number of runs of PBIST test on ROM
 * @pbist_rom_test_config_run: Configuration for PBIST test on ROM
 */
struct pbist_inst_info {
	u32 num_pbist_runs;
	u32 intr_num;
	struct pbist_config pbist_config_run[PBIST_NUM_RUNS];
	struct pbist_config_neg pbist_neg_config_run;
	u32 num_pbist_rom_test_runs;
	struct pbist_config_rom pbist_rom_test_config_run[PBIST_ROM_NUM_RUNS];
};

#if IS_ENABLED(CONFIG_SOC_K3_J784S4)

static struct pbist_inst_info pbist14_inst_info = {
	/* Main Pulsar 2 Instance 1 or MAIN_R52_x */
	.num_pbist_runs = 1,
	.intr_num = PBIST14_DFT_PBIST_CPU_0_INTR_NUM,
	.pbist_config_run = {
		{
			.override = 0,
			.algorithms_bit_map = PBIST14_ALGO_BITMAP_0,
			.memory_groups_bit_map = PBIST14_MEM_BITMAP_0,
			.scramble_value_lo = 0x76543210,
			.scramble_value_hi = 0xFEDCBA98,
		},
		{
			.override = 0,
			.algorithms_bit_map = 0,
			.memory_groups_bit_map = 0,
			.scramble_value_lo = 0,
			.scramble_value_hi = 0,
		},
	},
	.pbist_neg_config_run = {
		.CA0 = PBIST14_FAIL_VECTOR_CA0,
		.CA1 = PBIST14_FAIL_VECTOR_CA1,
		.CA2 = PBIST14_FAIL_VECTOR_CA2,
		.CA3 = PBIST14_FAIL_VECTOR_CA3,
		.CL0 = PBIST14_FAIL_VECTOR_CL0,
		.CL1 = PBIST14_FAIL_VECTOR_CL1,
		.CL2 = PBIST14_FAIL_VECTOR_CL2,
		.CL3 = PBIST14_FAIL_VECTOR_CL3,
		.CMS = PBIST14_FAIL_VECTOR_CMS,
		.CSR = PBIST14_FAIL_VECTOR_CSR,
		.I0 = PBIST14_FAIL_VECTOR_I0,
		.I1 = PBIST14_FAIL_VECTOR_I1,
		.I2 = PBIST14_FAIL_VECTOR_I2,
		.I3 = PBIST14_FAIL_VECTOR_I3,
		.RAMT = PBIST14_FAIL_VECTOR_RAMT,
		.RF0L = PBIST14_FAIL_VECTOR_RF0L,
		.RF0U = PBIST14_FAIL_VECTOR_RF0U,
		.RF1L = PBIST14_FAIL_VECTOR_RF1L,
		.RF1U = PBIST14_FAIL_VECTOR_RF1U,
		.RF2L = PBIST14_FAIL_VECTOR_RF2L,
		.RF2U = PBIST14_FAIL_VECTOR_RF2U,
		.RF3L = PBIST14_FAIL_VECTOR_RF3L,
		.RF3U = PBIST14_FAIL_VECTOR_RF3U,
		.RF4L = PBIST14_FAIL_VECTOR_RF4L,
		.RF4U = PBIST14_FAIL_VECTOR_RF4U,
		.RF5L = PBIST14_FAIL_VECTOR_RF5L,
		.RF5U = PBIST14_FAIL_VECTOR_RF5U,
		.RF6L = PBIST14_FAIL_VECTOR_RF6L,
		.RF6U = PBIST14_FAIL_VECTOR_RF6U,
		.RF7L = PBIST14_FAIL_VECTOR_RF7L,
		.RF7U = PBIST14_FAIL_VECTOR_RF7U,
		.RF8L = PBIST14_FAIL_VECTOR_RF8L,
		.RF8U = PBIST14_FAIL_VECTOR_RF8U,
		.RF9L = PBIST14_FAIL_VECTOR_RF9L,
		.RF9U = PBIST14_FAIL_VECTOR_RF9U,
		.RF10L = PBIST14_FAIL_VECTOR_RF10L,
		.RF10U = PBIST14_FAIL_VECTOR_RF10U,
		.D = PBIST14_FAIL_VECTOR_D,
		.E = PBIST14_FAIL_VECTOR_E
	},
	.num_pbist_rom_test_runs = 1,
	.pbist_rom_test_config_run = {
		{
			.D = PBIST14_ROM_VECTOR_D,
			.E = PBIST14_ROM_VECTOR_E,
			.CA2 = PBIST14_ROM_VECTOR_CA2,
			.CL0 = PBIST14_ROM_VECTOR_CL0,
			.CA3 = PBIST14_ROM_VECTOR_CA3,
			.I0 = PBIST14_ROM_VECTOR_I0,
			.CL1 = PBIST14_ROM_VECTOR_CL1,
			.I3 = PBIST14_ROM_VECTOR_I3,
			.I2 = PBIST14_ROM_VECTOR_I2,
			.CL2 = PBIST14_ROM_VECTOR_CL2,
			.CA1 = PBIST14_ROM_VECTOR_CA1,
			.CA0 = PBIST14_ROM_VECTOR_CA0,
			.CL3 = PBIST14_ROM_VECTOR_CL3,
			.I1 = PBIST14_ROM_VECTOR_I1,
			.RAMT = PBIST14_ROM_VECTOR_RAMT,
			.CSR = PBIST14_ROM_VECTOR_CSR,
			.CMS = PBIST14_ROM_VECTOR_CMS
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
		{
			.D = 0x0,
			.E = 0x0,
			.CA2 = 0x0,
			.CL0 = 0x0,
			.CA3 = 0x0,
			.I0 = 0x0,
			.CL1 = 0x0,
			.I3 = 0x0,
			.I2 = 0x0,
			.CL2 = 0x0,
			.CA1 = 0x0,
			.CA0 = 0x0,
			.CL3 = 0x0,
			.I1 = 0x0,
			.RAMT = 0x0,
			.CSR = 0x0,
			.CMS = 0x0
		},
	},
};

#endif /* CONFIG_SOC_K3_J784S4 */
#endif /* __TI_SCI_STATIC_DATA_H */
