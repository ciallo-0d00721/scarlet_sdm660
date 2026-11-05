// SPDX-License-Identifier: GPL-2.0-only
/* Opt-in recovery for temporary Xiaomi Wayne bring-up kernels. */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/panic_notifier.h>
#include <linux/reboot.h>
#include <soc/qcom/wayne-bootguard.h>

static bool bootguard_enabled;
static void __iomem *bootguard_reason;
static u32 bootguard_magic;
static bool bootguard_armed;

bool wayne_bootguard_is_armed(void)
{
	return bootguard_armed;
}

static int __init wayne_bootguard_setup(char *value)
{
	return !kstrtobool(value, &bootguard_enabled);
}
__setup("wayne.bootguard=", wayne_bootguard_setup);

static void wayne_bootguard_write_reason(void)
{
	writel(bootguard_magic, bootguard_reason);
	/* Flush the IMEM write before panic/restart or a subsequent watchdog bite. */
	readl(bootguard_reason);
}

static int wayne_bootguard_rearm(struct notifier_block *nb,
				unsigned long action, void *data)
{
	wayne_bootguard_write_reason();
	return NOTIFY_DONE;
}

static struct notifier_block wayne_bootguard_restart_nb = {
	.notifier_call = wayne_bootguard_rearm,
	.priority = 255,
};

static struct notifier_block wayne_bootguard_panic_nb = {
	.notifier_call = wayne_bootguard_rearm,
	.priority = 255,
};

static int __init wayne_bootguard_init(void)
{
	struct device_node *mode, *parent;
	struct resource resource;
	u32 offset;
	int ret;

	if (!bootguard_enabled || !of_machine_is_compatible("xiaomi,wayne"))
		return 0;

	mode = of_parse_phandle(of_chosen, "xiaomi,bootguard-reboot-mode", 0);
	if (!mode) {
		ret = -ENODEV;
		goto put_mode;
	}
	if (!of_device_is_compatible(mode, "syscon-reboot-mode")) {
		ret = -EINVAL;
		goto put_mode;
	}
	ret = of_property_read_u32(mode, "offset", &offset);
	if (ret)
		goto put_mode;
	ret = of_property_read_u32(mode, "mode-bootloader", &bootguard_magic);
	if (ret)
		goto put_mode;
	if ((offset & 3) || bootguard_magic != 0x77665500) {
		ret = -EINVAL;
		goto put_mode;
	}
	parent = of_get_parent(mode);
	if (!parent) {
		ret = -EINVAL;
		goto put_mode;
	}
	ret = of_address_to_resource(parent, 0, &resource);
	of_node_put(parent);
	if (ret)
		goto put_mode;
	if (resource_size(&resource) < sizeof(u32) ||
	    offset > resource_size(&resource) - sizeof(u32)) {
		ret = -EINVAL;
		goto put_mode;
	}
	bootguard_reason = ioremap(resource.start + offset, sizeof(u32));
	if (!bootguard_reason) {
		ret = -ENOMEM;
		goto put_mode;
	}
	ret = register_restart_handler(&wayne_bootguard_restart_nb);
	if (ret)
		goto unmap;
	ret = atomic_notifier_chain_register(&panic_notifier_list,
					    &wayne_bootguard_panic_nb);
	if (ret)
		goto unregister_restart;
	wayne_bootguard_write_reason();
	if (readl(bootguard_reason) != bootguard_magic) {
		ret = -EIO;
		goto unregister_panic;
	}
	bootguard_armed = true;
	pr_info("WAYNE_BOOTGUARD_ARMED bootloader=%#x offset=%#x\n",
		bootguard_magic, offset);
	of_node_put(mode);
	return 0;

unregister_panic:
	atomic_notifier_chain_unregister(&panic_notifier_list,
					&wayne_bootguard_panic_nb);
unregister_restart:
	unregister_restart_handler(&wayne_bootguard_restart_nb);
unmap:
	iounmap(bootguard_reason);
	bootguard_reason = NULL;
put_mode:
	of_node_put(mode);
	pr_err("WAYNE_BOOTGUARD_NOT_ARMED error=%d\n", ret);
	return ret;
}
arch_initcall(wayne_bootguard_init);
