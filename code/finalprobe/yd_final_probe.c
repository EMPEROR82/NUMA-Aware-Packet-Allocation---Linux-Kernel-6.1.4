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

/* ========================= */
/*       CONFIG STATE        */
/* ========================= */
static int force_enable       = 0;   /* enable NUMA forcing for RX page allocs */
static int force_nid          = 0;   /* target NUMA node for RX page allocs    */
static int force_small_enable = 0;   /* enable NUMA forcing for SKB allocs     */
static int force_small_nid    = 0;   /* target NUMA node for SKB allocs        */


static struct kobject *numa_kobj;

/*
 * This function was written to check where the allocation of data is taking place 
 * when we pass the skb to network stack
 *
 * Particularly useful to check where the copy break is allocating the data when
 * copy is done for small packets(< copybreak)
 */
static int eth_type_trans_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
/*
	struct sk_buff *skb;
	void           *data;
	struct page    *page;
	int             nid;


	if (!this_cpu_read(in_clean_alloc))
		return 0;


	skb  = (struct sk_buff *)regs->di;
	data = (void *)skb->data;
	page = virt_to_page(data);
	nid  = page_to_nid(page);

	pr_info("eth_type_trans: skb->data[%lx] on node[%d]\n",
			(unsigned long)data, nid);
*/
	return 0;
}


static struct kprobe eth_kp = {
	.symbol_name = "eth_type_trans",
	.pre_handler = eth_type_trans_pre_handler,
	.post_handler = NULL,
};



/* ============================================================ */
/*  KPROBE: __alloc_pages                                        */
/*  Purpose: Redirect the preferred_nid argument (rdx / 3rd     */
/*           arg) to force_nid when inside the RX frag alloc   */
/*           path so pages land on the desired NUMA node.       */
/* ============================================================ */


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



/* ============================================================ */
/*  KPROBE: __alloc_skb                                          */
/*  Purpose: Redirect the node argument (rcx / 4th arg) to      */
/*           force_small_nid when inside e1000 RX clean loop    */
/*           so SKB slab memory lands on the desired NUMA node. */
/* ============================================================ */


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



/* ========================= */
/*        SYSFS              */
/* ========================= */


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


	/* 1. __alloc_pages — entry kprobe (rewrites preferred_nid) */
	ret = register_kprobe(&alloc_kp);
	if (ret) {
		pr_err("force_node: alloc_kp registration failed (%d)\n", ret);
		goto err_ret;
	}


	/* 2. __alloc_skb — entry kprobe (rewrites node argument) */
	ret = register_kprobe(&skb_kp);
	if (ret) {
		pr_err("force_node: skb_kp registration failed (%d)\n", ret);
		goto err_alloc_kp;
	}


	/* 3. eth_type_trans — entry kprobe (debug: log actual NUMA node) */
	ret = register_kprobe(&eth_kp);
	if (ret) {
		pr_err("force_node: eth_kp registration failed (%d)\n", ret);
		goto err_skb_kp;
	}


	/* 4. sysfs — expose enable/nid knobs under /sys/kernel/numa_force/ */
	numa_kobj = kobject_create_and_add("numa_force", kernel_kobj);
	if (!numa_kobj) {
		ret = -ENOMEM;
		goto err_eth_kp;
	}


	ret  = sysfs_create_file(numa_kobj, &enable_attr.attr);
	ret |= sysfs_create_file(numa_kobj, &nid_attr.attr);
	ret |= sysfs_create_file(numa_kobj, &small_enable_attr.attr);
	ret |= sysfs_create_file(numa_kobj, &small_nid_attr.attr);
	if (ret) {
		pr_err("force_node: one or more sysfs_create_file calls failed\n");
		goto err_kobj;
	}


	pr_info("force_node: module loaded (in_clean_alloc managed by e1000 driver)\n");
	return 0;


	/* ---- unwind on any failure (reverse registration order) ---- */
err_kobj:
	kobject_put(numa_kobj);
err_eth_kp:
	unregister_kprobe(&eth_kp);
err_skb_kp:
	unregister_kprobe(&skb_kp);
err_alloc_kp:
	unregister_kprobe(&alloc_kp);
err_ret:
	return ret;
}



/* ========================= */
/*        EXIT               */
/* ========================= */


static void __exit force_node_exit(void)
{
	/* Tear down in reverse registration order */
	kobject_put(numa_kobj);
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
