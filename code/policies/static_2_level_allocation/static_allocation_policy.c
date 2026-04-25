#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/skbuff.h>
#include <net/rx_timing.h>
#include <linux/ip.h>
#include <linux/tcp.h>


static int force_enable       = 0;   
static int force_nid          = 0;  
static int force_small_enable = 0;  
static int force_small_nid    = 0;

static struct kobject *numa_kobj;


static int alloc_pages_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	if (force_enable && this_cpu_read(in_rx_alloc))
		regs->dx = force_nid;   /* preferred_nid is the 3rd argument (rdx) */
	else if (force_small_enable && this_cpu_read(in_clean_alloc))
		regs->dx = force_small_nid; 
#endif
	return 0;
}


static struct kprobe alloc_kp = {
	.symbol_name = "__alloc_pages",
	.pre_handler = alloc_pages_pre_handler,
	.post_handler = NULL,
};


static int alloc_skb_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	if (force_small_enable && this_cpu_read(in_clean_alloc))
		regs->cx = force_small_nid;   /* node is the 4th argument (rcx) */
#endif
	return 0;
}


static struct kprobe skb_kp = {
	.symbol_name = "__alloc_skb",
	.pre_handler = alloc_skb_pre_handler,
	.post_handler = NULL,
};


static ssize_t enable_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", force_enable);
}
static ssize_t enable_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &force_enable);
	if (ret) return -EINVAL;
	return count;
}
static struct kobj_attribute enable_attr =
__ATTR(enable, 0664, enable_show, enable_store);


static ssize_t nid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", force_nid);
}
static ssize_t nid_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &force_nid);
	if (ret) return -EINVAL;
	return count;
}
static struct kobj_attribute nid_attr =
__ATTR(nid, 0664, nid_show, nid_store);


static ssize_t small_enable_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", force_small_enable);
}
static ssize_t small_enable_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &force_small_enable);
	if (ret) return -EINVAL;
	return count;
}
static struct kobj_attribute small_enable_attr =
__ATTR(force_small_enable, 0664, small_enable_show, small_enable_store);


static ssize_t small_nid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", force_small_nid);
}
static ssize_t small_nid_store(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int ret = kstrtoint(buf, 10, &force_small_nid);
	if (ret) return -EINVAL;
	return count;
}
static struct kobj_attribute small_nid_attr =
__ATTR(small_nid, 0664, small_nid_show, small_nid_store);


static int __init force_node_static_init(void)
{
	int ret;

	ret = register_kprobe(&alloc_kp);
	if (ret) {
		pr_err("force_node_static: alloc_kp registration failed (%d)\n", ret);
		goto err_ret;
	}

	ret = register_kprobe(&skb_kp);
	if (ret) {
		pr_err("force_node_static: skb_kp registration failed (%d)\n", ret);
		goto err_alloc_kp;
	}

	numa_kobj = kobject_create_and_add("numa_force_static", kernel_kobj);
	if (!numa_kobj) {
		ret = -ENOMEM;
		goto err_skb_kp;
	}

	ret = sysfs_create_file(numa_kobj, &enable_attr.attr);
	if (ret) {
		pr_err("force_node_static: enable file creation failed (%d)\n", ret);
		goto err_kobj;
	}

	ret = sysfs_create_file(numa_kobj, &nid_attr.attr);
	if (ret) {
		pr_err("force_node_static: nid file creation failed (%d)\n", ret);
		goto err_enable;
	}

	ret = sysfs_create_file(numa_kobj, &small_enable_attr.attr);
	if (ret) {
		pr_err("force_node_static: small_enable file creation failed (%d)\n", ret);
		goto err_nid;
	}

	ret = sysfs_create_file(numa_kobj, &small_nid_attr.attr);
	if (ret) {
		pr_err("force_node_static: small_nid file creation failed (%d)\n", ret);
		goto err_small_enable;
	}

	pr_info("force_node_static: module loaded (in_clean_alloc managed by e1000 driver)\n");
	return 0;

err_small_enable:
	sysfs_remove_file(numa_kobj, &small_enable_attr.attr);
err_nid:
	sysfs_remove_file(numa_kobj, &nid_attr.attr);
err_enable:
	sysfs_remove_file(numa_kobj, &enable_attr.attr);
err_kobj:
	kobject_put(numa_kobj);
err_skb_kp:
	unregister_kprobe(&skb_kp);
err_alloc_kp:
	unregister_kprobe(&alloc_kp);
err_ret:
	return ret;
}

static void __exit force_node_static_exit(void)
{
	sysfs_remove_file(numa_kobj, &small_nid_attr.attr);
	sysfs_remove_file(numa_kobj, &small_enable_attr.attr);
	sysfs_remove_file(numa_kobj, &nid_attr.attr);
	sysfs_remove_file(numa_kobj, &enable_attr.attr);
	kobject_put(numa_kobj);
	unregister_kprobe(&skb_kp);
	unregister_kprobe(&alloc_kp);
	pr_info("force_node_static: module unloaded\n");
}


module_init(force_node_static_init);
module_exit(force_node_static_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Hackers");
MODULE_DESCRIPTION("Selective NUMA allocation for RX path using kprobes");
