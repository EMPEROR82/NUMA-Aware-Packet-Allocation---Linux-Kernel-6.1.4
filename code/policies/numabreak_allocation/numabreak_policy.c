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

static int slow_nid = 1;

static int __alloc_pages_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	if (this_cpu_read(in_rx_alloc))
		regs->dx = dma_nid; 
	else if (this_cpu_read(in_clean_alloc))
		regs->dx = dma_nid;
#endif
	return 0;
}

static struct kprobe alloc_kp = {
	.symbol_name = "__alloc_pages",
	.pre_handler = __alloc_pages_pre_handler,
	.post_handler = NULL,
};

static int __alloc_skb_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
#ifdef CONFIG_X86_64
	int pkt_size = regs->di;
	if (this_cpu_read(in_clean_alloc) && pkt_size <= numabreak)
		regs->cx = slow_nid;  
#endif
	return 0;
}


static struct kprobe skb_kp = {
	.symbol_name = "__alloc_skb",
	.pre_handler = __alloc_skb_pre_handler,
	.post_handler = NULL,
};

static int __init numabreak_policy_init(void)
{
	int ret;
	
    ret = register_kprobe(&alloc_kp);
	if (ret) {
		pr_err("numabreak_policy: alloc_kp registration failed (%d)\n", ret);
		goto err_ret;
	}

	ret = register_kprobe(&skb_kp);
	if (ret) {
		pr_err("numabreak_policy: skb_kp registration failed (%d)\n", ret);
		goto err_alloc_kp;
	}

	pr_info("numabreak_policy: module loaded (in_clean_alloc managed by e1000 driver)\n");
	return 0;

err_alloc_kp:
	unregister_kprobe(&alloc_kp);
err_ret:
	return ret;
}

static void __exit numabreak_policy_exit(void)
{
	unregister_kprobe(&skb_kp);
	unregister_kprobe(&alloc_kp);
	pr_info("numabreak_policy: module unloaded\n");
}

module_init(numabreak_policy_init);
module_exit(numabreak_policy_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Hackers");
MODULE_DESCRIPTION("Selective NUMA allocation for RX path using kprobes");
