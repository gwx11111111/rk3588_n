// SPDX-License-Identifier: GPL-2.0+
/**
 *
 * Driver for ROCKCHIP RK630 Ethernet PHYs
 *
 * Copyright (c) 2020, Fuzhou Rockchip Electronics Co., Ltd
 *
 * David Wu <david.wu@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/of_irq.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>

#define RK630_PHY_ID				0x00441400

/* PAGE 0 */
#define REG_INTERRUPT_STATUS			0X10
#define REG_INTERRUPT_MASK			0X11
#define REG_GLOBAL_CONFIGURATION		0X13
#define REG_MAC_ADDRESS0			0x16
#define REG_MAC_ADDRESS1			0x17
#define REG_MAC_ADDRESS2			0x18

#define REG_PAGE_SEL				0x1F

/* PAGE 1 */
#define REG_PAGE1_APS_CTRL			0x12
#define REG_PAGE1_UAPS_CONFIGURE		0X13
#define REG_PAGE1_EEE_CONFIGURE			0x17

/* PAGE 2 */
#define REG_PAGE2_AFE_CTRL			0x18

/* PAGE 6 */
#define REG_PAGE6_ADC_ANONTROL			0x10
#define REG_PAGE6_AFE_RX_CTRL			0x13
#define REG_PAGE6_AFE_TX_CTRL			0x14
#define REG_PAGE6_AFE_DRIVER2			0x15

/* PAGE 8 */
#define REG_PAGE8_AFE_CTRL			0x18

struct rk630_phy_priv {
	struct phy_device *phydev;
	bool ieee;
	int wol_irq;
	struct wake_lock wol_wake_lock;
};

static void rk630_phy_wol_enable(struct phy_device *phydev)
{
	struct net_device *ndev = phydev->attached_dev;
	u32 value;

	/* Switch to page 0 */
	phy_write(phydev, REG_PAGE_SEL, 0x0000);
	phy_write(phydev, REG_MAC_ADDRESS0, ((u16)ndev->dev_addr[0] << 8) + ndev->dev_addr[1]);
	phy_write(phydev, REG_MAC_ADDRESS1, ((u16)ndev->dev_addr[2] << 8) + ndev->dev_addr[3]);
	phy_write(phydev, REG_MAC_ADDRESS2, ((u16)ndev->dev_addr[4] << 8) + ndev->dev_addr[5]);

	value = phy_read(phydev, REG_GLOBAL_CONFIGURATION);
	value |= BIT(8);
	value &= ~BIT(7);
	value |= BIT(10);
	phy_write(phydev, REG_GLOBAL_CONFIGURATION, value);

	value = phy_read(phydev, REG_INTERRUPT_MASK);
	value |= BIT(14);
	phy_write(phydev, REG_INTERRUPT_MASK, value);
}

static void rk630_phy_wol_disable(struct phy_device *phydev)
{
	u32 value;

	/* Switch to page 0 */
	phy_write(phydev, REG_PAGE_SEL, 0x0000);
	value = phy_read(phydev, REG_GLOBAL_CONFIGURATION);
	value &= ~BIT(10);
	phy_write(phydev, REG_GLOBAL_CONFIGURATION, value);
}

static void rk630_phy_ieee_set(struct phy_device *phydev, bool enable)
{
	u32 value;

	/* Switch to page 1 */
	phy_write(phydev, REG_PAGE_SEL, 0x0100);
	value = phy_read(phydev, REG_PAGE1_EEE_CONFIGURE);
	if (enable)
		value |= BIT(3);
	else
		value &= ~BIT(3);
	phy_write(phydev, REG_PAGE1_EEE_CONFIGURE, value);
	/* Switch to page 0 */
	phy_write(phydev, REG_PAGE_SEL, 0x0000);
}

static void rk630_phy_set_uaps(struct phy_device *phydev)
{
	u32 value;

	/* Switch to page 1 */
	phy_write(phydev, REG_PAGE_SEL, 0x0100);
	value = phy_read(phydev, REG_PAGE1_UAPS_CONFIGURE);
	value |= BIT(15);
	phy_write(phydev, REG_PAGE1_UAPS_CONFIGURE, value);
	/* Switch to page 0 */
	phy_write(phydev, REG_PAGE_SEL, 0x0000);
}

static int rk630_phy_config_init(struct phy_device *phydev)
{
	u32 value;

	phy_write(phydev, 0, phy_read(phydev, 0) & ~BIT(13));

	/* Switch to page 1 */
	phy_write(phydev, REG_PAGE_SEL, 0x0100);
	/* Disable APS */
	phy_write(phydev, REG_PAGE1_APS_CTRL, 0x4824);
	/* Switch to page 2 */
	phy_write(phydev, REG_PAGE_SEL, 0x0200);
	/* PHYAFE TRX optimization */
	phy_write(phydev, REG_PAGE2_AFE_CTRL, 0x0000);
	/* Switch to page 6 */
	phy_write(phydev, REG_PAGE_SEL, 0x0600);
	/* PHYAFE TX optimization */
	phy_write(phydev, REG_PAGE6_AFE_TX_CTRL, 0x708f);
	/* PHYAFE RX optimization */
	phy_write(phydev, REG_PAGE6_AFE_RX_CTRL, 0xf000);
	phy_write(phydev, REG_PAGE6_AFE_DRIVER2, 0x1530);

	/* Switch to page 8 */
	phy_write(phydev, REG_PAGE_SEL, 0x0800);
	/* PHYAFE TRX optimization */
	phy_write(phydev, REG_PAGE8_AFE_CTRL, 0x00bc);

	/* Adjust tx level, bypass */
	value = phy_read(phydev, 0x1d);
	value |= BIT(11);
	phy_write(phydev, 0x1d, value);
	/* switch to page6 */
	phy_write(phydev, REG_PAGE_SEL, 0x0600);
	/* Enable tx level control */
	value = phy_read(phydev, REG_PAGE6_ADC_ANONTROL);
	value &= ~BIT(6);
	phy_write(phydev, REG_PAGE6_ADC_ANONTROL, value);
	/* Set tx level */
	value = phy_read(phydev, REG_PAGE6_AFE_DRIVER2);
	value &= ~GENMASK(15, 8);
	value |= 0x121a;
	phy_write(phydev, REG_PAGE6_AFE_DRIVER2, value);

	/* Switch to page 0 */
	phy_write(phydev, REG_PAGE_SEL, 0x0000);

	rk630_phy_ieee_set(phydev, true);
	/*
	 * Ultra Auto-Power Saving Mode (UAPS) is designed to
	 * save power when cable is not plugged into PHY.
	 */
	rk630_phy_set_uaps(phydev);

	return 0;
}

static irqreturn_t rk630_wol_irq_thread(int irq, void *dev_id)
{
	struct rk630_phy_priv *priv = (struct rk630_phy_priv *)dev_id;

	phy_write(priv->phydev, REG_INTERRUPT_STATUS, BIT(14));
	wake_lock_timeout(&priv->wol_wake_lock, msecs_to_jiffies(8000));
	return IRQ_HANDLED;
}

static int rk630_phy_probe(struct phy_device *phydev)
{
	struct rk630_phy_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->wol_irq = of_irq_get_byname(phydev->mdio.dev.of_node, "wol_irq");
	if (priv->wol_irq == -EPROBE_DEFER)
		return priv->wol_irq;

	if (priv->wol_irq > 0) {
		wake_lock_init(&priv->wol_wake_lock,
			       WAKE_LOCK_SUSPEND, "wol_wake_lock");
		ret = devm_request_threaded_irq(&phydev->mdio.dev, priv->wol_irq,
						NULL, rk630_wol_irq_thread,
						IRQF_TRIGGER_RISING | IRQF_SHARED | IRQF_ONESHOT,
						"wol_irq", priv);
		if (ret) {
			wake_lock_destroy(&priv->wol_wake_lock);
			phydev_err(phydev, "request wol_irq failed: %d\n", ret);
			return ret;
		}
		disable_irq(priv->wol_irq);
		enable_irq_wake(priv->wol_irq);
	}

	priv->phydev = phydev;

	return 0;
}

static void rk630_phy_remove(struct phy_device *phydev)
{
	struct rk630_phy_priv *priv = phydev->priv;

	if (priv->wol_irq > 0)
		wake_lock_destroy(&priv->wol_wake_lock);
}

static int rk630_phy_suspend(struct phy_device *phydev)
{
	struct rk630_phy_priv *priv = phydev->priv;

	if (priv->wol_irq > 0) {
		rk630_phy_wol_enable(phydev);
		phy_write(phydev, REG_INTERRUPT_MASK, BIT(14));
		enable_irq(priv->wol_irq);
	}
	return genphy_suspend(phydev);
}

static int rk630_phy_resume(struct phy_device *phydev)
{
	struct rk630_phy_priv *priv = phydev->priv;

	if (priv->wol_irq > 0) {
		rk630_phy_wol_disable(phydev);
		phy_write(phydev, REG_INTERRUPT_MASK, 0);
		disable_irq(priv->wol_irq);
	}

	return genphy_resume(phydev);
}

static struct phy_driver rk630_phy_driver[] = {
{
	.phy_id			= RK630_PHY_ID,
	.phy_id_mask		= 0xffffffff,
	.name			= "RK630 PHY",
	.features		= PHY_BASIC_FEATURES,
	.flags			= 0,
	.probe			= rk630_phy_probe,
	.remove			= rk630_phy_remove,
	.soft_reset		= genphy_soft_reset,
	.config_init		= rk630_phy_config_init,
	.config_aneg		= genphy_config_aneg,
	.read_status		= genphy_read_status,
	.suspend		= rk630_phy_suspend,
	.resume			= rk630_phy_resume,
},
};

static struct mdio_device_id __maybe_unused rk630_phy_tbl[] = {
	{ RK630_PHY_ID, 0xffffffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, rockchip_phy_tbl);

module_phy_driver(rk630_phy_driver);

MODULE_AUTHOR("David Wu <david.wu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK630 Ethernet PHY driver");
MODULE_LICENSE("GPL v2");
