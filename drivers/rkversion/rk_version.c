// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Xiao Yapeng, yp.xiao@rock-chips.com
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>

#define SDK_VERSION_NAME	"sdk_version"
#define SDK_MISC_MINOR		142

static const char sdk_version[] = "sdk release v1.3.0\n";

struct sdk_version_dev {
	struct miscdevice misc;
	bool is_show;
};

struct sdk_version_dev *sdk_version_devp;

static int sdk_version_dev_open(struct inode *inode, struct file *file)
{
	file->private_data = sdk_version_devp;
	sdk_version_devp->is_show = 0;

	return 0;
}

static int sdk_version_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t sdk_version_dev_read(struct file *filp, char __user *buf, size_t count,
				loff_t *f_pos)
{
	struct sdk_version_dev *dev = filp->private_data;

	if (copy_to_user(buf, sdk_version, sizeof(sdk_version)))
		return -EFAULT;

	if (dev->is_show == 0) {
		dev->is_show = 1;
		return sizeof(sdk_version);
	}

	return 0;
}

static const struct file_operations sdk_version_fops = {
	.owner = THIS_MODULE,
	.open = sdk_version_dev_open,
	.read = sdk_version_dev_read,
	.release = sdk_version_dev_release,
};

static int __init sdk_version_init(void)
{
	int ret;

	sdk_version_devp = kzalloc(sizeof(struct sdk_version_dev), GFP_KERNEL);
	if (!sdk_version_devp) {
		ret = -ENOMEM;
		goto fail_malloc;
	}

	sdk_version_devp->misc.minor = SDK_MISC_MINOR;
	sdk_version_devp->misc.name = "sdk_version";
	sdk_version_devp->misc.fops = &sdk_version_fops;

	ret = misc_register(&sdk_version_devp->misc);
	if (ret < 0)
		printk("Error: misc_register returned %d\n", ret);

	return 0;

fail_malloc:
	return ret;
}

static void __exit sdk_version_exit(void)
{
	misc_deregister(&sdk_version_devp->misc);
	kfree(sdk_version_devp);
}

module_init(sdk_version_init)
module_exit(sdk_version_exit)

MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0.0");
MODULE_AUTHOR("Xiao Yapeng yp.xiao@rock-chips.com");
MODULE_DESCRIPTION("Rockchip sdk version driver");
