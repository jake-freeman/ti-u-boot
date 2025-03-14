// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012
 * Altera Corporation <www.altera.com>
 */

#include <common.h>
#include <clk.h>
#include <log.h>
#include <dm.h>
#include <fdtdec.h>
#include <malloc.h>
#include <reset.h>
#include <spi.h>
#include <spi-mem.h>
#include <dm/device_compat.h>
#include <linux/err.h>
#include <thermal.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/time.h>
#include <zynqmp_firmware.h>
#include "cadence_qspi.h"
#include <dt-bindings/power/xlnx-versal-power.h>

#define CQSPI_STIG_READ			0
#define CQSPI_STIG_WRITE		1
#define CQSPI_READ			2
#define CQSPI_WRITE			3

__weak int cadence_qspi_apb_dma_read(struct cadence_spi_priv *priv,
				     const struct spi_mem_op *op)
{
	return 0;
}

__weak int cadence_qspi_versal_flash_reset(struct udevice *dev)
{
	return 0;
}

__weak ofnode cadence_qspi_get_subnode(struct udevice *dev)
{
	return dev_read_first_subnode(dev);
}

static int cadence_spi_get_temp(int *temp)
{
	struct udevice *dev;
	int ret;

	ret = uclass_first_device_err(UCLASS_THERMAL, &dev);
	if (ret)
		return ret;

	ret = thermal_get_temp(dev, temp);
	if (ret)
		return ret;

	/* The temperature we get is in milicelsius. Change it to Celsius. */
	*temp /= 1000;

	return 0;
}

static int cadence_spi_phy_apply_setting(struct cadence_spi_priv *priv,
					 struct phy_setting *phy)
{
	unsigned int reg;

	reg = readl(priv->regbase + CQSPI_REG_RD_DATA_CAPTURE);
	reg |= CQSPI_REG_RD_DATA_CAPTURE_SMPL_EDGE;
	writel(reg, priv->regbase + CQSPI_REG_RD_DATA_CAPTURE);

	cadence_qspi_apb_set_rx_dll(priv->regbase, phy->rx);
	cadence_qspi_apb_set_tx_dll(priv->regbase, phy->tx);
	priv->phy_read_delay = phy->read_delay;

	return cadence_qspi_apb_resync_dll(priv->regbase);
}

static int cadence_spi_phy_check_pattern(struct cadence_spi_priv *priv,
					 struct spi_slave *spi)
{
	struct spi_mem_op op = priv->phy_read_op;
	u8 *read_data;
	int ret;

	read_data = kmalloc(ARRAY_SIZE(phy_tuning_pattern), 0);
	if (!read_data)
		return -ENOMEM;

	op.data.buf.in = read_data;
	op.addr.val = priv->phy_pattern_start;
	op.data.nbytes = ARRAY_SIZE(phy_tuning_pattern);

	ret = spi_mem_exec_op(spi, &op);
	if (ret)
		goto out;

	if (memcmp(read_data, phy_tuning_pattern,
		   ARRAY_SIZE(phy_tuning_pattern))) {
		ret = -EAGAIN;
		goto out;
	}

	ret = 0;
out:
	kfree(read_data);
	return ret;
}

static int cadence_spi_find_rx_low(struct cadence_spi_priv *priv,
				   struct spi_slave *spi,
				   struct phy_setting *phy)
{
	int ret;

	do {
		phy->rx = CQSPI_PHY_RX_LOW_SEARCH_START;
		do {
			ret = cadence_spi_phy_apply_setting(priv, phy);
			if (!ret) {
				ret = cadence_spi_phy_check_pattern(priv, spi);
				if (!ret)
					return 0;
			}
			phy->rx += CQSPI_PHY_DDR_SEARCH_STEP;
		} while (phy->rx <= CQSPI_PHY_RX_LOW_SEARCH_END);

		phy->read_delay++;
	} while (phy->read_delay <= CQSPI_PHY_MAX_RD);

	debug("Unable to find RX low\n");
	return -ENOENT;
}

static int cadence_spi_find_rx_low_sdr(struct cadence_spi_priv *priv,
				       struct spi_slave *spi,
				       struct phy_setting *phy)
{
	int ret;

	phy->rx = 0;
	do {
		ret = cadence_spi_phy_apply_setting(priv, phy);
		if (!ret) {
			ret = cadence_spi_phy_check_pattern(priv, spi);
			if (!ret)
				return 0;
		}
		phy->rx++;
	} while (phy->rx < CQSPI_PHY_MAX_DELAY - 1);

	debug("Unable to find RX low\n");
	return -ENOENT;
}

static int cadence_spi_find_rx_high(struct cadence_spi_priv *priv,
				    struct spi_slave *spi,
				    struct phy_setting *phy)
{
	int ret;

	do {
		phy->rx = CQSPI_PHY_RX_HIGH_SEARCH_END;
		do {
			ret = cadence_spi_phy_apply_setting(priv, phy);
			if (!ret) {
				ret = cadence_spi_phy_check_pattern(priv, spi);
				if (!ret)
					return 0;
			}
			phy->rx -= CQSPI_PHY_DDR_SEARCH_STEP;
		} while (phy->rx >= CQSPI_PHY_RX_HIGH_SEARCH_START);

		phy->read_delay--;
	} while (phy->read_delay >= CQSPI_PHY_INIT_RD);

	debug("Unable to find RX high\n");
	return -ENOENT;
}

static int cadence_spi_find_rx_high_sdr(struct cadence_spi_priv *priv,
					struct spi_slave *spi,
					struct phy_setting *phy,
					u8 lowerbound)
{
	int ret;

	phy->rx = CQSPI_PHY_MAX_DELAY;
	do {
		ret = cadence_spi_phy_apply_setting(priv, phy);
		if (!ret) {
			ret = cadence_spi_phy_check_pattern(priv, spi);
			if (!ret)
				return 0;
		}
		phy->rx--;
	} while (phy->rx > lowerbound);

	debug("Unable to find RX high\n");
	return -ENOENT;
}

static int cadence_spi_find_tx_low(struct cadence_spi_priv *priv,
				   struct spi_slave *spi,
				   struct phy_setting *phy)
{
	int ret;

	do {
		phy->tx = CQSPI_PHY_TX_LOW_SEARCH_START;
		do {
			ret = cadence_spi_phy_apply_setting(priv, phy);
			if (!ret) {
				ret = cadence_spi_phy_check_pattern(priv, spi);
				if (!ret)
					return 0;
			}
			phy->tx += CQSPI_PHY_DDR_SEARCH_STEP;
		} while (phy->tx <= CQSPI_PHY_TX_LOW_SEARCH_END);

		phy->read_delay++;
	} while (phy->read_delay <= CQSPI_PHY_MAX_RD);

	debug("Unable to find TX low\n");
	return -ENOENT;
}

static int cadence_spi_find_tx_high(struct cadence_spi_priv *priv,
				    struct spi_slave *spi,
				    struct phy_setting *phy)
{
	int ret;

	do {
		phy->tx = CQSPI_PHY_TX_HIGH_SEARCH_END;
		do {
			ret = cadence_spi_phy_apply_setting(priv, phy);
			if (!ret) {
				ret = cadence_spi_phy_check_pattern(priv, spi);
				if (!ret)
					return 0;
			}
			phy->tx -= CQSPI_PHY_DDR_SEARCH_STEP;
		} while (phy->tx >= CQSPI_PHY_TX_HIGH_SEARCH_START);

		phy->read_delay--;
	} while (phy->read_delay >= CQSPI_PHY_INIT_RD);

	debug("Unable to find TX high\n");
	return -ENOENT;
}

static int cadence_spi_phy_find_gaplow(struct cadence_spi_priv *priv,
				       struct spi_slave *spi,
				       struct phy_setting *bottomleft,
				       struct phy_setting *topright,
				       struct phy_setting *gaplow)
{
	struct phy_setting left, right, mid;
	int ret;

	left = *bottomleft;
	right = *topright;

	mid.tx = left.tx + ((right.tx - left.tx) / 2);
	mid.rx = left.rx + ((right.rx - left.rx) / 2);
	mid.read_delay = left.read_delay;

	do {
		ret = cadence_spi_phy_apply_setting(priv, &mid);
		if (!ret)
			ret = cadence_spi_phy_check_pattern(priv, spi);

		if (ret) {
			/*
			 * Since we couldn't find the pattern, we need to go to
			 * the lower half.
			 */
			right.tx = mid.tx;
			right.rx = mid.rx;

			mid.tx = left.tx + ((mid.tx - left.tx) / 2);
			mid.rx = left.rx + ((mid.rx - left.rx) / 2);
		} else {
			/*
			 * Since we found the pattern, we need to go the upper
			 * half.
			 */
			left.tx = mid.tx;
			left.rx = mid.rx;

			mid.tx = mid.tx + ((right.tx - mid.tx) / 2);
			mid.rx = mid.rx + ((right.rx - mid.rx) / 2);
		}

	/* Break the loop if the window has closed. */
	} while ((right.tx - left.tx >= 2) && (right.rx - left.rx >= 2));

	*gaplow = mid;
	return 0;
}

static int cadence_spi_phy_find_gaphigh(struct cadence_spi_priv *priv,
					struct spi_slave *spi,
					struct phy_setting *bottomleft,
					struct phy_setting *topright,
					struct phy_setting *gaphigh)
{
	struct phy_setting left, right, mid;
	int ret;

	left = *bottomleft;
	right = *topright;

	mid.tx = left.tx + ((right.tx - left.tx) / 2);
	mid.rx = left.rx + ((right.rx - left.rx) / 2);
	mid.read_delay = right.read_delay;

	do {
		ret = cadence_spi_phy_apply_setting(priv, &mid);
		if (!ret)
			ret = cadence_spi_phy_check_pattern(priv, spi);

		if (ret) {
			/*
			 * Since we couldn't find the pattern, we need to go the
			 * upper half.
			 */
			left.tx = mid.tx;
			left.rx = mid.rx;

			mid.tx = mid.tx + ((right.tx - mid.tx) / 2);
			mid.rx = mid.rx + ((right.rx - mid.rx) / 2);
		} else {
			/*
			 * Since we found the pattern, we need to go to the
			 * lower half.
			 */
			right.tx = mid.tx;
			right.rx = mid.rx;

			mid.tx = left.tx + ((mid.tx - left.tx) / 2);
			mid.rx = left.rx + ((mid.rx - left.rx) / 2);
		}

	/* Break the loop if the window has closed. */
	} while ((right.tx - left.tx >= 2) && (right.rx - left.rx >= 2));

	*gaphigh = mid;
	return 0;
}

static int cadence_spi_phy_calibrate(struct cadence_spi_priv *priv,
				     struct spi_slave *spi)
{
	struct phy_setting rxlow, rxhigh, txlow, txhigh;
	struct phy_setting srxlow, srxhigh;
	struct phy_setting bottomleft, topright, searchpoint;
	struct phy_setting gaplow, gaphigh;
	struct phy_setting backuppoint, backupcornerpoint;
	struct udevice *bus = spi->dev->parent;
	int ret, tmp;
	bool primary = 1, secondary = 1;

	priv->use_phy = true;

	/*
	 * Finding rx fails at some of the tx values based on the H/W platform.
	 * A window of tx values is used to find the rx without errors. This
	 * can increase the number of CPU cycles taken for the PHY tuning in
	 * the cases where more tx values need to be parsed to find a stable
	 * rx.
	 */

	/* ***********************Golden rxlow search*********************** */

	/*
	 *
	 *		rx
	 *	    127	^
	 *		|
	 *		|	xxxxx     ++++++++++++++++++++
	 *		|	xxxxxx     +++++++++++++++++++
	 *		|	xxxxxxx     ++++++++++++++++++
	 *		|	xxxxxxxx     +++++++++++++++++
	 *		|	xxxxxxxxx     ++++++++++++++++
	 *		|	xxxxxxxxxx     +++++++++++++++
	 *		|	xxxxxxxxxxx     ++++++++++++++
	 *		|	|xxxxx|xxxxx     +++++++++++++
	 *		|	|xxxxx|xxxxxx     ++++++++++++
	 *	search	|	|xxxxx|xxxxxxx     +++++++++++
	 *	rxlow --------->|xxxxx|xxxxxxxx     ++++++++++
	 *		|	|xxxxx|xxxxxxxxx     +++++++++
	 *		|	|xxxxx|xxxxxxxxxx     ++++++++
	 *		|	|xxxxx|xxxxxxxxxxx     +++++++
	 *		|	|     |
	 *		--------|-----|----------------------------> tx
	 *		0	|     |				 127
	 *		    txlow     txlow
	 *		    start     end
	 *
	 */

	/*
	 *	|----------------------------------------------------------|
	 *	| Primary | Secondary | Final                              |
	 *	| Search  | Search    | Point                              |
	 *	|---------|-----------|------------------------------------|
	 *	| Fail    | Fail      | Return Fail                        |
	 *	|---------|-----------|------------------------------------|
	 *	| Fail    | Pass      | Return Fail                        |
	 *	|---------|-----------|------------------------------------|
	 *	| Pass    | Fail      | Return Fail                        |
	 *	|---------|-----------|------------------------------------|
	 *	| Pass    | Pass      | rx = min(primary.rx, secondary.rx) |
	 *	|         |           | tx = primary.tx                    |
	 *	|         |           | read_delay =                       |
	 *	|	  |	      |		min(primary.read_delay,    |
	 *	|	  |	      |		    secondary.read_delay)  |
	 *	|----------------------------------------------------------|
	 */

	/* *******************Golden Primary rxlow search******************* */
	/*
	 * To find the rx boundaries, we fix a valid tx and search through rx
	 * range, read_delay values. As we are not sure of a valid tx we use a
	 * window of tx values to find the rx boundaries.
	 */

	rxlow.tx = CQSPI_PHY_TX_LOOKUP_LOW_START;
	do {
		dev_dbg(bus, "Searching for Godlen Primary rxlow on TX = %d\n",
			rxlow.tx);
		rxlow.read_delay = CQSPI_PHY_INIT_RD;
		ret = cadence_spi_find_rx_low(priv, spi, &rxlow);
		rxlow.tx += CQSPI_PHY_DDR_SEARCH_STEP;
	} while (ret && rxlow.tx <= CQSPI_PHY_TX_LOOKUP_LOW_END);
	if (ret)
		goto out;
	dev_dbg(bus, "Golden Primary rxlow: RX: %d TX: %d RD: %d\n", rxlow.rx,
		rxlow.tx, rxlow.read_delay);

	/* ******************Golden Secondary rxlow search****************** */
	/* Search for one more rxlow at different tx */

	if (rxlow.tx <= (CQSPI_PHY_TX_LOOKUP_LOW_END -
			 CQSPI_PHY_SEARCH_OFFSET))
		srxlow.tx = rxlow.tx + CQSPI_PHY_SEARCH_OFFSET;
	else
		srxlow.tx = CQSPI_PHY_TX_LOOKUP_LOW_END;
	dev_dbg(bus, "Searching for Golden Secondary rxlow on TX = %d\n",
		srxlow.tx);
	srxlow.read_delay = CQSPI_PHY_INIT_RD;
	ret = cadence_spi_find_rx_low(priv, spi, &srxlow);
	if (ret)
		goto out;
	dev_dbg(bus, "Golden Secondary rxlow: RX: %d TX: %d RD: %d\n",
		srxlow.rx, srxlow.tx, srxlow.read_delay);

	rxlow.rx = min(rxlow.rx, srxlow.rx);
	rxlow.read_delay = min(rxlow.read_delay, srxlow.read_delay);
	dev_dbg(bus, "Golden Final rxlow: RX: %d TX: %d RD: %d\n", rxlow.rx,
		rxlow.tx, rxlow.read_delay);

	/* **********************Golden rxhigh search********************** */

	/*
	 *
	 *		rx
	 *	    127	^
	 *		|
	 *		|	|xxxx     ++++++++++++++++++++
	 *		|	|xxxxx     +++++++++++++++++++
	 *    search	|	|xxxxxx     ++++++++++++++++++
	 *    rxhigh  --------->|xxxxxxx     +++++++++++++++++
	 *    on fixed  |	|xxxxxxxx     ++++++++++++++++
	 *    tx	|	|xxxxxxxxx     +++++++++++++++
	 *		|	|xxxxxxxxxx     ++++++++++++++
	 *		|	xxxxxxxxxxxx     +++++++++++++
	 *		|	xxxxxxxxxxxxx     ++++++++++++
	 *		|	xxxxxxxxxxxxxx     +++++++++++
	 *		|	xxxxxxxxxxxxxxx     ++++++++++
	 *		|	xxxxxxxxxxxxxxxx     +++++++++
	 *		|	xxxxxxxxxxxxxxxxx     ++++++++
	 *		|	xxxxxxxxxxxxxxxxxx     +++++++
	 *		|
	 *		-------------------------------------------> tx
	 *		0					 127
	 *
	 */

	/*
	 *	|----------------------------------------------------------|
	 *	| Primary | Secondary | Final                              |
	 *	| Search  | Search    | Point                              |
	 *	|---------|-----------|------------------------------------|
	 *	| Fail    | Fail      | Return Fail                        |
	 *	|---------|-----------|------------------------------------|
	 *	| Fail    | Pass      | Choose Secondary                   |
	 *	|---------|-----------|------------------------------------|
	 *	| Pass    | Fail      | Choose Primary                     |
	 *	|---------|-----------|------------------------------------|
	 *	| Pass    | Pass      | if (secondary.rx > primary.rx)     |
	 *	|         |           |		Choose Secondary           |
	 *	|         |           | else                               |
	 *	|	  |	      |		Choose Primary             |
	 *	|----------------------------------------------------------|
	 */

	/* ******************Golden Primary rxhigh search****************** */
	/*
	 * To find rxhigh we use the tx values of rxlow. Start the read_delay
	 * from maximum and decrement it. As these are valid values and rxhigh
	 * read_delay is always greater than or equal to rxlow read_delay.
	 */

	rxhigh.tx = rxlow.tx;
	dev_dbg(bus, "Searching for Golden Primary rxhigh on TX = %d\n",
		rxhigh.tx);
	rxhigh.read_delay = CQSPI_PHY_MAX_RD;
	ret = cadence_spi_find_rx_high(priv, spi, &rxhigh);
	if (ret)
		primary = 0;
	dev_dbg(bus, "Golden Primary rxhigh: RX: %d TX: %d RD: %d\n", rxhigh.rx,
		rxhigh.tx, rxhigh.read_delay);

	/* *****************Golden Secondary rxhigh search***************** */
	/* Search for one more rxhigh at different tx */

	if (rxhigh.tx <=
	    (CQSPI_PHY_TX_LOOKUP_LOW_END - CQSPI_PHY_SEARCH_OFFSET))
		srxhigh.tx = rxhigh.tx + CQSPI_PHY_SEARCH_OFFSET;
	else
		srxhigh.tx = CQSPI_PHY_TX_LOOKUP_LOW_END;
	dev_dbg(bus, "Searching for Golden Secondary rxhigh on TX = %d\n",
		srxhigh.tx);
	srxhigh.read_delay = CQSPI_PHY_MAX_RD;
	ret = cadence_spi_find_rx_high(priv, spi, &srxhigh);
	if (ret)
		secondary = 0;
	dev_dbg(bus, "Golden Secondary rxhigh: RX: %d TX: %d RD: %d\n",
		srxhigh.rx, srxhigh.tx, srxhigh.read_delay);

	if (primary || secondary) {
		if (srxhigh.rx > rxhigh.rx)
			rxhigh = srxhigh;
	} else {
		goto out;
	}
	dev_dbg(bus, "Golden Final rxhigh: RX: %d TX: %d RD: %d\n", rxhigh.rx,
		rxhigh.tx, rxhigh.read_delay);

	primary = 1;
	secondary = 1;

	/*
	 * Check a different point if rxlow and rxhigh are on the same read
	 * delay. This avoids mistaking the failing region for an RX boundary.
	 */

	if (rxlow.read_delay == rxhigh.read_delay) {
		dev_dbg(bus, "rxlow and rxhigh at the same read delay.\n");

		/* *******************Backup rxlow search******************* */

		/* Look for RX boundaries at upper TX range. */

		/*
		 *
		 *		rx
		 *	    127	^
		 *		|
		 *		|	xxxxx     ++++++++++++++++++++
		 *		|	xxxxxx     +++++++++++++++++++
		 *		|	xxxxxxx     ++++++++++++++++++
		 *		|	xxxxxxxx     +++++++++++++++++
		 *		|	xxxxxxxxx     ++++++++++++++++
		 *		|	xxxxxxxxxx     +++++++++++++++
		 *		|	xxxxxxxxxxx     ++++++++++++++
		 *		|	xxxxxxxxxxxx     +++++++|++++|
		 *		|	xxxxxxxxxxxxx     ++++++|++++|
		 *	search	|	xxxxxxxxxxxxxx     +++++|++++|
		 *	rxlow --------------------------------->|++++|
		 *		|	xxxxxxxxxxxxxxxx     +++|++++|
		 *		|	xxxxxxxxxxxxxxxxx     ++|++++|
		 *		|	xxxxxxxxxxxxxxxxxx     +|++++|
		 *		|				|    |
		 *		--------------------------------|----|-----> tx
		 *		0				|    |	 127
		 *`					   txhigh    txhigh
		 *					    start    end
		 *
		 */

		/*
		 *	|-----------------------------------------------------|
		 *	| Primary | Secondary | Final                         |
		 *	| Search  | Search    | Point                         |
		 *	|---------|-----------|-------------------------------|
		 *	| Fail    | Fail      | Return Fail                   |
		 *	|---------|-----------|-------------------------------|
		 *	| Fail    | Pass      | Return Fail                   |
		 *	|---------|-----------|-------------------------------|
		 *	| Pass    | Fail      | Return Fail                   |
		 *	|---------|-----------|-------------------------------|
		 *	| Pass    | Pass      | rx =			      |
		 *	|	  |	      |	 min(primary.rx, secondary.rx)|
		 *	|         |           | tx = primary.tx               |
		 *	|         |           | read_delay =                  |
		 *	|	  |	      |	 min(primary.read_delay,      |
		 *	|	  |	      |	     secondary.read_delay)    |
		 *	|-----------------------------------------------------|
		 */

		/* ***************Backup Primary rxlow search*************** */
		/*
		 * Find the rx boundaries using the tx window at the higher
		 * end. We start at the window end and decrement the tx value
		 * until we find the valid point.
		 */

		backuppoint.tx = CQSPI_PHY_TX_LOOKUP_HIGH_END;
		do {
			dev_dbg(bus, "Searching for Backup Primary rxlow on TX = %d\n",
				backuppoint.tx);
			backuppoint.read_delay = CQSPI_PHY_INIT_RD;
			ret = cadence_spi_find_rx_low(priv, spi, &backuppoint);
			backuppoint.tx -= CQSPI_PHY_DDR_SEARCH_STEP;
		} while (ret &&
			 backuppoint.tx >= CQSPI_PHY_TX_LOOKUP_HIGH_START);
		if (ret)
			goto out;
		dev_dbg(bus, "Backup Primary rxlow: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		/* **************Backup Secondary rxlow search************** */
		/* Search for one more rxlow at different tx */

		if (backuppoint.tx >=
		    (CQSPI_PHY_TX_LOOKUP_HIGH_START + CQSPI_PHY_SEARCH_OFFSET))
			srxlow.tx = backuppoint.tx - CQSPI_PHY_SEARCH_OFFSET;
		else
			srxlow.tx = CQSPI_PHY_TX_LOOKUP_HIGH_START;
		dev_dbg(bus,
			"Searching for Backup Secondary rxlow on TX = %d\n",
			srxlow.tx);
		srxlow.read_delay = CQSPI_PHY_INIT_RD;
		ret = cadence_spi_find_rx_low(priv, spi, &srxlow);
		if (ret)
			goto out;
		dev_dbg(bus, "Backup Secondary rxlow: RX: %d TX: %d RD: %d\n",
			srxlow.rx, srxlow.tx, srxlow.read_delay);

		backuppoint.rx = min(backuppoint.rx, srxlow.rx);
		backuppoint.read_delay =
		    min(backuppoint.read_delay, srxlow.read_delay);
		dev_dbg(bus, "Backup Final rxlow: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		if (backuppoint.rx < rxlow.rx) {
			rxlow = backuppoint;
			dev_dbg(bus, "Updating rxlow to the one at TX = %d\n",
				backuppoint.tx);
		}
		dev_dbg(bus, "Final rxlow: RX: %d TX: %d RD: %d\n", rxlow.rx,
			rxlow.tx, rxlow.read_delay);

		/* ******************Backup rxhigh search****************** */

		/*
		 *
		 *		rx
		 *	    127	^
		 *		|
		 *		|	xxxxx     +++++++++++++++++++|
		 *		|	xxxxxx     ++++++++++++++++++|
		 *    search	|	xxxxxxx     +++++++++++++++++|
		 *    rxhigh  -------------------------------------->|
		 *    on fixed	|	xxxxxxxxx     +++++++++++++++|
		 *    tx	|	xxxxxxxxxx     ++++++++++++++|
		 *		|	xxxxxxxxxxx     +++++++++++++|
		 *		|	xxxxxxxxxxxx     +++++++++++++
		 *		|	xxxxxxxxxxxxx     ++++++++++++
		 *		|	xxxxxxxxxxxxxx     +++++++++++
		 *		|	xxxxxxxxxxxxxxx     ++++++++++
		 *		|	xxxxxxxxxxxxxxxx     +++++++++
		 *		|	xxxxxxxxxxxxxxxxx     ++++++++
		 *		|	xxxxxxxxxxxxxxxxxx     +++++++
		 *		|
		 *		-------------------------------------------> tx
		 *		0					 127
		 *
		 */

		/*
		 *	|-----------------------------------------------------|
		 *	| Primary | Secondary | Final                         |
		 *	| Search  | Search    | Point                         |
		 *	|---------|-----------|-------------------------------|
		 *	| Fail    | Fail      | Return Fail                   |
		 *	|---------|-----------|-------------------------------|
		 *	| Fail    | Pass      | Choose Secondary              |
		 *	|---------|-----------|-------------------------------|
		 *	| Pass    | Fail      | Choose Primary                |
		 *	|---------|-----------|-------------------------------|
		 *	| Pass    | Pass      | if (secondary.rx > primary.rx)|
		 *	|         |           |		Choose Secondary      |
		 *	|         |           | else                          |
		 *	|	  |	      |		Choose Primary        |
		 *	|-----------------------------------------------------|
		 */

		/* **************Backup Primary rxhigh search************** */
		/*
		 * To find rxhigh we use the tx values of backuppoint. Start
		 * the read_delay from maximum and decrement it. As these are
		 * valid values and rxhigh read_delay is always greater than or
		 * equal to rxlow read_delay.
		 */

		dev_dbg(bus, "Searching for Backup Primary rxhigh on TX = %d\n",
			backuppoint.tx);
		backuppoint.read_delay = CQSPI_PHY_MAX_RD;
		ret = cadence_spi_find_rx_high(priv, spi, &backuppoint);
		if (ret)
			primary = 0;
		dev_dbg(bus, " Backup Primary rxhigh: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		/* *************Backup Secondary rxhigh search************* */
		/* Search for one more rxhigh at different tx */

		if (backuppoint.tx >=
		    (CQSPI_PHY_TX_LOOKUP_HIGH_START + CQSPI_PHY_SEARCH_OFFSET))
			srxhigh.tx = backuppoint.tx - CQSPI_PHY_SEARCH_OFFSET;
		else
			srxhigh.tx = CQSPI_PHY_TX_LOOKUP_HIGH_START;
		dev_dbg(bus,
			"Searching for Backup Secondary rxhigh on TX = %d\n",
			srxhigh.tx);
		srxhigh.read_delay = CQSPI_PHY_MAX_RD;
		ret = cadence_spi_find_rx_high(priv, spi, &srxhigh);
		if (ret)
			secondary = 0;
		dev_dbg(bus, "Backup Secondary rxhigh: RX: %d TX: %d RD: %d\n",
			srxhigh.rx, srxhigh.tx, srxhigh.read_delay);

		if (primary || secondary) {
			if (srxhigh.rx > backuppoint.rx)
				backuppoint = srxhigh;
		} else {
			goto out;
		}
		dev_dbg(bus, "Backup Final rxhigh: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		if (backuppoint.rx > rxhigh.rx) {
			rxhigh = backuppoint;
			dev_dbg(bus, "Updating rxhigh to the one at TX = %d\n",
				backuppoint.tx);
		}
		dev_dbg(bus, "Final rxhigh: RX: %d TX: %d RD: %d\n", rxhigh.rx,
			rxhigh.tx, rxhigh.read_delay);
	}

	/* ***********************Golden txlow search*********************** */
	/* Look for TX boundaries at 1/4 of RX window. */

	/*
	 *
	 *		rx
	 *	    127	^
	 *		|
	 *     rxhigh --------->xxxxx     ++++++++++++++++++++
	 *		|	xxxxxx     +++++++++++++++++++
	 *		|	xxxxxxx     ++++++++++++++++++
	 *		|	xxxxxxxx     +++++++++++++++++
	 *		|	xxxxxxxxx     ++++++++++++++++
	 *		|	xxxxxxxxxx     +++++++++++++++
	 *		|	xxxxxxxxxxx     ++++++++++++++
	 *		|	xxxxxxxxxxxx     +++++++++++++
	 *    fix rx	|	xxxxxxxxxxxxx     ++++++++++++
	 *    1/4 b/w ---------><------->xxxxx     +++++++++++
	 *    rxlow and	|	xxxx|xxxxxxxxxx     ++++++++++
	 *    rxhigh	|	xxxx|xxxxxxxxxxx     +++++++++
	 *		|	xxxx|xxxxxxxxxxxx     ++++++++
	 *	rxlow --------->xxxx|xxxxxxxxxxxxx     +++++++
	 *		|	    |
	 *		------------|------------------------------> tx
	 *		0	    |				 127
	 *		       search
	 *			txlow
	 *
	 */

	txlow.rx = rxlow.rx + ((rxhigh.rx - rxlow.rx) / 4);
	dev_dbg(bus, "Searching for Golden txlow on RX = %d\n", txlow.rx);
	txlow.read_delay = CQSPI_PHY_INIT_RD;
	ret = cadence_spi_find_tx_low(priv, spi, &txlow);
	if (ret)
		goto out;
	dev_dbg(bus, "Golden txlow: RX: %d TX: %d RD: %d\n", txlow.rx,
		txlow.tx, txlow.read_delay);

	/* **********************Golden txhigh search********************** */
	/* Start from maximum read_delay and decrememt it */

	/*
	 *
	 *		rx
	 *	    127	^
	 *		|
	 *     rxhigh --------->xxxxx     ++++++++++++++++++++
	 *		|	xxxxxx     +++++++++++++++++++
	 *		|	xxxxxxx     ++++++++++++++++++
	 *		|	xxxxxxxx     +++++++++++++++++
	 *		|	xxxxxxxxx     ++++++++++++++++
	 *		|	xxxxxxxxxx     +++++++++++++++
	 *		|	xxxxxxxxxxx     ++++++++++++++
	 *		|	xxxxxxxxxxxx     +++++++++++++
	 *    fix rx	|	xxxxxxxxxxxxx     ++++++++++++
	 *    1/4 b/w --------------------------------><----->
	 *    rxlow and	|	xxxxxxxxxxxxxxx     ++++++|+++
	 *    rxhigh	|	xxxxxxxxxxxxxxxx     +++++|+++
	 *		|	xxxxxxxxxxxxxxxxx     ++++|+++
	 *	rxlow --------->xxxxxxxxxxxxxxxxxx     +++|+++
	 *		|				  |
	 *		----------------------------------|--------> tx
	 *		0				  |	 127
	 *					     search
	 *					     txhigh
	 *
	 */

	txhigh.rx = txlow.rx;
	dev_dbg(bus, "Searching for Golden txhigh on RX = %d\n", txhigh.rx);
	txhigh.read_delay = CQSPI_PHY_MAX_RD;
	ret = cadence_spi_find_tx_high(priv, spi, &txhigh);
	if (ret)
		goto out;
	dev_dbg(bus, "Golden txhigh: RX: %d TX: %d RD: %d\n", txhigh.rx,
		txhigh.tx, txhigh.read_delay);

	/*
	 * Check a different point if txlow and txhigh are on the same read
	 * delay. This avoids mistaking the failing region for an TX boundary.
	 */
	if (txlow.read_delay == txhigh.read_delay) {
		/* *******************Backup txlow search******************* */
		/* Look for TX boundaries at 3/4 of RX window. */

		/*
		 *
		 *		rx
		 *	    127	^
		 *		|
		 *     rxhigh --------->xxxxx     ++++++++++++++++++++
		 *		|	xxxxxx     +++++++++++++++++++
		 *    fix rx	|	xxxxxxx     ++++++++++++++++++
		 *    3/4 b/w ---------><----->x     +++++++++++++++++
		 *    rxlow and	|	xxxx|xxxx     ++++++++++++++++
		 *    rxhigh	|	xxxx|xxxxx     +++++++++++++++
		 *		|	xxxx|xxxxxx     ++++++++++++++
		 *		|	xxxx|xxxxxxx     +++++++++++++
		 *		|	xxxx|xxxxxxxx     ++++++++++++
		 *		|	xxxx|xxxxxxxxx     +++++++++++
		 *		|	xxxx|xxxxxxxxxx     ++++++++++
		 *		|	xxxx|xxxxxxxxxxx     +++++++++
		 *		|	xxxx|xxxxxxxxxxxx     ++++++++
		 *	rxlow --------->xxxx|xxxxxxxxxxxxx     +++++++
		 *		|	    |
		 *		------------|------------------------------> tx
		 *		0	    |				 127
		 *		       search
		 *			txlow
		 *
		 */

		dev_dbg(bus, "txlow and txhigh at the same read delay.\n");
		backuppoint.rx = rxlow.rx + (3 * (rxhigh.rx - rxlow.rx) / 4);
		dev_dbg(bus, "Searching for Backup txlow on RX = %d\n",
			backuppoint.rx);
		backuppoint.read_delay = CQSPI_PHY_INIT_RD;
		ret = cadence_spi_find_tx_low(priv, spi, &backuppoint);
		if (ret)
			goto out;
		dev_dbg(bus, "Backup txlow: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		if (backuppoint.tx < txlow.tx) {
			txlow = backuppoint;
			dev_dbg(bus, "Updating txlow with the one at RX = %d\n",
				backuppoint.rx);
		}
		dev_dbg(bus, "Final txlow: RX: %d TX: %d RD: %d\n", txlow.rx,
			txlow.tx, txlow.read_delay);

		/* ******************Backup txhigh search****************** */
		/* Start from maximum read_delay and decrememt it */

		/*
		 *
		 *		rx
		 *	    127	^
		 *		|
		 *     rxhigh --------->xxxxx     ++++++++++++++++++++
		 *		|	xxxxxx     +++++++++++++++++++
		 *    fix rx	|	xxxxxxx     ++++++++++++++++++
		 *    3/4 b/w ------------------------------><------->
		 *    rxlow and	|	xxxxxxxxx     +++++++++++|++++
		 *    rxhigh	|	xxxxxxxxxx     ++++++++++|++++
		 *		|	xxxxxxxxxxx     +++++++++|++++
		 *		|	xxxxxxxxxxxx     ++++++++|++++
		 *		|	xxxxxxxxxxxxx     +++++++|++++
		 *		|	xxxxxxxxxxxxxx     ++++++|++++
		 *		|	xxxxxxxxxxxxxxx     +++++|++++
		 *		|	xxxxxxxxxxxxxxxx     ++++|++++
		 *		|	xxxxxxxxxxxxxxxxx     +++|++++
		 *	rxlow --------->xxxxxxxxxxxxxxxxxx     ++|++++
		 *		|				 |
		 *		---------------------------------|---------> tx
		 *		0				 |	 127
		 *						 search
		 *						 txhigh
		 *
		 */

		dev_dbg(bus, "Searching for Backup txhigh on RX = %d\n",
			backuppoint.rx);
		backuppoint.read_delay = CQSPI_PHY_MAX_RD;
		ret = cadence_spi_find_tx_high(priv, spi, &backuppoint);
		if (ret)
			goto out;
		dev_dbg(bus, "Backup txhigh: RX: %d TX: %d RD: %d\n",
			backuppoint.rx, backuppoint.tx,
			backuppoint.read_delay);

		if (backuppoint.tx > txhigh.tx) {
			txhigh = backuppoint;
			dev_dbg(bus,
				"Updating txhigh with the one at RX = %d\n",
				backuppoint.rx);
		}
		dev_dbg(bus, "Final txhigh: RX: %d TX: %d RD: %d\n", txhigh.rx,
			txhigh.tx, txhigh.read_delay);
	}

	/*
	 * Set bottom left and top right corners. These are theoretical
	 * corners. They may not actually be "good" points. But the longest
	 * diagonal will be between these corners.
	 */

	bottomleft.tx = txlow.tx;
	bottomleft.rx = rxlow.rx;
	if (txlow.read_delay <= rxlow.read_delay)
		bottomleft.read_delay = txlow.read_delay;
	else
		bottomleft.read_delay = rxlow.read_delay;

	backupcornerpoint = bottomleft;
	backupcornerpoint.tx += 4;
	backupcornerpoint.rx += 4;
	ret = cadence_spi_phy_apply_setting(priv, &backupcornerpoint);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret) {
		backupcornerpoint.read_delay--;
		ret = cadence_spi_phy_apply_setting(priv, &backupcornerpoint);
		if (!ret)
			ret = cadence_spi_phy_check_pattern(priv, spi);
	}

	/* TODO: if (ret) */

	if (!ret)
		bottomleft.read_delay = backupcornerpoint.read_delay;

	topright.tx = txhigh.tx;
	topright.rx = rxhigh.rx;
	if (txhigh.read_delay >= rxhigh.read_delay)
		topright.read_delay = txhigh.read_delay;
	else
		topright.read_delay = rxhigh.read_delay;

	backupcornerpoint = topright;
	backupcornerpoint.tx -= 4;
	backupcornerpoint.rx -= 4;
	ret = cadence_spi_phy_apply_setting(priv, &backupcornerpoint);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret) {
		backupcornerpoint.read_delay++;
		ret = cadence_spi_phy_apply_setting(priv, &backupcornerpoint);
		if (!ret)
			ret = cadence_spi_phy_check_pattern(priv, spi);
	}

	/* TODO: if (ret) */

	if (!ret)
		topright.read_delay = backupcornerpoint.read_delay;

	dev_dbg(bus, "topright: RX: %d TX: %d RD: %d\n", topright.rx,
		topright.tx, topright.read_delay);
	dev_dbg(bus, "bottomleft: RX: %d TX: %d RD: %d\n", bottomleft.rx,
		bottomleft.tx, bottomleft.read_delay);

	ret = cadence_spi_phy_find_gaplow(priv, spi, &bottomleft, &topright,
					  &gaplow);
	if (ret)
		goto out;
	dev_dbg(bus, "gaplow: RX: %d TX: %d RD: %d\n", gaplow.rx, gaplow.tx,
		gaplow.read_delay);

	if (bottomleft.read_delay == topright.read_delay) {
		/*
		 * If there is only one passing region, it means that the
		 * "true" topright is too small to find, so the start of the
		 * failing region is a good approximation. Put the tuning point
		 * in the middle and adjust for temperature.
		 */

		dev_dbg(bus,
			"bottomleft and topright at the same read delay.\n");
		topright = gaplow;
		searchpoint.read_delay = bottomleft.read_delay;
		searchpoint.tx = bottomleft.tx +
				 ((topright.tx - bottomleft.tx) / 2);
		searchpoint.rx = bottomleft.rx +
				 ((topright.rx - bottomleft.rx) / 2);

		ret = cadence_spi_get_temp(&tmp);
		if (ret) {
			/*
			 * Assume room temperature if we couldn't get it from
			 * the thermal sensor.
			 */

			dev_dbg(bus,
				"Unable to get temperature. Assuming room temperature\n");
			tmp = CQSPI_PHY_DEFAULT_TEMP;
		}

		if (tmp < CQSPI_PHY_MIN_TEMP || tmp > CQSPI_PHY_MAX_TEMP) {
			printf("ERROR: temperature outside operating range: %dC\n",
			       tmp);
			return -EINVAL;
		}

		/* Avoid a divide-by-zero. */

		if (tmp == CQSPI_PHY_MID_TEMP)
			tmp++;
		dev_dbg(bus, "Temperature: %dC\n", tmp);

		searchpoint.tx += (topright.tx - bottomleft.tx) /
				  (330 / (tmp - CQSPI_PHY_MID_TEMP));
		searchpoint.rx += (topright.rx - bottomleft.rx) /
				  (330 / (tmp - CQSPI_PHY_MID_TEMP));
	} else {
		/*
		 * If there are two passing regions, find the start and end of
		 * the second one.
		 */

		ret = cadence_spi_phy_find_gaphigh(priv, spi, &bottomleft,
						   &topright, &gaphigh);
		if (ret)
			return ret;
		dev_dbg(bus, "gaphigh: RX: %d TX: %d RD: %d\n", gaphigh.rx,
			gaphigh.tx, gaphigh.read_delay);

		/*
		 * Place the final tuning point in the corner furthest from the
		 * failing region but leave some margin for temperature
		 * changes.
		 */

		if ((abs(gaplow.tx - bottomleft.tx) +
		     abs(gaplow.rx - bottomleft.rx)) <
		    (abs(gaphigh.tx - topright.tx) +
		     abs(gaphigh.rx - topright.rx))) {
			searchpoint = topright;
			searchpoint.tx -= 16;
			searchpoint.rx -= (16 * (topright.rx - bottomleft.rx))
					  / (topright.tx - bottomleft.tx);
		} else {
			searchpoint = bottomleft;
			searchpoint.tx += 16;
			searchpoint.rx += (16 * (topright.rx - bottomleft.rx))
					  / (topright.tx - bottomleft.tx);
		}
	}

	/* Set the final PHY settings we found. */

	dev_dbg(bus, "Final tuning point: RX: %d TX: %d RD: %d\n",
		searchpoint.rx, searchpoint.tx, searchpoint.read_delay);
	ret = cadence_spi_phy_apply_setting(priv, &searchpoint);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret) {
		debug("Failed to find pattern at final calibration point\n");
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
	priv->phy_read_delay = searchpoint.read_delay;
out:
	if (ret)
		priv->use_phy = false;
	return ret;
}

static void cadence_spi_phy_reset_setting(struct phy_setting *phy)
{
	phy->rx = 0;
	phy->tx = 127;
	phy->read_delay = 0;
}

static int cadence_spi_phy_calibrate_sdr(struct cadence_spi_priv *priv,
					 struct spi_slave *spi)
{
	struct udevice *bus = spi->dev->parent;
	struct phy_setting rxlow, rxhigh, first, second, final;
	char window1 = 0;
	char window2 = 0;
	int ret;

	priv->use_phy = true;
	cadence_spi_phy_reset_setting(&rxlow);
	cadence_spi_phy_reset_setting(&rxhigh);
	cadence_spi_phy_reset_setting(&first);

	do {
		ret = cadence_spi_find_rx_low_sdr(priv, spi, &rxlow);

		if (ret)
			rxlow.read_delay++;
	} while (ret && rxlow.read_delay <= CQSPI_PHY_MAX_RD);

	rxhigh.read_delay = rxlow.read_delay;
	ret = cadence_spi_find_rx_high_sdr(priv, spi, &rxhigh, rxlow.rx);
	if (ret)
		goto out;

	first.read_delay = rxlow.read_delay;
	window1 = rxhigh.rx - rxlow.rx;
	first.rx = rxlow.rx + (window1 / 2);

	dev_dbg(bus, "First tuning point: RX: %d TX: %d RD: %d\n", first.rx,
		first.tx, first.read_delay);
	ret = cadence_spi_phy_apply_setting(priv, &first);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret || first.read_delay > CQSPI_PHY_MAX_RD)
		goto out;

	cadence_spi_phy_reset_setting(&rxlow);
	cadence_spi_phy_reset_setting(&rxhigh);
	cadence_spi_phy_reset_setting(&second);

	rxlow.read_delay = first.read_delay + 1;
	if (rxlow.read_delay > CQSPI_PHY_MAX_RD)
		goto compare;

	ret = cadence_spi_find_rx_low_sdr(priv, spi, &rxlow);

	if (ret)
		goto compare;

	rxhigh.read_delay = rxlow.read_delay;
	ret = cadence_spi_find_rx_high_sdr(priv, spi, &rxhigh, rxlow.rx);
	if (ret)
		goto compare;

	window2 = rxhigh.rx - rxlow.rx;
	second.rx = rxlow.rx + (window2 / 2);
	second.read_delay = rxlow.read_delay;

	dev_dbg(bus, "Second tuning point: RX: %d TX: %d RD: %d\n", second.rx,
		second.tx, second.read_delay);
	ret = cadence_spi_phy_apply_setting(priv, &second);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret || second.read_delay > CQSPI_PHY_MAX_RD)
		window2 = 0;

compare:
	cadence_spi_phy_reset_setting(&final);
	if (window2 > window1) {
		final.rx = second.rx;
		final.read_delay = second.read_delay;
	} else {
		final.rx = first.rx;
		final.read_delay = first.read_delay;
	}

	dev_dbg(bus, "Final tuning point: RX: %d TX: %d RD: %d\n", final.rx,
		final.tx, final.read_delay);
	ret = cadence_spi_phy_apply_setting(priv, &final);
	if (!ret)
		ret = cadence_spi_phy_check_pattern(priv, spi);

	if (ret) {
		ret = -EINVAL;
		goto out;
	}

out:
	if (ret)
		priv->use_phy = false;

	return ret;
}

static int cadence_spi_write_speed(struct udevice *bus, uint hz)
{
	struct cadence_spi_priv *priv = dev_get_priv(bus);

	cadence_qspi_apb_config_baudrate_div(priv->regbase,
					     priv->ref_clk_hz, hz);

	/* Reconfigure delay timing if speed is changed. */
	cadence_qspi_apb_delay(priv->regbase, priv->ref_clk_hz, hz,
			       priv->tshsl_ns, priv->tsd2d_ns,
			       priv->tchsh_ns, priv->tslch_ns);

	return 0;
}

static int cadence_spi_read_id(struct cadence_spi_priv *priv, u8 len,
			       u8 *idcode)
{
	int err;

	struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(0x9F, 1),
					  SPI_MEM_OP_NO_ADDR,
					  SPI_MEM_OP_NO_DUMMY,
					  SPI_MEM_OP_DATA_IN(len, idcode, 1));

	err = cadence_qspi_apb_command_read_setup(priv, &op);
	if (!err)
		err = cadence_qspi_apb_command_read(priv, &op);

	return err;
}

/* Calibration sequence to determine the read data capture delay register */
static int spi_calibration(struct udevice *bus, uint hz)
{
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	void *base = priv->regbase;
	unsigned int idcode = 0, temp = 0;
	int err = 0, i, range_lo = -1, range_hi = -1;

	/* start with slowest clock (1 MHz) */
	cadence_spi_write_speed(bus, 1000000);

	/* configure the read data capture delay register to 0 */
	cadence_qspi_apb_readdata_capture(base, 1, true, 0);

	/* Enable QSPI */
	cadence_qspi_apb_controller_enable(base);

	/* read the ID which will be our golden value */
	err = cadence_spi_read_id(priv, 3, (u8 *)&idcode);
	if (err) {
		puts("SF: Calibration failed (read)\n");
		return err;
	}

	/* use back the intended clock and find low range */
	cadence_spi_write_speed(bus, hz);
	for (i = 0; i < CQSPI_READ_CAPTURE_MAX_DELAY; i++) {
		/* Disable QSPI */
		cadence_qspi_apb_controller_disable(base);

		/* reconfigure the read data capture delay register */
		cadence_qspi_apb_readdata_capture(base, 1, true, i);

		/* Enable back QSPI */
		cadence_qspi_apb_controller_enable(base);

		/* issue a RDID to get the ID value */
		err = cadence_spi_read_id(priv, 3, (u8 *)&temp);
		if (err) {
			puts("SF: Calibration failed (read)\n");
			return err;
		}

		/* search for range lo */
		if (range_lo == -1 && temp == idcode) {
			range_lo = i;
			continue;
		}

		/* search for range hi */
		if (range_lo != -1 && temp != idcode) {
			range_hi = i - 1;
			break;
		}
		range_hi = i;
	}

	if (range_lo == -1) {
		puts("SF: Calibration failed (low range)\n");
		return err;
	}

	/* Disable QSPI for subsequent initialization */
	cadence_qspi_apb_controller_disable(base);

	/* configure the final value for read data capture delay register */
	cadence_qspi_apb_readdata_capture(base, 1, true, (range_hi + range_lo) / 2);
	debug("SF: Read data capture delay calibrated to %i (%i - %i)\n",
	      (range_hi + range_lo) / 2, range_lo, range_hi);

	/* just to ensure we do once only when speed or chip select change */
	priv->qspi_calibrated_hz = hz;
	priv->qspi_calibrated_cs = spi_chip_select(bus);

	return 0;
}

static int cadence_spi_set_speed(struct udevice *bus, uint hz)
{
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	int err;

	if (!hz || hz > priv->max_hz)
		hz = priv->max_hz;
	/* Disable QSPI */
	cadence_qspi_apb_controller_disable(priv->regbase);

	/*
	 * If the device tree already provides a read delay value, use that
	 * instead of calibrating.
	 */
	if (priv->read_delay >= 0) {
		cadence_spi_write_speed(bus, hz);
		cadence_qspi_apb_readdata_capture(priv->regbase, 1, true,
						  priv->read_delay);
	} else if (priv->previous_hz != hz ||
		   priv->qspi_calibrated_hz != hz ||
		   priv->qspi_calibrated_cs != spi_chip_select(bus)) {
		/*
		 * Calibration required for different current SCLK speed,
		 * requested SCLK speed or chip select
		 */
		err = spi_calibration(bus, hz);
		if (err)
			return err;

		/* prevent calibration run when same as previous request */
		priv->previous_hz = hz;
	}

	/* Enable QSPI */
	cadence_qspi_apb_controller_enable(priv->regbase);

	debug("%s: speed=%d\n", __func__, hz);

	return 0;
}

static int cadence_spi_probe(struct udevice *bus)
{
	struct cadence_spi_plat *plat = dev_get_plat(bus);
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	struct clk clk;
	int ret;

	priv->regbase		= plat->regbase;
	priv->ahbbase		= plat->ahbbase;
	priv->is_dma		= plat->is_dma;
	priv->is_decoded_cs	= plat->is_decoded_cs;
	priv->fifo_depth	= plat->fifo_depth;
	priv->fifo_width	= plat->fifo_width;
	priv->trigger_address	= plat->trigger_address;
	priv->read_delay	= plat->read_delay;
	priv->phase_detect_selector = plat->phase_detect_selector;
	priv->has_phy		= plat->has_phy;
	priv->phy_pattern_start = plat->phy_pattern_start;
	priv->phy_tx_start	= plat->phy_tx_start;
	priv->phy_tx_end	= plat->phy_tx_end;
	priv->ahbsize		= plat->ahbsize;
	priv->max_hz		= plat->max_hz;

	priv->page_size		= plat->page_size;
	priv->block_size	= plat->block_size;
	priv->tshsl_ns		= plat->tshsl_ns;
	priv->tsd2d_ns		= plat->tsd2d_ns;
	priv->tchsh_ns		= plat->tchsh_ns;
	priv->tslch_ns		= plat->tslch_ns;

	if (IS_ENABLED(CONFIG_ZYNQMP_FIRMWARE))
		xilinx_pm_request(PM_REQUEST_NODE, PM_DEV_OSPI,
				  ZYNQMP_PM_CAPABILITY_ACCESS, ZYNQMP_PM_MAX_QOS,
				  ZYNQMP_PM_REQUEST_ACK_NO, NULL);

	if (priv->ref_clk_hz == 0) {
		ret = clk_get_by_index(bus, 0, &clk);
		if (ret) {
#ifdef CONFIG_HAS_CQSPI_REF_CLK
			priv->ref_clk_hz = CONFIG_CQSPI_REF_CLK;
#elif defined(CONFIG_ARCH_SOCFPGA)
			priv->ref_clk_hz = cm_get_qspi_controller_clk_hz();
#else
			return ret;
#endif
		} else {
			priv->ref_clk_hz = clk_get_rate(&clk);
			if (IS_ERR_VALUE(priv->ref_clk_hz))
				return priv->ref_clk_hz;
		}
	}

	priv->resets = devm_reset_bulk_get_optional(bus);
	if (priv->resets)
		reset_deassert_bulk(priv->resets);

	if (!priv->qspi_is_init) {
		cadence_qspi_apb_controller_init(priv);
		priv->qspi_is_init = 1;
	}

	priv->wr_delay = 50 * DIV_ROUND_UP(NSEC_PER_SEC, priv->ref_clk_hz);

	/* Versal and Versal-NET use spi calibration to set read delay */
	if (CONFIG_IS_ENABLED(ARCH_VERSAL) ||
	    CONFIG_IS_ENABLED(ARCH_VERSAL_NET))
		if (priv->read_delay >= 0)
			priv->read_delay = -1;

	/* Reset ospi flash device */
	return cadence_qspi_versal_flash_reset(bus);
}

static int cadence_spi_remove(struct udevice *dev)
{
	struct cadence_spi_priv *priv = dev_get_priv(dev);
	int ret = 0;

	if (priv->resets)
		ret = reset_release_bulk(priv->resets);

	return ret;
}

static int cadence_spi_set_mode(struct udevice *bus, uint mode)
{
	struct cadence_spi_priv *priv = dev_get_priv(bus);

	/* Disable QSPI */
	cadence_qspi_apb_controller_disable(priv->regbase);

	/* Set SPI mode */
	cadence_qspi_apb_set_clk_mode(priv->regbase, mode);

	/* Enable Direct Access Controller */
	if (priv->use_dac_mode)
		cadence_qspi_apb_dac_mode_enable(priv->regbase);

	/* Enable QSPI */
	cadence_qspi_apb_controller_enable(priv->regbase);

	return 0;
}

static int cadence_spi_mem_exec_op(struct spi_slave *spi,
				   const struct spi_mem_op *op)
{
	struct udevice *bus = spi->dev->parent;
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	void *base = priv->regbase;
	int err = 0;
	u32 mode;

	/* Set Chip select */
	cadence_qspi_apb_chipselect(base, spi_chip_select(spi->dev),
				    priv->is_decoded_cs);

	if (op->data.dir == SPI_MEM_DATA_IN && op->data.buf.in) {
		/*
		 * Performing reads in DAC mode forces to read minimum 4 bytes
		 * which is unsupported on some flash devices during register
		 * reads, prefer STIG mode for such small reads.
		 */
		if (op->data.nbytes <= CQSPI_STIG_DATA_LEN_MAX)
			mode = CQSPI_STIG_READ;
		else
			mode = CQSPI_READ;
	} else {
		if (op->data.nbytes <= CQSPI_STIG_DATA_LEN_MAX)
			mode = CQSPI_STIG_WRITE;
		else
			mode = CQSPI_WRITE;
	}

	switch (mode) {
	case CQSPI_STIG_READ:
		err = cadence_qspi_apb_command_read_setup(priv, op);
		if (!err)
			err = cadence_qspi_apb_command_read(priv, op);
		break;
	case CQSPI_STIG_WRITE:
		err = cadence_qspi_apb_command_write_setup(priv, op);
		if (!err)
			err = cadence_qspi_apb_command_write(priv, op);
		break;
	case CQSPI_READ:
		err = cadence_qspi_apb_read_setup(priv, op);
		if (!err) {
			if (priv->is_dma)
				err = cadence_qspi_apb_dma_read(priv, op);
			else
				err = cadence_qspi_apb_read_execute(priv, op);
		}
		break;
	case CQSPI_WRITE:
		err = cadence_qspi_apb_write_setup(priv, op);
		if (!err)
			err = cadence_qspi_apb_write_execute(priv, op);
		break;
	default:
		err = -1;
		break;
	}

	return err;
}

static bool cadence_spi_mem_supports_op(struct spi_slave *slave,
					const struct spi_mem_op *op)
{
	bool all_true, all_false;

	/*
	 * op->dummy.dtr is required for converting nbytes into ncycles.
	 * Also, don't check the dtr field of the op phase having zero nbytes.
	 */
	all_true = op->cmd.dtr &&
		   (!op->addr.nbytes || op->addr.dtr) &&
		   (!op->dummy.nbytes || op->dummy.dtr) &&
		   (!op->data.nbytes || op->data.dtr);

	all_false = !op->cmd.dtr && !op->addr.dtr && !op->dummy.dtr &&
		    !op->data.dtr;

	/* Mixed DTR modes not supported. */
	if (!(all_true || all_false))
		return false;

	if (all_true)
		return spi_mem_dtr_supports_op(slave, op);
	else
		return spi_mem_default_supports_op(slave, op);
}

static void cadence_spi_mem_do_calibration(struct spi_slave *spi,
					   struct spi_mem_op *op)
{
	struct udevice *bus = spi->dev->parent;
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	int ret;

	if (!IS_ENABLED(CONFIG_CADENCE_QSPI_PHY) || !priv->has_phy)
		return;

	priv->phy_read_op = *op;

	if (priv->phase_detect_selector >
	    CQSPI_REG_PHY_DLL_MASTER_DLY_ELMTS_LEN) {
		dev_warn(bus,
			 "Phase Detect Selector should be in between [0, 7]. Skipping Calibration\n");
		return;
	}

	ret = cadence_spi_phy_check_pattern(priv, spi);
	if (ret) {
		dev_warn(bus, "Pattern not found. Skipping calibration\n");
		return;
	}

	if (cadence_qspi_apb_op_eligible(op)) {
		priv->use_dqs = true;

		cadence_qspi_apb_phy_pre_config(priv, 0, 1);

		ret = cadence_spi_phy_calibrate(priv, spi);
		if (ret)
			dev_warn(bus,
				 "PHY calibration failed: %d. Falling back to slower clock speeds.\n",
				 ret);
	} else if (cadence_qspi_apb_op_eligible_sdr(op)) {
		priv->use_dqs = false;

		cadence_qspi_apb_phy_pre_config(priv, 1, 0);

		ret = cadence_spi_phy_calibrate_sdr(priv, spi);
		if (ret)
			dev_warn(bus,
				 "PHY calibration failed: %d. Falling back to slower clock speeds.\n",
				 ret);

	} else {
		dev_warn(bus,
			 "Given read_op not eligible. Skipping Calibration.\n");
		return;
	}

	cadence_qspi_apb_phy_post_config(priv, priv->read_delay);
}

static int cadence_spi_ofdata_phy_pattern(ofnode flash_node)
{
	ofnode subnode;
	const char *label;
	u32 start;

	subnode = ofnode_find_subnode(flash_node, "partitions");
	if (!ofnode_valid(subnode))
		/*
		 * Maybe the node has legacy style partitions, listed directly
		 * under flash node.
		 */
		subnode = ofnode_first_subnode(flash_node);
	else
		subnode = ofnode_first_subnode(subnode);

	while (ofnode_valid(subnode)) {
		label = ofnode_read_string(subnode, "label");
		if (label && strcmp(label, "ospi.phypattern") == 0) {
			if (!ofnode_read_u32_array(subnode, "reg", &start, 1))
				return start;
			break;
		} else if (label && strcmp(label, "ospi_nand.phypattern") == 0)
			return 0;

		subnode = ofnode_next_subnode(subnode);
	}

	return -ENOENT;
}

static int cadence_spi_of_to_plat(struct udevice *bus)
{
	struct cadence_spi_plat *plat = dev_get_plat(bus);
	struct cadence_spi_priv *priv = dev_get_priv(bus);
	ofnode subnode;
	int ret;

	plat->regbase = devfdt_get_addr_index_ptr(bus, 0);
	plat->ahbbase = devfdt_get_addr_size_index_ptr(bus, 1, &plat->ahbsize);
	plat->is_decoded_cs = dev_read_bool(bus, "cdns,is-decoded-cs");
	plat->fifo_depth = dev_read_u32_default(bus, "cdns,fifo-depth", 128);
	plat->fifo_width = dev_read_u32_default(bus, "cdns,fifo-width", 4);
	plat->trigger_address = dev_read_u32_default(bus,
						     "cdns,trigger-address",
						     0);
	plat->phase_detect_selector = dev_read_u32_default(bus,
							   "cdns,phase-detect-selector",
							   CQSPI_REG_PHY_DLL_MASTER_DLY_ELMTS_LEN + 1);
	/* Use DAC mode only when MMIO window is at least 8M wide */
	if (plat->ahbsize >= SZ_8M)
		priv->use_dac_mode = true;

	plat->is_dma = dev_read_bool(bus, "cdns,is-dma");

	/* All other parameters are embedded in the child node */
	subnode = cadence_qspi_get_subnode(bus);
	if (!ofnode_valid(subnode)) {
		printf("Error: subnode with SPI flash config missing!\n");
		return -ENODEV;
	}

	/* Use 500 KHz as a suitable default */
	plat->max_hz = ofnode_read_u32_default(subnode, "spi-max-frequency",
					       500000);

	/* Read other parameters from DT */
	plat->page_size = ofnode_read_u32_default(subnode, "page-size", 256);
	plat->block_size = ofnode_read_u32_default(subnode, "block-size", 16);
	plat->tshsl_ns = ofnode_read_u32_default(subnode, "cdns,tshsl-ns",
						 200);
	plat->tsd2d_ns = ofnode_read_u32_default(subnode, "cdns,tsd2d-ns",
						 255);
	plat->tchsh_ns = ofnode_read_u32_default(subnode, "cdns,tchsh-ns", 20);
	plat->tslch_ns = ofnode_read_u32_default(subnode, "cdns,tslch-ns", 20);
	/*
	 * Read delay should be an unsigned value but we use a signed integer
	 * so that negative values can indicate that the device tree did not
	 * specify any signed values and we need to perform the calibration
	 * sequence to find it out.
	 */
	plat->read_delay = ofnode_read_s32_default(subnode, "cdns,read-delay",
						   -1);
	plat->has_phy = ofnode_read_bool(subnode, "cdns,phy-mode");

	plat->phy_tx_start = ofnode_read_u32_default(subnode,
						     "cdns,phy-tx-start",
						     16);
	plat->phy_tx_end = ofnode_read_u32_default(subnode,
						   "cdns,phy-tx-end",
						   48);

	/* Find the PHY tuning pattern partition. */
	ret = cadence_spi_ofdata_phy_pattern(subnode);
	if (ret < 0)
		dev_dbg(bus, "Unable to find PHY pattern partition\n");
	else
		plat->phy_pattern_start = ret;

	debug("%s: regbase=%p ahbbase=%p max-frequency=%d page-size=%d\n",
	      __func__, plat->regbase, plat->ahbbase, plat->max_hz,
	      plat->page_size);

	return 0;
}

static const struct spi_controller_mem_ops cadence_spi_mem_ops = {
	.exec_op = cadence_spi_mem_exec_op,
	.supports_op = cadence_spi_mem_supports_op,
	.do_calibration = cadence_spi_mem_do_calibration,
};

static const struct dm_spi_ops cadence_spi_ops = {
	.set_speed	= cadence_spi_set_speed,
	.set_mode	= cadence_spi_set_mode,
	.mem_ops	= &cadence_spi_mem_ops,
	/*
	 * cs_info is not needed, since we require all chip selects to be
	 * in the device tree explicitly
	 */
};

static const struct udevice_id cadence_spi_ids[] = {
	{ .compatible = "cdns,qspi-nor" },
	{ .compatible = "ti,am654-ospi" },
	{ }
};

U_BOOT_DRIVER(cadence_spi) = {
	.name = "cadence_spi",
	.id = UCLASS_SPI,
	.of_match = cadence_spi_ids,
	.ops = &cadence_spi_ops,
	.of_to_plat = cadence_spi_of_to_plat,
	.plat_auto	= sizeof(struct cadence_spi_plat),
	.priv_auto	= sizeof(struct cadence_spi_priv),
	.probe = cadence_spi_probe,
	.remove = cadence_spi_remove,
	.flags = DM_FLAG_OS_PREPARE,
};
