#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/mm.h>

static int __kprobes before(struct kprobe *p, struct pt_regs *regs)
{
	void *data = (void *)regs->di;
	struct page* page = virt_to_page(data); 
	int nid = page_to_nid(page); 

	pr_info("napi_build_skb called for addr[%lx] on node[%d]\n", (unsigned long)data, nid); 
	return 0;
}

static struct kprobe kp = {
    .symbol_name   = "napi_build_skb",
	.pre_handler = before,
	.post_handler = NULL,
};

int init_module(void)
{
	int ret;
	printk(KERN_INFO "Setting the nodetest probe\n");
	ret = register_kprobe(&kp);
        if (ret < 0) {
                printk(KERN_INFO "register_kprobe failed, returned %d\n", ret);
                return ret;
        }
        printk(KERN_INFO "Planted kprobe at %lx\n", (unsigned long)kp.addr);
        return 0;
}

void cleanup_module(void)
{
        unregister_kprobe(&kp);
	printk(KERN_INFO "Removed the probe\n");
}
MODULE_LICENSE("GPL");
