#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/skbuff.h>
#include <linux/mm.h>
#include <linux/percpu.h>

int ctr;

/*
 * This function was written to check where the allocation of data is taking place 
 * when we pass the skb to network stack
 *
 * Particularly useful to check where the copy break is allocating the data when
 * copy is done for small packets(< copybreak)
 */
static int eth_type_trans_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct sk_buff *skb;
	void           *data;
	struct page    *page;
	int             nid;


	if (!this_cpu_read(in_clean_alloc))
		return 0;
	
	/*
	 * You can remove this if you want to look for every packed
	 *
	 * WARNING: removing this may overflow dmesg due to large 
	 * number of packets
	 */
	if(ctr++)
		return 0; 
	
	if(ctr == 1000) 
		ctr = 0; 

	skb  = (struct sk_buff *)regs->di;
	data = (void *)skb->data;
	page = virt_to_page(data);
	nid  = page_to_nid(page);
	
	pr_info("eth_type_trans: skb->data[%lx] on node[%d]\n",
			(unsigned long)data, nid);
	return 0;
}


static struct kprobe eth_kp = {
	.symbol_name = "eth_type_trans",
	.pre_handler = eth_type_trans_pre_handler,
	.post_handler = NULL,
};

int init_module(void)
{
	int ret;
	printk(KERN_INFO "Setting the nodetest probe\n");
	ret = register_kprobe(&eth_kp);
        if (ret < 0) {
                printk(KERN_INFO "register_kprobe failed, returned %d\n", ret);
                return ret;
        }
        printk(KERN_INFO "Planted kprobe at %lx\n", (unsigned long)eth_kp.addr);
	ctr = 0; // periodic checking of node id so that dmesg does not overflow
        return 0;
}

void cleanup_module(void)
{
        unregister_kprobe(&eth_kp);
	printk(KERN_INFO "Removed the probe\n");
}
MODULE_LICENSE("GPL");
