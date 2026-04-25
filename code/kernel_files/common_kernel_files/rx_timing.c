// SPDX-License-Identifier: GPL-2.0
#include <net/rx_timing.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string.h>

DEFINE_PER_CPU(struct rx_timing_pcpu, rx_timing_data);
EXPORT_PER_CPU_SYMBOL(rx_timing_data);

bool rx_timing_on __read_mostly;
EXPORT_SYMBOL(rx_timing_on);

static struct dentry *rx_timing_dentry;

/* Aggregate one field across all CPUs */
#define AGG(field, out_total, out_max) do {				\
	for_each_possible_cpu(_cpu) {					\
		const struct rx_timing_pcpu *_t =			\
			per_cpu_ptr(&rx_timing_data, _cpu);		\
		out_total += READ_ONCE(_t->field);			\
	}								\
} while (0)

#define AGG3(f_ns, f_cnt, f_max, o_ns, o_cnt, o_max) do {		\
	for_each_possible_cpu(_cpu) {					\
		const struct rx_timing_pcpu *_t =			\
			per_cpu_ptr(&rx_timing_data, _cpu);		\
		o_ns  += READ_ONCE(_t->f_ns);				\
		o_cnt += READ_ONCE(_t->f_cnt);				\
		if (READ_ONCE(_t->f_max) > o_max)			\
			o_max = READ_ONCE(_t->f_max);			\
	}								\
} while (0)

static int rx_timing_show(struct seq_file *m, void *v)
{
	int _cpu;

#define SHOW3(label, f_ns, f_cnt, f_max) do {				\
	u64 ns = 0, cnt = 0, mx = 0;					\
	AGG3(f_ns, f_cnt, f_max, ns, cnt, mx);				\
	seq_printf(m, "%-24s  %12llu  %12llu  %12llu\n",		\
		   label, cnt, cnt ? ns / cnt : 0, mx);			\
} while (0)

#define SHOW2(label, f_ns, f_cnt) do {					\
	u64 ns = 0, cnt = 0;						\
	AGG(f_ns, ns, ns); AGG(f_cnt, cnt, cnt);			\
	seq_printf(m, "%-24s  %12llu  %12llu  %12s\n",			\
		   label, cnt, cnt ? ns / cnt : 0, "-");		\
} while (0)

	seq_puts(m, "RX stack timing (ns)\n");
	seq_puts(m, "=========================================="
		    "==============================\n");
	seq_printf(m, "%-24s  %12s  %12s  %12s\n",
		   "stage", "count", "avg_ns", "max_ns");
	seq_printf(m, "%-24s  %12s  %12s  %12s\n",
		   "-----", "-----", "------", "------");

	/* Driver */
	SHOW3("e1000_alloc_frag",     frag_alloc_ns, frag_alloc_cnt, frag_alloc_max);
	SHOW3("napi_build_skb",       build_skb_ns,  build_skb_cnt,  build_skb_max);
	SHOW3("copybreak_alloc",      copybreak_ns,  copybreak_cnt,  copybreak_max);
	SHOW2("prefetch_gap",         prefetch_gap_ns, prefetch_gap_cnt);

	seq_puts(m, "\n");

	/* Stack */
	SHOW3("napi_gro_receive",     gro_ns,         gro_cnt,         gro_max);
	SHOW3("__netif_receive_core", netif_core_ns,   netif_core_cnt,  netif_core_max);
	SHOW3("ip_rcv_core",          ip_rcv_ns,       ip_rcv_cnt,      ip_rcv_max);
	SHOW3("tcp_v4_rcv",           tcp_rcv_ns,      tcp_rcv_cnt,     tcp_rcv_max);
	SHOW2("tcp_queue_rcv",        tcp_queue_ns,    tcp_queue_cnt);
	SHOW3("copy_to_user",         copy_user_ns,    copy_user_cnt,   copy_user_max);

	seq_puts(m, "\n");

	/* End-to-end */
	SHOW3("e2e_softirq",          e2e_softirq_ns,  e2e_softirq_cnt, e2e_softirq_max);

	seq_printf(m, "\nenabled: %d\n", rx_timing_on);

#undef SHOW3
#undef SHOW2
	return 0;
}

static int rx_timing_open(struct inode *inode, struct file *file)
{
	return single_open(file, rx_timing_show, NULL);
}

static ssize_t rx_timing_write(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	char buf[8];
	int cpu;

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	if (buf[0] == '1') {
		rx_timing_on = true;
		pr_info("rx_timing: ON\n");
	} else if (buf[0] == '0') {
		rx_timing_on = false;
		pr_info("rx_timing: OFF\n");
	} else if (buf[0] == 'r' || buf[0] == 'R') {
		for_each_possible_cpu(cpu)
			memset(per_cpu_ptr(&rx_timing_data, cpu), 0,
			       sizeof(struct rx_timing_pcpu));
		pr_info("rx_timing: counters reset\n");
	}
	return count;
}

static const struct file_operations rx_timing_fops = {
	.owner   = THIS_MODULE,
	.open    = rx_timing_open,
	.read    = seq_read,
	.write   = rx_timing_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int __init rx_timing_init(void)
{
	rx_timing_dentry = debugfs_create_file("rx_timing", 0666,
					       NULL, NULL, &rx_timing_fops);
	return 0;
}

static void __exit rx_timing_exit(void)
{
	debugfs_remove(rx_timing_dentry);
}

core_initcall(rx_timing_init);
module_exit(rx_timing_exit);
MODULE_LICENSE("GPL");

