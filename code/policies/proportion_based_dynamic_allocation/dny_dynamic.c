// force_node.c — Selective NUMA allocation for RX path
//
// Uses kprobes to intercept allocation functions and redirect them
// to a specific NUMA node during e1000 RX processing.
//
// The in_clean_alloc per-CPU flag is set/cleared directly by the
// patched e1000_clean_rx_irq() in e1000_main.c, eliminating the
// need for a kprobe on that static function.

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
#include <linux/ip.h>
#include <linux/tcp.h>

#include <net/rx_timing.h>

/* ========================= */
/*       CONFIG STATE        */
/* ========================= */
static int force_enable       = 0;   /* enable NUMA forcing for RX page allocs */
static int force_nid          = 0;   /* target NUMA node for RX page allocs    */
static int force_small_enable = 0;   /* enable NUMA forcing for SKB allocs     */
static int force_small_nid    = 0;   /* target NUMA node for SKB allocs        */

extern int dny_dynamic_enabled;

static struct kobject *numa_kobj;

/* ========================= */
/*   KPROBE: eth_type_trans  */
/* ========================= */
/*
 * Debug: log which NUMA node the RX buffer landed on after the full
 * RX path completes.
 */
static int eth_type_trans_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct sk_buff *skb;
	void *data;
	struct page *page;
	int nid;

	if (!this_cpu_read(in_clean_alloc))
		return 0;

	skb = (struct sk_buff *)regs->di;
	data = (void *)skb->data;
	page = virt_to_page(data);
	nid  = page_to_nid(page);

	//pr_info("eth_type_trans: skb->data[%lx] on node[%d]\n", (unsigned long)data, nid);

	return 0;
}

static struct kprobe eth_kp = {
	.symbol_name = "eth_type_trans",
	.pre_handler = eth_type_trans_pre_handler,
};

/* ========================= */
/*   KPROBE: __alloc_pages   */
/* ========================= */
/*
 * Redirect the preferred_nid argument (rdx / 3rd arg) to force_nid
 * when inside the RX frag alloc path so pages land on the desired
 * NUMA node.
 */
static int alloc_pages_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	if (force_enable && this_cpu_read(in_rx_alloc)) {
		u32 small_packets = this_cpu_read(packet_counter.nr_small_packets);
		u32 big_packets   = this_cpu_read(packet_counter.nr_big_packets);

		if (small_packets > big_packets) {
			regs->dx = 1;
		} else {
			regs->dx = 0;
		}
	}

	if (force_enable && this_cpu_read(in_clean_alloc)) {
		u32 small_packets = this_cpu_read(packet_counter.nr_small_packets);
		u32 big_packets   = this_cpu_read(packet_counter.nr_big_packets);

		if (small_packets > big_packets) {
			regs->dx = 1;
		} else {
			regs->dx = 0;
		}
	}
#endif
	return 0;
}

static struct kprobe alloc_kp = {
	.symbol_name = "__alloc_pages",
	.pre_handler = alloc_pages_pre_handler,
};

/* ========================= */
/*   KPROBE: __alloc_skb     */
/* ========================= */
/*
 * Redirect the node argument (rcx / 4th arg) to force_small_nid when
 * inside e1000 RX clean loop so SKB slab memory lands on the desired
 * NUMA node.
 */
static int alloc_skb_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	if (force_small_enable && this_cpu_read(in_clean_alloc)) {
		u32 small_packets = this_cpu_read(packet_counter.nr_small_packets);
		u32 big_packets   = this_cpu_read(packet_counter.nr_big_packets);

		if (small_packets > big_packets) {
			regs->cx = 1;
		} else {
			regs->cx = 0;
		}
	}
#endif
	return 0;
}

static struct kprobe skb_kp = {
	.symbol_name = "__alloc_skb",
	.pre_handler = alloc_skb_pre_handler,
};

/* ========================= */
/*        SYSFS              */
/* ========================= */

static ssize_t enable_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(force_enable));
}

static ssize_t enable_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	WRITE_ONCE(force_enable, val);
	return count;
}

static struct kobj_attribute enable_attr =
	__ATTR(enable, 0664, enable_show, enable_store);

static ssize_t nid_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(force_nid));
}

static ssize_t nid_store(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	WRITE_ONCE(force_nid, val);
	return count;
}

static struct kobj_attribute nid_attr =
	__ATTR(nid, 0664, nid_show, nid_store);

static ssize_t small_enable_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(force_small_enable));
}

static ssize_t small_enable_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	WRITE_ONCE(force_small_enable, val);
	return count;
}

static struct kobj_attribute small_enable_attr =
	__ATTR(force_small_enable, 0664, small_enable_show, small_enable_store);

static ssize_t small_nid_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(force_small_nid));
}

static ssize_t small_nid_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	WRITE_ONCE(force_small_nid, val);
	return count;
}

static struct kobj_attribute small_nid_attr =
	__ATTR(small_nid, 0664, small_nid_show, small_nid_store);

static ssize_t dny_dynamic_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(dny_dynamic_enabled));
}

static ssize_t dny_dynamic_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	WRITE_ONCE(dny_dynamic_enabled, val);
	return count;
}

/* FIXED: this attribute must be named dny_dynamic */
static struct kobj_attribute dny_dynamic_attr =
	__ATTR(dny_dynamic, 0664, dny_dynamic_show, dny_dynamic_store);

static struct attribute *numa_force_attrs[] = {
	&enable_attr.attr,
	&nid_attr.attr,
	&small_enable_attr.attr,
	&small_nid_attr.attr,
	&dny_dynamic_attr.attr,
	NULL,
};

static const struct attribute_group numa_force_group = {
	.attrs = numa_force_attrs,
};

/* ========================= */
/*        INIT               */
/* ========================= */
/*
 * Registration order:
 *
 *   1. alloc_kp    — rewrites preferred_nid inside __alloc_pages (guarded by in_rx_alloc)
 *   2. skb_kp      — rewrites node arg      inside __alloc_skb   (guarded by in_clean_alloc)
 *   3. eth_kp      — logs NUMA node         inside eth_type_trans (guarded by in_clean_alloc)
 *   4. sysfs       — exposes enable/nid knobs
 *
 * NOTE: in_clean_alloc is managed directly by e1000_clean_rx_irq()
 *       in the patched e1000 driver (e1000_main.c). The old kprobe/kretprobe
 *       on e1000_clean_rx_irq has been removed because that static function's
 *       symbol is not in kallsyms and is not kprobe-safe.
 */
static int __init force_node_init(void)
{
	int ret;

	ret = register_kprobe(&alloc_kp);
	if (ret) {
		pr_err("force_node: alloc_kp registration failed (%d)\n", ret);
		return ret;
	}

	ret = register_kprobe(&skb_kp);
	if (ret) {
		pr_err("force_node: skb_kp registration failed (%d)\n", ret);
		goto err_alloc_kp;
	}

	ret = register_kprobe(&eth_kp);
	if (ret) {
		pr_err("force_node: eth_kp registration failed (%d)\n", ret);
		goto err_skb_kp;
	}

	numa_kobj = kobject_create_and_add("numa_force", kernel_kobj);
	if (!numa_kobj) {
		ret = -ENOMEM;
		goto err_eth_kp;
	}

	ret = sysfs_create_group(numa_kobj, &numa_force_group);
	if (ret) {
		pr_err("force_node: sysfs_create_group failed (%d)\n", ret);
		goto err_kobj;
	}

	WRITE_ONCE(dny_dynamic_enabled, 1);

	pr_info("force_node: module loaded (in_clean_alloc managed by e1000 driver)\n");
	return 0;

err_kobj:
	kobject_put(numa_kobj);
	numa_kobj = NULL;
err_eth_kp:
	unregister_kprobe(&eth_kp);
err_skb_kp:
	unregister_kprobe(&skb_kp);
err_alloc_kp:
	unregister_kprobe(&alloc_kp);
	return ret;
}

/* ========================= */
/*        EXIT               */
/* ========================= */
static void __exit force_node_exit(void)
{
	WRITE_ONCE(dny_dynamic_enabled, 0);

	if (numa_kobj) {
		sysfs_remove_group(numa_kobj, &numa_force_group);
		kobject_put(numa_kobj);
		numa_kobj = NULL;
	}

	unregister_kprobe(&eth_kp);
	unregister_kprobe(&skb_kp);
	unregister_kprobe(&alloc_kp);

	pr_info("force_node: module unloaded\n");
}

/* ========================= */
/*        MODULE             */
/* ========================= */
module_init(force_node_init);
module_exit(force_node_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dnyanesh");
MODULE_DESCRIPTION("Selective NUMA allocation for RX path using kprobes");
