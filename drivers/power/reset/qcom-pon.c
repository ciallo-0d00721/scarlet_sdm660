// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-18 Linaro Limited

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/regmap.h>
#include <soc/qcom/wayne-bootguard.h>

#define PON_SOFT_RB_SPARE		0x8f

#define GEN1_REASON_SHIFT		2
#define GEN2_REASON_SHIFT		1

#define NO_REASON_SHIFT			0

struct pm8916_pon {
	struct device *dev;
	struct regmap *regmap;
	u32 baseaddr;
	struct reboot_mode_driver reboot_mode;
	long reason_shift;
};

static int pm8916_reboot_mode_write(struct reboot_mode_driver *reboot,
				    unsigned int magic)
{
	struct pm8916_pon *pon = container_of
			(reboot, struct pm8916_pon, reboot_mode);
	int ret;

	ret = regmap_update_bits(pon->regmap,
				 pon->baseaddr + PON_SOFT_RB_SPARE,
				 GENMASK(7, pon->reason_shift),
				 magic << pon->reason_shift);
	if (ret < 0)
		dev_err(pon->dev, "update reboot mode bits failed\n");

	return ret;
}

static void pm8916_pon_arm_wayne_bootloader(struct pm8916_pon *pon)
{
	struct device_node *np = pon->dev->of_node;
	unsigned int reason;
	u32 magic;
	int ret;

	if (!wayne_bootguard_is_armed() ||
	    !of_device_is_compatible(np, "qcom,pm8998-pon") ||
	    pon->baseaddr != 0x800 ||
	    of_property_read_u32(np, "mode-bootloader", &magic) || magic != 2)
		return;

	ret = pm8916_reboot_mode_write(&pon->reboot_mode, magic);
	if (!ret)
		ret = regmap_read(pon->regmap, pon->baseaddr + PON_SOFT_RB_SPARE,
				  &reason);
	if (!ret && (reason >> pon->reason_shift) != magic)
		ret = -EIO;
	if (ret)
		dev_err(pon->dev, "WAYNE_PON_BOOTGUARD_NOT_ARMED error=%d\n", ret);
	else
		dev_info(pon->dev, "WAYNE_PON_BOOTGUARD_ARMED reason=%#x\n", magic);
}

static int pm8916_pon_probe(struct platform_device *pdev)
{
	struct pm8916_pon *pon;
	long reason_shift;
	int error;

	pon = devm_kzalloc(&pdev->dev, sizeof(*pon), GFP_KERNEL);
	if (!pon)
		return -ENOMEM;

	pon->dev = &pdev->dev;

	pon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pon->regmap) {
		dev_err(&pdev->dev, "failed to locate regmap\n");
		return -ENODEV;
	}

	error = of_property_read_u32(pdev->dev.of_node, "reg",
				     &pon->baseaddr);
	if (error)
		return error;

	reason_shift = (long)of_device_get_match_data(&pdev->dev);

	if (reason_shift != NO_REASON_SHIFT) {
		pon->reboot_mode.dev = &pdev->dev;
		pon->reason_shift = reason_shift;
		pon->reboot_mode.write = pm8916_reboot_mode_write;
		error = devm_reboot_mode_register(&pdev->dev, &pon->reboot_mode);
		if (error) {
			dev_err(&pdev->dev, "can't register reboot mode\n");
			return error;
		}
	}

	pm8916_pon_arm_wayne_bootloader(pon);
	platform_set_drvdata(pdev, pon);

	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id pm8916_pon_id_table[] = {
	{ .compatible = "qcom,pm8916-pon", .data = (void *)GEN1_REASON_SHIFT },
	{ .compatible = "qcom,pm8941-pon", .data = (void *)NO_REASON_SHIFT },
	{ .compatible = "qcom,pms405-pon", .data = (void *)GEN1_REASON_SHIFT },
	{ .compatible = "qcom,pm8998-pon", .data = (void *)GEN2_REASON_SHIFT },
	{ .compatible = "qcom,pmk8350-pon", .data = (void *)GEN2_REASON_SHIFT },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8916_pon_id_table);

static struct platform_driver pm8916_pon_driver = {
	.probe = pm8916_pon_probe,
	.driver = {
		.name = "pm8916-pon",
		.of_match_table = pm8916_pon_id_table,
	},
};
#ifdef CONFIG_QCOM_WAYNE_BOOTGUARD
/* Bind when the SPMI PMIC appears, before USB device initialization. */
static int __init pm8916_pon_driver_init(void)
{
	return platform_driver_register(&pm8916_pon_driver);
}
subsys_initcall(pm8916_pon_driver_init);
#else
module_platform_driver(pm8916_pon_driver);
#endif

MODULE_DESCRIPTION("pm8916 Power On driver");
MODULE_LICENSE("GPL v2");
