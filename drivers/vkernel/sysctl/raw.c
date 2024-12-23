// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/futex.h>
#include <linux/sched/sysctl.h>
#include <linux/tty.h>
#include <linux/inetdevice.h>
#include <linux/netconf.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>


#include "sysctl.h"
#include "utils.h"

enum {
	DEVCONF_ALL,
	DEVCONF_DFLT,
	DEVCONF_OTHER
};

int vkernel_set_sysctl_raw(struct vkernel *vk, char *buf)
{
	struct ipc_namespace *ipc_ns = NULL;
	struct net *n;
	char *name;
	char *val;
	char *p;
	u64 uval;
	s64 sval, old_sval, third_sval;
	bool has_uval = false, has_sval = false;

	val = strchr(buf, '=');
	if (!val)
		return -EINVAL;
	*val++ = 0;
	name = strstrip(buf);
	val = strstrip(val);

	if (!kstrtou64(val, 10, &uval))
		has_uval = true;
	else
		pr_warn("failed to parse raw sysctl val %s to u64\n", val);
	if (!kstrtos64(val, 10, &sval))
		has_sval = true;
	else
		pr_warn("failed to parse raw sysctl val %s to s64\n", val);

	if (vk->init_process->nsproxy)
		ipc_ns = vk->init_process->nsproxy->ipc_ns;

	n = vk->sysctl_net.net;

	if (!strcmp(name, "fs.file-max")) {
		if (has_uval && uval)
			vk->sysctl_fs.files_stat.max_files = uval;
	} else if (!strcmp(name, "fs.nr_open")) {
		if (has_uval && uval)
			vk->sysctl_fs.nr_open = uval;
	} else if (!strcmp(name, "fs.lease-break-time")) {
		if (has_uval && sval > 0)
			vk->sysctl_fs.leases_enable = sval;
	} else if (!strcmp(name, "fs.leases-enable")) {
		if (has_sval && (sval == 0 || sval == 1))
			vk->sysctl_fs.lease_break_time = sval;
	} else if (!strcmp(name, "fs.mount-max")) {
		if (has_uval && uval)
			vk->sysctl_fs.mount_max = uval;
	} else if (!strcmp(name, "kernel.msgmax")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->msg_ctlmax = uval;
	} else if (!strcmp(name, "kernel.msgmnb")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->msg_ctlmnb = uval;
	} else if (!strcmp(name, "kernel.msgmni")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->msg_ctlmni = uval;
	}
#ifdef CONFIG_CHECKPOINT_RESTORE
	else if (!strcmp(name, "kernel.msg_next_id")) {
		if (has_sval && ipc_ns && sval >= -1)
			ipc_ns->ids[IPC_MSG_IDS].next_id = sval;
	}
#endif
	else if (!strcmp(name, "kernel.sem")) {
		if (ipc_ns) {
			old_sval = ipc_ns->sem_ctls[3];
			uval = 0;
			while ((p = strsep(&val, " \t")) != NULL && uval < 4) {
				if (!*p)
					continue;
				if (!kstrtos64(p, 10, &sval) && sval > 0)
					ipc_ns->sem_ctls[uval] = sval;
				uval++;
			}
			if (sem_check_semmni(ipc_ns))
				ipc_ns->sem_ctls[3] = old_sval;
		}
	}
#ifdef CONFIG_CHECKPOINT_RESTORE
	else if (!strcmp(name, "kernel.sem_next_id")) {
		if (has_sval && ipc_ns && sval >= -1)
			ipc_ns->ids[IPC_SEM_IDS].next_id = sval;
	}
#endif
	else if (!strcmp(name, "kernel.shmall")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->shm_ctlall = uval;
	} else if (!strcmp(name, "kernel.shmmax")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->shm_ctlmax = uval;
	} else if (!strcmp(name, "kernel.shmmni")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->shm_ctlmni = uval;
	}
#ifdef CONFIG_CHECKPOINT_RESTORE
	else if (!strcmp(name, "kernel.shm_next_id")) {
		if (has_uval && ipc_ns && uval)
			ipc_ns->ids[IPC_SHM_IDS].next_id = uval;
	}
#endif
	else if (!strcmp(name, "kernel.shm_rmid_forced")) {
		if (has_sval && ipc_ns && (sval == 0 || sval == 1))
			ipc_ns->shm_rmid_forced = sval;
	} else if (!strcmp(name, "kernel.numa_balancing")) {
		/* inactive */
		if (has_sval && sval >= 0)
			vk->sysctl_kernel.nb_mode = sval;
	} else if (!strcmp(name, "kernel.numa_balancing_promote_rate_limit_MBps")) {
		 /* inactive */
		if (has_sval && sval > 0)
			vk->sysctl_kernel.nb_promote_rate_limit = sval;
	} else if (!strcmp(name, "kernel.sched_cfs_bandwidth_slice_us")) {
		if (has_uval && uval)
			vk->sysctl_kernel.sched_cfs_bandwidth_slice = uval;
	} else if (!strcmp(name, "kernel.sched_child_runs_first")) {
		if (has_uval && (uval == 0 || uval == 1))
			vk->sysctl_kernel.sched_child_runs_first = uval;
	} else if (!strcmp(name, "kernel.sched_deadline_period_max_us")) {
		if (has_uval && uval)
			vk->sysctl_kernel.sched_dl_period_max = uval;
	} else if (!strcmp(name, "kernel.sched_deadline_period_min_us")) {
		if (has_uval && uval)
			vk->sysctl_kernel.sched_dl_period_min = uval;
	} else if (!strcmp(name, "kernel.sched_rr_timeslice_ms")) {
		/* inactive */
		if (has_sval && sval > 0)
			vk->sysctl_kernel.sched_rr_timeslice = sval;
	} else if (!strcmp(name, "kernel.sched_rt_period_us")) {
		/* inactive */
		if (has_sval && sval > 0)
			vk->sysctl_kernel.sched_rt_period = sval;
	} else if (!strcmp(name, "kernel.sched_rt_runtime_us")) {
		/* inactive */
		if (has_sval && sval > 0)
			vk->sysctl_kernel.sched_rt_runtime = sval;
	} else if (!strcmp(name, "kernel.threads-max")) {
		if (has_sval && sval > 0)
			vk->sysctl_kernel.max_threads = clamp_t(u64, sval,
					MIN_THREADS, MAX_THREADS);
	} else if (!strcmp(name, "kernel.keys.gc_delay")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.key_gc_delay = uval;
	} else if (!strcmp(name, "kernel.keys.maxbytes")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.key_quota_maxbytes = uval;
	} else if (!strcmp(name, "kernel.keys.maxkeys")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.key_quota_maxkeys = uval;
	} else if (!strcmp(name, "kernel.keys.persistent_keyring_expiry")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.persistent_keyring_expiry = uval;
	} else if (!strcmp(name, "kernel.keys.root_maxbytes")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.key_quota_root_maxbytes = uval;
	} else if (!strcmp(name, "kernel.keys.root_maxkeys")) {
		if (has_uval && uval > 0)
			vk->sysctl_kernel.key_quota_root_maxkeys = uval;
	} else if (!strcmp(name, "kernel.pty.max")) {
		if (has_sval && sval > 0)
			vk->sysctl_kernel.pty_limit = sval;
	} else if (!strcmp(name, "kernel.pty.reserve")) {
		if (has_sval && sval > 0)
			vk->sysctl_kernel.pty_reserve = sval;
	} else if (!strcmp(name, "net.nf_conntrack_max")) {
		if (has_uval && uval > 0)
			vk->sysctl_net.nf_conntrack_max = uval;
	} else if (!strcmp(name, "net.core.busy_poll")) {
		if (has_uval)
			vk->sysctl_net.net_busy_poll = uval;
	} else if (!strcmp(name, "net.core.busy_read")) {
		if (has_uval)
			vk->sysctl_net.net_busy_read = uval;
	} else if (!strcmp(name, "net.core.optmem_max")) {
		if (has_sval && sval > 0)
			vk->sysctl_net.optmem_max = sval;
	} else if (!strcmp(name, "net.core.wmem_max")) {
		if (has_uval && uval)
			vk->sysctl_net.wmem_max = uval;
	} else if (!strcmp(name, "net.core.rmem_max")) {
		if (has_uval && uval)
			vk->sysctl_net.rmem_max = uval;
	} else if (!strcmp(name, "net.core.wmem_default")) {
		if (has_uval && uval)
			vk->sysctl_net.wmem_default = uval;
	} else if (!strcmp(name, "net.core.rmem_default")) {
		if (has_uval && uval)
			vk->sysctl_net.rmem_default = uval;
	} else if (!strcmp(name, "net.core.somaxconn")) {
		if (has_uval && uval)
			n->core.sysctl_somaxconn = uval;
	} else if (!strcmp(name, "net.ipv4.icmp_echo_ignore_broadcasts")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_icmp_echo_ignore_broadcasts = uval;
	} else if (!strcmp(name, "net.ipv4.ip_local_port_range")) {
		uval = 0;
		while ((p = strsep(&val, " \t")) != NULL && uval < 2) {
			if (!*p)
				continue;
			if (uval == 0) {
				if (kstrtos64(p, 10, &sval))
					sval = 0;
			} else {
				if (kstrtos64(p, 10, &old_sval))
					old_sval = 0;
			}
			uval++;
		}
		if (sval > 0 && old_sval > 0) {
			n->ipv4.ip_local_ports.range[0] = sval;
			n->ipv4.ip_local_ports.range[1] = old_sval;
		}
	} else if (!strcmp(name, "net.ipv4.tcp_max_tw_buckets")) {
		if (has_sval && sval > 0)
			n->ipv4.tcp_death_row.sysctl_max_tw_buckets = sval;
	} else if (!strcmp(name, "net.ipv4.tcp_ecn")) {
		if (has_uval && uval <= 2)
			n->ipv4.sysctl_tcp_ecn = uval;
	} else if (!strcmp(name, "net.ipv4.ip_default_ttl")) {
		if (has_uval && (uval >= 1 && uval <= 255))
			n->ipv4.sysctl_ip_default_ttl = uval;
	} else if (!strcmp(name, "net.ipv4.ip_no_pmtu_disc")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_ip_no_pmtu_disc = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_keepalive_time")) {
		if (has_sval && sval > 0)
			WRITE_ONCE(n->ipv4.sysctl_tcp_keepalive_time, sval * HZ);
	} else if (!strcmp(name, "net.ipv4.tcp_keepalive_intvl")) {
		if (has_sval && sval > 0)
			WRITE_ONCE(n->ipv4.sysctl_tcp_keepalive_intvl, sval * HZ);
	} else if (!strcmp(name, "net.ipv4.tcp_keepalive_probes")) {
		if (has_uval && uval)
			n->ipv4.sysctl_tcp_keepalive_probes = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_syn_retries")) {
		if (has_uval && uval >= 1 && uval <= MAX_TCP_SYNCNT)
			n->ipv4.sysctl_tcp_syn_retries = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_synack_retries")) {
		if (has_uval && uval)
			n->ipv4.sysctl_tcp_synack_retries = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_syncookies")) {
		if (has_uval && uval >= 0 && uval <= 2)
			n->ipv4.sysctl_tcp_syncookies = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_reordering")) {
		if (has_sval && sval > 0)
			n->ipv4.sysctl_tcp_reordering = sval;
	} else if (!strcmp(name, "net.ipv4.tcp_retries1")) {
		if (has_uval && uval && uval <= 255)
			n->ipv4.sysctl_tcp_retries1 = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_retries2")) {
		if (has_uval && uval)
			n->ipv4.sysctl_tcp_retries2 = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_orphan_retries")) {
		if (has_uval && uval)
			n->ipv4.sysctl_tcp_orphan_retries = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_tw_reuse")) {
		if (has_uval && uval >= 0 && uval <= 2)
			n->ipv4.sysctl_tcp_tw_reuse = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_fin_timeout")) {
		if (has_sval && sval > 0)
			WRITE_ONCE(n->ipv4.sysctl_tcp_fin_timeout, sval * HZ);
	} else if (!strcmp(name, "net.ipv4.tcp_sack")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_sack = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_window_scaling")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_window_scaling = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_timestamps")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_timestamps = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_thin_linear_timeouts")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_thin_linear_timeouts = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_retrans_collapse")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_retrans_collapse = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_fack")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_fack = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_adv_win_scale")) {
		if (has_sval && sval >= 0 && sval <= 4)
			n->ipv4.sysctl_tcp_adv_win_scale = sval;
	} else if (!strcmp(name, "net.ipv4.tcp_dsack")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_dsack = uval; // ?
	} else if (!strcmp(name, "net.ipv4.tcp_nometrics_save")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_nometrics_save = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_moderate_rcvbuf")) {
		if (has_uval && (uval == 0 || uval == 1))
			n->ipv4.sysctl_tcp_moderate_rcvbuf = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_min_tso_segs")) {
		if (has_uval && uval)
			n->ipv4.sysctl_tcp_min_tso_segs = uval;
	} else if (!strcmp(name, "net.ipv4.tcp_wmem")) {
		uval = 0;
		while ((p = strsep(&val, " \t")) != NULL && uval < 3) {
			if (!*p)
				continue;
			if (uval == 0) {
				if (kstrtos64(p, 10, &sval))
					sval = 0;
			} else if (uval == 1) {
				if (kstrtos64(p, 10, &old_sval))
					old_sval = 0;
			} else {
				if (kstrtos64(p, 10, &third_sval))
					third_sval = 0;
			}
			uval++;
		}
		if (sval > 0 && old_sval > 0 && third_sval > 0) {
			n->ipv4.sysctl_tcp_wmem[0] = sval;
			n->ipv4.sysctl_tcp_wmem[1] = old_sval;
			n->ipv4.sysctl_tcp_wmem[2] = third_sval;
		}
	} else if (!strcmp(name, "net.ipv4.tcp_rmem")) {
		uval = 0;
		while ((p = strsep(&val, " \t")) != NULL && uval < 3) {
			if (!*p)
				continue;
			if (uval == 0) {
				if (kstrtos64(p, 10, &sval))
					sval = 0;
			} else if (uval == 1) {
				if (kstrtos64(p, 10, &old_sval))
					old_sval = 0;
			} else {
				if (kstrtos64(p, 10, &third_sval))
					third_sval = 0;
			}
			uval++;
		}
		if (sval > 0 && old_sval > 0 && third_sval > 0) {
			n->ipv4.sysctl_tcp_rmem[0] = sval;
			n->ipv4.sysctl_tcp_rmem[1] = old_sval;
			n->ipv4.sysctl_tcp_rmem[2] = third_sval;
		}
	} else if (!strcmp(name, "net.ipv4.max_syn_backlog")) {
		if (has_sval && sval > 0)
			n->ipv4.sysctl_max_syn_backlog = sval;
	} else if (!strcmp(name, "net.ipv4.tcp_fastopen")) {
		if (has_sval && (sval == 1 || sval == 2 || sval == 4))
			n->ipv4.sysctl_tcp_fastopen = sval;
	} else if (!strcmp(name, "net.ipv4.tcp_congestion_control")) {
		if (strlen(val) > 1)
			tcp_set_default_congestion_control_ptr(n, val);
	} else if (!strcmp(name, "net.ipv4.conf.all.forwarding")) {
		if (has_sval && sval >= 0)
			devconf_forward(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_FORWARDING, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.mc_forwarding")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_MC_FORWARDING, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.proxy_arp")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_PROXY_ARP, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.accept_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ACCEPT_REDIRECTS, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.secure_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_SECURE_REDIRECTS, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.send_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_SEND_REDIRECTS, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.shared_media")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_SHARED_MEDIA, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.rp_filter")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_RP_FILTER, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.accept_source_route")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ACCEPT_SOURCE_ROUTE, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.bootp_relay")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_BOOTP_RELAY, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.log_martians")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_LOG_MARTIANS, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.tag")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_TAG, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_filter")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARPFILTER, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.medium_id")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_MEDIUM_ID, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.disable_xfrm")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_NOXFRM, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.disable_policy")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_NOPOLICY, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.force_igmp_version")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_FORCE_IGMP_VERSION, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_announce")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARP_ANNOUNCE, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_ignore")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARP_IGNORE, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.promote_secondaries")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_PROMOTE_SECONDARIES, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_accept")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARP_ACCEPT, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_notify")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARP_NOTIFY, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.accept_local")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ACCEPT_LOCAL, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.src_valid_mark")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_SRC_VMARK, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.proxy_arp_pvlan")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_PROXY_ARP_PVLAN, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.route_localnet")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ROUTE_LOCALNET, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.igmpv2_unsolicited_report_interval")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_IGMPV2_UNSOLICITED_REPORT_INTERVAL,
					DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.igmpv3_unsolicited_report_interval")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_IGMPV3_UNSOLICITED_REPORT_INTERVAL,
					DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.ignore_routes_with_linkdown")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN,
					 DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.drop_unicast_in_l2_multicast")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST,
					DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.drop_gratuitous_arp")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_DROP_GRATUITOUS_ARP, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.bc_forwarding")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_BC_FORWARDING, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.all.arp_evict_nocarrier")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_all, sval,
					IPV4_DEVCONF_ARP_EVICT_NOCARRIER, DEVCONF_ALL);
	} else if (!strcmp(name, "net.ipv4.conf.default.forwarding")) {
		if (has_sval && sval >= 0)
			devconf_forward(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_FORWARDING, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.mc_forwarding")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_MC_FORWARDING, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.proxy_arp")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_PROXY_ARP, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.accept_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ACCEPT_REDIRECTS, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.secure_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_SECURE_REDIRECTS, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.send_redirects")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_SEND_REDIRECTS, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.shared_media")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_SHARED_MEDIA, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.rp_filter")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_RP_FILTER, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.accept_source_route")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ACCEPT_SOURCE_ROUTE, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.bootp_relay")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_BOOTP_RELAY, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.log_martians")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_LOG_MARTIANS, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.tag")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_TAG, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_filter")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARPFILTER, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.medium_id")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_MEDIUM_ID, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.disable_xfrm")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_NOXFRM, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.disable_policy")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_NOPOLICY, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.force_igmp_version")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_FORCE_IGMP_VERSION, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_announce")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARP_ANNOUNCE, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_ignore")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARP_IGNORE, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.promote_secondaries")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_PROMOTE_SECONDARIES, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_accept")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARP_ACCEPT, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_notify")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARP_NOTIFY, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.accept_local")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ACCEPT_LOCAL, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.src_valid_mark")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_SRC_VMARK, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.proxy_arp_pvlan")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_PROXY_ARP_PVLAN, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.route_localnet")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ROUTE_LOCALNET, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.igmpv2_unsolicited_report_interval")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_IGMPV2_UNSOLICITED_REPORT_INTERVAL,
					DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.igmpv3_unsolicited_report_interval")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_IGMPV3_UNSOLICITED_REPORT_INTERVAL,
					DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.ignore_routes_with_linkdown")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN,
					DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.drop_unicast_in_l2_multicast")) {
		if (has_sval && sval >= 0)
			devconf_flush(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST,
					DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.drop_gratuitous_arp")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_DROP_GRATUITOUS_ARP, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.bc_forwarding")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_BC_FORWARDING, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.conf.default.arp_evict_nocarrier")) {
		if (has_sval && sval >= 0)
			devconf_proc(n, n->ipv4.devconf_dflt, sval,
					IPV4_DEVCONF_ARP_EVICT_NOCARRIER, DEVCONF_DFLT);
	} else if (!strcmp(name, "net.ipv4.unix_max_dgram_qlen")) {
		if (has_sval && sval > 0)
			n->unx.sysctl_max_dgram_qlen = sval;
	} else if (!strcmp(name, "vm.max_map_count")) {
		if (has_sval && sval > 0)
			vk->sysctl_vm.max_map_count = sval;
	} else if (!strcmp(name, "vm.mmap_min_addr")) {
		if (!has_uval && kstrtou64(val, 16, &uval)) {
			pr_warn("failed to parse raw sysctl val %s to u64\n", val);
			return -EINVAL;
		}
		if (uval) {
			vk->sysctl_vm.dac_mmap_min_addr = uval;
#ifdef CONFIG_LSM_MMAP_MIN_ADDR
			if (vk->sysctl_vm.dac_mmap_min_addr > CONFIG_LSM_MMAP_MIN_ADDR)
				vk->sysctl_vm.mmap_min_addr = vk->sysctl_vm.dac_mmap_min_addr;
			else
				vk->sysctl_vm.mmap_min_addr = CONFIG_LSM_MMAP_MIN_ADDR;
#else
			vk->sysctl_vm.mmap_min_addr = vk->sysctl_vm.dac_mmap_min_addr;
#endif
		}
	} else if (!strcmp(name, "vm.overcommit_kbytes")) {
		if (has_uval && uval) {
			vk->sysctl_vm.overcommit_kbytes = uval;
			vk->sysctl_vm.overcommit_ratio = 0;
		}
	} else if (!strcmp(name, "vm.overcommit_memory")) {
		if (has_sval && sval > 0) {
			if (sval == OVERCOMMIT_NEVER)
				vk_sync_overcommit_as(vk);
			vk->sysctl_vm.overcommit_memory = sval;
		}
	} else if (!strcmp(name, "vm.overcommit_ratio")) {
		if (has_sval && sval) {
			vk->sysctl_vm.overcommit_ratio = sval;
			vk->sysctl_vm.overcommit_kbytes = 0;
		}
	} else {
		pr_err("vkernel: unsupported sysctl %s\n", name);
		return -EINVAL;
	}

	pr_debug("handled sysctl %s\n", name);
	return 0;
}
