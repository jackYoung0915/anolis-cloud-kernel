/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#ifndef _LINUX_VKERNEL_H
#define _LINUX_VKERNEL_H

#include <linux/types.h>
#include <linux/sched.h>
#include <asm/syscall.h>
#include <linux/hashtable.h>
#include <linux/pid_namespace.h>
#include <linux/utsname.h>
#include <linux/capability.h>
#include <linux/percpu_counter.h>
#include <linux/mempolicy.h>
#include <linux/fs.h>

#include <net/busy_poll.h>
#include <net/tcp.h>

#include <asm/vkernel.h>

#define VKERNEL_API_VERSION	1

#define VKERNEL_NAME_LEN 64
#define VKERNEL_PATH_MAX 128
#define VKERNEL_ACL_HASH_BITS 8

#define NOT_FOUND 0x8000
#define IOP_VKERNEL_REG 0x8000
#define IOP_VKERNEL_DIR 0x4000

/* Refer KVM */
#define VKERNELIO 0xAF

/* System/VK IOCTL list */
#define VKERNEL_GET_API_VERSION		_IO(VKERNELIO, 0x00)
#define VKERNEL_CREATE_VK			_IO(VKERNELIO, 0x01)
#define VKERNEL_DESTROY_VK			_IO(VKERNELIO, 0x02)
#define VKERNEL_CHECK_EXTENSION		_IO(VKERNELIO, 0x03)
#define VKERNEL_TRACE_ENABLE		_IO(VKERNELIO, 0x04)
#define VKERNEL_TRACE_PAUSE			_IO(VKERNELIO, 0x05)
#define VKERNEL_TRACE_DISABLE		_IO(VKERNELIO, 0x06)
#define VKERNEL_SET_DEF_SYSCALL		_IO(VKERNELIO, 0x07)
#define VKERNEL_RESTRICT_SYSCALL	_IO(VKERNELIO, 0x08)
#define VKERNEL_RESTRICT_FILE		_IO(VKERNELIO, 0x09)
#define VKERNEL_RESTRICT_LINUX_CAP	_IO(VKERNELIO, 0x0a)
#define VKERNEL_SET_CPU_PREF		_IO(VKERNELIO, 0X0b)
#define VKERNEL_SET_MEMORY_PREF		_IO(VKERNELIO, 0X0c)
#define VKERNEL_SET_SYSCTL_FS		_IO(VKERNELIO, 0X0d)
#define VKERNEL_SET_SYSCTL_KERNEL	_IO(VKERNELIO, 0x0e)
#define VKERNEL_SET_SYSCTL_NET		_IO(VKERNELIO, 0x0f)
#define VKERNEL_SET_SYSCTL_VM		_IO(VKERNELIO, 0x10)
#define VKERNEL_ENABLE_CAP			_IO(VKERNELIO, 0x11)
#define VKERNEL_REGISTER			_IO(VKERNELIO, 0x12)
#define VKERNEL_UNREGISTER			_IO(VKERNELIO, 0x13)
#define VKERNEL_ACTIVATE			_IO(VKERNELIO, 0x14)
#define VKERNEL_DEACTIVATE			_IO(VKERNELIO, 0x15)

/* syscall condition compare operations */
#define VKERNEL_SYSCALL_CMP_ED		0 /* invalid op, means the end of conditions */
#define VKERNEL_SYSCALL_CMP_EQ		1 /* equal, arg == val */
#define VKERNEL_SYSCALL_CMP_NE		2 /* not equal, arg != val */
#define VKERNEL_SYSCALL_CMP_LT		3 /* less than, arg < val */
#define VKERNEL_SYSCALL_CMP_LE		4 /* less than or equal, arg <= val */
#define VKERNEL_SYSCALL_CMP_GT		5 /* greater than, arg > val */
#define VKERNEL_SYSCALL_CMP_GE		6 /* greater than or equal, arg >= val */
#define VKRENEL_SYSCALL_CMP_ME		7 /* masked equal, arg & mask == val, mask is val1 */

/* syscall rule actions */
#define VKERNEL_SYSCALL_ACT_INVALID			0
#define VKERNEL_SYSCALL_ACT_KILL_PROCESS	1
#define VKERNEL_SYSCALL_ACT_KILL_THREAD		2
#define VKERNEL_SYSCALL_ACT_TRAP			3
#define VKERNEL_SYSCALL_ACT_ERRNO			4
#define VKERNEL_SYSCALL_ACT_USER_NOTIF		5
#define VKERNEL_SYSCALL_ACT_TRACE			6
#define VKERNEL_SYSCALL_ACT_LOG				7
#define VKERNEL_SYSCALL_ACT_ALLOW			8

#define VKERNEL_SYSCALL_ACT_BITS		16
#define VKERNEL_SYSCALL_ERRNO_BITS		16
#define VKERNEL_SYSCALL_ERRNO_MASK		((1U << VKERNEL_SYSCALL_ERRNO_BITS) - 1)

/* Extension capability list */
#define VKERNEL_CAP_ISOLATE_LOG				0
#define VKERNEL_CAP_ISOLATE_ANON			1
#define VKERNEL_CAP_ISOLATE_ANON_PIPE		2
#define VKERNEL_CAP_ISOLATE_RAMFS			3
#define VKERNEL_CAP_NUM						4

#define current_vk_task	get_current_syscall_task()
#define current_vk		get_current_syscall_vk()

struct vkernel_desc {
	char custom[VKERNEL_NAME_LEN];
	int pid;
};

struct vkernel_syscall_cond {
	u16 index; /* argument index 0-5 */
	u16 op; /* compare option */
	unsigned long oprand1; /* compared value */
	unsigned long oprand2; /* optional masked value */
};

struct vkernel_syscall_rule_desc {
	u32 nr; /* syscall nr */
	u32 act; /* action when conditions matched, [31:16]act|[15:0]errno */
	struct vkernel_syscall_cond conds[6]; /* optional conditions */
};

struct vkernel_syscall_rule {
	struct list_head link;
	u32 act;
	struct vkernel_syscall_cond conds[6];
};

struct vkernel_syscall {
	sys_call_vk_t table[NR_syscalls + 1];
	struct list_head rule_chains[NR_syscalls + 1];
	long (*do_futex)(u32 __user *uaddr, int op, u32 val, ktime_t *timeout,
			u32 __user *uaddr2, u32 val2, u32 val3);
	u32 def_act; /* default syscall rule action, [31:16]act|[15:0]errno */
};

struct vkernel_file_desc {
	char path[VKERNEL_PATH_MAX];
	u16 mode;
};

struct vkernel_file_desc_set {
	u64 nr_descs;
	struct vkernel_file_desc descs[];
};

struct vkernel_acl_node {
	struct hlist_node hash;
	struct list_head link;
	unsigned long ino;
	struct super_block *sb;
	char path[VKERNEL_PATH_MAX];
	unsigned short mode;
};

struct vkernel_acl {
	struct hlist_head *ht;
	int bits;
	struct list_head nodes;
	bool active;
};

struct vkernel_linux_cap {
	kernel_cap_t inheritable;
	kernel_cap_t permitted;
	kernel_cap_t effective;
	kernel_cap_t bset;
	kernel_cap_t ambient;
};

struct vkernel_sysctl_fs_desc {
	u64 file_max;
	u32 nr_open;
	s32 lease_break_time;
	s32 leases_enable;
	u32 mount_max;
};

struct vkernel_sysctl_kernel_desc {
	u32 msgmax;
	u32 msgmnb;
	u32 msgmni;
	s32 msg_next_id;
	s32 semmsl;
	s32 semmns;
	s32 semopm;
	s32 semmni;
	s32 sem_next_id;
	u64 shmall;
	u64 shmmax;
	u64 shmmni;
	s32 shm_next_id;
	s32 shm_rmid_forced;
	s32 numa_balancing;
	s32 numa_balancing_promote_rate_limit;
	u32 sched_cfs_bandwidth_slice;
	u32 sched_child_runs_first;
	u32 sched_dl_period_max;
	u32 sched_dl_period_min;
	s32 sched_rr_timeslice;
	s32 sched_rt_period;
	s32 sched_rt_runtime;
	s32 max_threads;
	u32 key_gc_delay;
	u32 key_persistent_keyring_expiry;
	u32 key_quota_maxbytes;
	u32 key_quota_maxkeys;
	u32 key_quota_root_maxbytes;
	u32 key_quota_root_maxkeys;
	s32 pty_limit;
	s32 pty_reserve;
};

struct vkernel_sysctl_net_desc {
	u32 nf_conntrack_max;
	u32 core_busy_poll;
	u32 core_busy_read;
	s32 core_dev_weight;
	s32 core_netdev_budget;
	s32 core_netdev_budget_us;
	s32 core_netdev_max_backlog;
	s32 core_optmem_max;
	u32 core_wmem_max;
	u32 core_rmem_max;
	u32 core_wmem_default;
	u32 core_rmem_default;

	/* net ns fileds */

	u32 core_somaxconn;

	u8 ipv4_icmp_echo_ignore_all;
	u8 ipv4_icmp_echo_enable_probe;
	u8 ipv4_icmp_echo_ignore_broadcasts;
	u8 ipv4_icmp_ignore_bogus_error_responses;
	u8 ipv4_icmp_errors_use_inbound_ifaddr;
	u32 ipv4_icmp_ratelimit;
	u32 ipv4_icmp_ratemask;
	s32 ipv4_ip_local_port_range[2];
	s32 ipv4_max_tw_buckets;
	u8 ipv4_tcp_ecn;
	u8 ipv4_tcp_ecn_fallback;
	u8 ipv4_ip_default_ttl;
	u8 ipv4_ip_no_pmtu_disc;
	u8 ipv4_ip_fwd_use_pmtu;
	u8 ipv4_ip_fwd_update_priority;
	u8 ipv4_ip_nonlocal_bind;
	u8 ipv4_ip_autobind_reuse;
	u8 ipv4_ip_dynaddr;
	u8 ipv4_ip_early_demux;
	u8 ipv4_tcp_early_demux;
	u8 ipv4_udp_early_demux;
	u8 ipv4_nexthop_compat_mode;
	u8 ipv4_fwmark_reflect;
	u8 ipv4_tcp_fwmark_accept;
	u8 ipv4_tcp_mtu_probing;
	s32 ipv4_tcp_mtu_probe_floor;
	s32 ipv4_tcp_base_mss;
	s32 ipv4_tcp_min_snd_mss;
	s32 ipv4_tcp_probe_threshold;
	u32 ipv4_tcp_probe_interval;
	s32 ipv4_tcp_keepalive_time;
	s32 ipv4_tcp_keepalive_intvl;
	u8 ipv4_tcp_keepalive_probes;
	u8 ipv4_tcp_syn_retries;
	u8 ipv4_tcp_synack_retries;
	u8 ipv4_tcp_syncookies;
	u8 ipv4_tcp_migrate_req;
	u8 ipv4_tcp_comp_sack_nr;
	s32 ipv4_tcp_reordering;
	u8 ipv4_tcp_retries1;
	u8 ipv4_tcp_retries2;
	u8 ipv4_tcp_orphan_retries;
	u8 ipv4_tcp_tw_reuse;
	s32 ipv4_tcp_fin_timeout;
	u32 ipv4_tcp_notsent_lowat;
	u8 ipv4_tcp_sack;
	u8 ipv4_tcp_window_scaling;
	u8 ipv4_tcp_timestamps;
	u8 ipv4_tcp_early_retrans;
	u8 ipv4_tcp_recovery;
	u8 ipv4_tcp_thin_linear_timeouts;
	u8 ipv4_tcp_slow_start_after_idle;
	u8 ipv4_tcp_retrans_collapse;
	u8 ipv4_tcp_stdurg;
	u8 ipv4_tcp_rfc1337;
	u8 ipv4_tcp_abort_on_overflow;
	u8 ipv4_tcp_fack;
	s32 ipv4_tcp_max_reordering;
	s32 ipv4_tcp_adv_win_scale;
	u8 ipv4_tcp_dsack;
	u8 ipv4_tcp_app_win;
	u8 ipv4_tcp_frto;
	u8 ipv4_tcp_nometrics_save;
	u8 ipv4_tcp_no_ssthresh_metrics_save;
	u8 ipv4_tcp_moderate_rcvbuf;
	u8 ipv4_tcp_tso_win_divisor;
	u8 ipv4_tcp_workaround_signed_windows;
	s32 ipv4_tcp_limit_output_bytes;
	s32 ipv4_tcp_challenge_ack_limit;
	s32 ipv4_tcp_min_rtt_wlen;
	u8 ipv4_tcp_min_tso_segs;
	u8 ipv4_tcp_tso_rtt_log;
	u8 ipv4_tcp_autocorking;
	u8 ipv4_tcp_reflect_tos;
	s32 ipv4_tcp_invalid_ratelimit;
	s32 ipv4_tcp_pacing_ss_ratio;
	s32 ipv4_tcp_pacing_ca_ratio;
	s32 ipv4_tcp_wmem[3];
	s32 ipv4_tcp_rmem[3];
	u32 ipv4_tcp_child_ehash_entries;
	u64 ipv4_tcp_comp_sack_delay_ns;
	u64 ipv4_tcp_comp_sack_slack_ns;
	s32 ipv4_max_syn_backlog;
	s32 ipv4_tcp_fastopen;
	u32 ipv4_tcp_fastopen_blackhole_timeout;
	char ipv4_tcp_congestion_control[TCP_CA_NAME_MAX];
	u8 ipv4_tcp_plb_enabled;
	u8 ipv4_tcp_plb_idle_rehash_rounds;
	u8 ipv4_tcp_plb_rehash_rounds;
	u8 ipv4_tcp_plb_suspend_rto_sec;
	s32 ipv4_tcp_plb_cong_thresh;
	s32 ipv4_udp_wmem_min;
	s32 ipv4_udp_rmem_min;
	u8 ipv4_fib_notify_on_flag_change;
	u8 ipv4_igmp_llm_reports;
	s32 ipv4_igmp_max_memberships;
	s32 ipv4_igmp_max_msf;
	s32 ipv4_igmp_qrv;
	u32 ipv4_fib_multipath_hash_fields;
	u8 ipv4_fib_multipath_use_neigh;
	u8 ipv4_fib_multipath_hash_policy;

	s32 ipv4_conf_all[IPV4_DEVCONF_MAX];
	s32 ipv4_conf_default[IPV4_DEVCONF_MAX];

	s32 unix_max_dgram_qlen;
};

struct vkernel_sysctl_fs {
	/* file */
	struct files_stat_struct files_stat;
	unsigned int nr_open;
	long old_max;
	struct percpu_counter nr_files;
	/* inode */
	struct inodes_stat_t inodes_stat;
	unsigned long __percpu *nr_inodes;
	unsigned long __percpu *nr_unused;
	/* lease lock */
	int leases_enable;
	int lease_break_time;
	/* mount */
	unsigned int mount_max;
};

struct vkernel_sysctl_kernel {
	/* TODO: numa balancing, implemented at mem cgroup? */
	int nb_mode;
	int nb_promote_rate_limit;
	/* TODO: sched, implemented at cpu cgroup? */
	unsigned int sched_cfs_bandwidth_slice;
	unsigned int sched_child_runs_first;
	unsigned int sched_dl_period_max;
	unsigned int sched_dl_period_min;
	/* NOTE: rt has inflence on rcu */
	int sched_rr_timeslice;
	int sched_rt_period;
	int sched_rt_runtime;
	/* thread */
	int nr_threads;
	int max_threads;
	/* security keys */
	unsigned int key_gc_delay;
	unsigned int persistent_keyring_expiry;
	unsigned int key_quota_root_maxbytes;
	unsigned int key_quota_root_maxkeys;
	unsigned int key_quota_maxbytes;
	unsigned int key_quota_maxkeys;
	/* pty */
	int pty_limit;
	int pty_reserve;
	atomic_t pty_count;
};

struct vkernel_sysctl_net {
	/* netns specific */
	unsigned int nf_conntrack_max;
	/* core */
	unsigned int net_busy_poll;
	unsigned int net_busy_read;
	/* napi_struct specific, inactive (not netns specific) */
	int weight_p;
	int dev_weight_rx_bias;
	int dev_weight_tx_bias;
	int dev_rx_weight;
	int dev_tx_weight;
	/* softnet_data specific, inactive (not netns specific) */
	int netdev_budget;
	unsigned int netdev_budget_usecs;
	int netdev_max_backlog;
	/* sock specific (netns specific) */
	int optmem_max;
	u32 wmem_max;
	u32 rmem_max;
	u32 wmem_default;
	u32 rmem_default;
	/* global (not netns specific) */
	// struct rps_sock_flow_table __rcu *rps_sock_flow_table;
	/* netns core, ipv4, ipv4 conf, unix */
	struct net *net;
};

struct vkernel;

struct vkernel_ops {
	int (*cap_capable)(struct vkernel *vk, const struct cred *cred,
			struct user_namespace *targ_ns,	int cap, unsigned int opts);
	int (*generic_permission)(struct vkernel *vk, struct mnt_idmap *idmap,
			struct inode *inode, int mask);
};

struct vkernel_custom_type {
	struct module *owner;
	struct hlist_node hash;
	char name[VKERNEL_NAME_LEN];
	int (*post_create)(struct vkernel *vk);
	void (*pre_destroy)(struct vkernel *vk);
};

struct vkernel {
	/* basic */
	char name[VKERNEL_NAME_LEN];
	struct hlist_node hash;
	struct list_head link;
	struct pid_namespace *pid_ns;
	struct uts_namespace *uts_ns;
	struct task_struct *init_process;
	int init_pid;
	refcount_t users_count;
	bool active;

	/* security */
	struct vkernel_syscall syscall;
	struct vkernel_acl acl;
	struct vkernel_linux_cap linux_cap;

	/* extension caps */
	unsigned long caps;
	unsigned int log_ns;

	/* sysctl */
	struct vkernel_sysctl_fs sysctl_fs;
	struct vkernel_sysctl_kernel sysctl_kernel;
	struct vkernel_sysctl_net sysctl_net;

	/* operation */
	struct vkernel_ops ops;

	/* custom */
	struct vkernel_custom_type *custom;
	void *private;

	/* debug */
	struct dentry *debugfs_dentry;
};

struct vkernel *vkernel_find_vk_by_id(unsigned int id);
struct vkernel *vkernel_find_vk_by_task(struct task_struct *tsk);
int vkernel_register_vk(struct vkernel *vk);
int vkernel_unregister_vk(struct vkernel *vk);

struct vkernel *vkernel_create_vk(struct task_struct *tsk, const char *name,
			const char *custom);
void vkernel_destroy_vk(struct vkernel *vk);

void vkernel_get_vk(struct vkernel *vk);
bool vkernel_get_vk_safe(struct vkernel *vk);
void vkernel_put_vk(struct vkernel *vk);
void vkernel_put_vk_no_destroy(struct vkernel *vk);

int vkernel_set_syscall(struct vkernel_syscall *syscall, unsigned int nr,
			sys_call_vk_t call);
int vkernel_set_default_syscall_rule(struct vkernel_syscall *syscall, u32 act);
int vkernel_add_syscall_rule(struct vkernel_syscall *syscall,
			struct vkernel_syscall_rule_desc *desc);

int vkernel_set_acl(struct vkernel_acl *acl, char *path, unsigned short mode);
int vkernel_clear_acl(struct vkernel_acl *acl, char *path);
int vkernel_set_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set);
int vkernel_clear_acl_set(struct vkernel_acl *acl, struct vkernel_file_desc_set *set);

int vkernel_set_linux_cap(struct vkernel *vk, struct vkernel_linux_cap *cap);

int vkernel_set_sysctl_fs(struct vkernel_sysctl_fs *fs, struct vkernel_sysctl_fs_desc *desc);
int vkernel_set_sysctl_kernel(struct vkernel_sysctl_kernel *k,
			struct vkernel_sysctl_kernel_desc *desc);
int vkernel_set_sysctl_net(struct vkernel_sysctl_net *net, struct vkernel_sysctl_net_desc *desc);

struct vkernel_custom_type *vkernel_find_custom(const char *name);
int vkernel_register_custom(struct vkernel_custom_type *custom);
int vkernel_unregister_custom(struct vkernel_custom_type *custom);

#endif
