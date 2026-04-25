/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_RX_TIMING_H
#define _NET_RX_TIMING_H

#include <linux/ktime.h>
#include <linux/percpu.h>

#define POLICY_RESET_LIMIT 8192
#define REDUCE_RATIO 9

extern unsigned int numabreak;

struct policy_counters {
    u32 nr_small_packets;
    u32 nr_big_packets;
    u32 nr_to_reset;
};

DECLARE_PER_CPU(struct policy_counters, packet_counter);

struct rx_timing_pcpu {
	/* --- Driver layer (e1000) --- */
	u64 frag_alloc_ns;
	u64 frag_alloc_cnt;
	u64 frag_alloc_max;

	u64 build_skb_ns;
	u64 build_skb_cnt;
	u64 build_skb_max;

	u64 copybreak_ns;
	u64 copybreak_cnt;
	u64 copybreak_max;

	u64 prefetch_gap_ns;
	u64 prefetch_gap_cnt;

	/* --- GRO layer --- */
	u64 gro_ns;
	u64 gro_cnt;
	u64 gro_max;

	/* --- Core stack --- */
	u64 netif_core_ns;
	u64 netif_core_cnt;
	u64 netif_core_max;

	/* --- IP layer --- */
	u64 ip_rcv_ns;
	u64 ip_rcv_cnt;
	u64 ip_rcv_max;

	/* --- TCP layer --- */
	u64 tcp_rcv_ns;
	u64 tcp_rcv_cnt;
	u64 tcp_rcv_max;

	/* --- Socket enqueue --- */
	u64 tcp_queue_ns;
	u64 tcp_queue_cnt;

	/* --- Copy to user (process context) --- */
	u64 copy_user_ns;
	u64 copy_user_cnt;
	u64 copy_user_max;

	/* --- End-to-end softirq --- */
	u64 e2e_softirq_ns;
	u64 e2e_softirq_cnt;
	u64 e2e_softirq_max;
};

DECLARE_PER_CPU(struct rx_timing_pcpu, rx_timing_data);
DECLARE_PER_CPU(bool, in_clean_alloc); 
DECLARE_PER_CPU(bool, in_rx_alloc); 
extern bool rx_timing_on;

static __always_inline u64 rx_ts(void)
{
	return likely(rx_timing_on) ? ktime_get_ns() : 0;
}

/* Helper to record a measurement */
static __always_inline void rx_timing_record(u64 *total, u64 *cnt, u64 *max, u64 dt)
{
	this_cpu_add(*total, dt);
	this_cpu_inc(*cnt);
	if (max && dt > __this_cpu_read(*max))
		__this_cpu_write(*max, dt);
}

#endif /* _NET_RX_TIMING_H */

