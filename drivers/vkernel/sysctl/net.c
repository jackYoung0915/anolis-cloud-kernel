// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 ARM Ltd.
 * Author: Joy Allen <taozhiheng@jyhlab.org.cn>
 */

#include <linux/inetdevice.h>
#include <linux/netconf.h>

#include "sysctl.h"
#include "utils.h"

int (*tcp_set_default_congestion_control_ptr)(struct net *net, const char *name);
void (*rt_cache_flush_ptr)(struct net *net);
void (*inet_netconf_notify_devconf_ptr)(struct net *net, int event, int type,
				 int ifindex, struct ipv4_devconf *devconf);

// extern unsigned int nf_conntrack_max;

int vk_init_sysctl_net(struct vkernel_sysctl_net *net, struct task_struct *tsk)
{
	tcp_set_default_congestion_control_ptr =
			(void *)lookup_name("tcp_set_default_congestion_control");
	rt_cache_flush_ptr = (void *)lookup_name("rt_cache_flush");
	inet_netconf_notify_devconf_ptr =
			(void *)lookup_name("inet_netconf_notify_devconf");

	/* congestion_control can be null */
	if (!rt_cache_flush_ptr || !inet_netconf_notify_devconf_ptr) {
		pr_err("failed to find net symbols, flush: %p, notify: %p\n",
				rt_cache_flush_ptr, inet_netconf_notify_devconf_ptr);
		return -1;
	}

	if (!tsk) {
		pr_err("failed to init sysctl net with invalid task\n");
		return -1;
	}

	// net->nf_conntrack_max = nf_conntrack_max;
	net->nf_conntrack_max = 1572864;

	net->net_busy_poll = 0;
	net->net_busy_read = 0;

	net->weight_p = 64;
	net->dev_weight_rx_bias = 1;
	net->dev_weight_tx_bias = 1;
	net->dev_rx_weight = 64;
	net->dev_tx_weight = 64;

	net->netdev_budget = 300;
	net->netdev_budget_usecs = 2 * USEC_PER_SEC / HZ;
	net->netdev_max_backlog = 1000;

	net->optmem_max = sizeof(unsigned long)*(2*UIO_MAXIOV+512);
	net->wmem_max = SK_WMEM_MAX;
	net->rmem_max = SK_RMEM_MAX;
	net->wmem_default = SK_WMEM_MAX;
	net->rmem_default = SK_RMEM_MAX;

	net->net = ERR_PTR(-ESRCH);
	rcu_read_lock();
	task_lock(tsk);
	if (tsk->nsproxy)
		net->net = get_net(tsk->nsproxy->net_ns);
	task_unlock(tsk);
	rcu_read_unlock();
	if (IS_ERR(net->net)) {
		pr_err("failed to get net ns, error %ld\n", PTR_ERR(net->net));
		return -1;
	}

	return 0;
}

void vk_uninit_sysctl_net(struct vkernel_sysctl_net *net)
{
	if (!IS_ERR(net->net))
		put_net(net->net);
}

enum {
	DEVCONF_ALL,
	DEVCONF_DFLT,
	DEVCONF_OTHER
};

#define IPV4_DEVCONF_DFLT(net, attr) \
	IPV4_DEVCONF((*net->ipv4.devconf_dflt), attr)

static void devinet_copy_dflt_conf(struct net *net, int i)
{
	struct net_device *dev;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		struct in_device *in_dev;

		in_dev = __in_dev_get_rcu(dev);
		if (in_dev && !test_bit(i, in_dev->cnf.state))
			in_dev->cnf.data[i] = net->ipv4.devconf_dflt->data[i];
	}
	rcu_read_unlock();
}

static void inet_forward_change(struct net *net)
{
	struct net_device *dev;
	int on = IPV4_DEVCONF_ALL(net, FORWARDING);

	IPV4_DEVCONF_ALL(net, ACCEPT_REDIRECTS) = !on;
	IPV4_DEVCONF_DFLT(net, FORWARDING) = on;
	inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
					NETCONFA_FORWARDING,
					NETCONFA_IFINDEX_ALL,
					net->ipv4.devconf_all);
	inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
					NETCONFA_FORWARDING,
					NETCONFA_IFINDEX_DEFAULT,
					net->ipv4.devconf_dflt);

	for_each_netdev(net, dev) {
		struct in_device *in_dev;

		if (on)
			dev_disable_lro(dev);

		in_dev = __in_dev_get_rtnl(dev);
		if (in_dev) {
			IN_DEV_CONF_SET(in_dev, FORWARDING, on);
			inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
							NETCONFA_FORWARDING,
							dev->ifindex, &in_dev->cnf);
		}
	}
}

static int devinet_conf_ifindex(struct net *net, struct ipv4_devconf *cnf)
{
	struct in_device *idev;

	if (cnf == net->ipv4.devconf_dflt)
		return NETCONFA_IFINDEX_DEFAULT;
	else if (cnf == net->ipv4.devconf_all)
		return NETCONFA_IFINDEX_ALL;

	idev = container_of(cnf, struct in_device, cnf);
	return idev->dev->ifindex;
}

int devconf_proc(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type)
{
	int old_val;
	int ifindex;

	old_val = conf->data[i - 1];
	conf->data[i - 1] = val;

	set_bit(i - 1, conf->state);

	if (type == DEVCONF_DFLT)
		devinet_copy_dflt_conf(net, i - 1); // inline
	if (i == IPV4_DEVCONF_ACCEPT_LOCAL || i == IPV4_DEVCONF_ROUTE_LOCALNET)
		if (conf->data[i - 1] == 0 && old_val != 0)
			rt_cache_flush_ptr(net);

	if (i == IPV4_DEVCONF_BC_FORWARDING && conf->data[i - 1] != old_val)
		rt_cache_flush_ptr(net);

	if (i == IPV4_DEVCONF_RP_FILTER && conf->data[i - 1] != old_val) {
		ifindex = devinet_conf_ifindex(net, conf); // inline
		inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
						NETCONFA_RP_FILTER,
						ifindex, conf);
	}
	if (i == IPV4_DEVCONF_PROXY_ARP && conf->data[i - 1] != old_val) {
		ifindex = devinet_conf_ifindex(net, conf);
		inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
						NETCONFA_PROXY_NEIGH,
						ifindex, conf);
	}
	if (i == IPV4_DEVCONF_IGNORE_ROUTES_WITH_LINKDOWN && conf->data[i - 1] != old_val) {
		ifindex = devinet_conf_ifindex(net, conf);
		inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
						NETCONFA_IGNORE_ROUTES_WITH_LINKDOWN,
						ifindex, conf);
	}

	return 0;
}

int devconf_forward(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type)
{
	int old_val;

	old_val = conf->data[i - 1];
	conf->data[i - 1] = val;
	if (conf->data[i - 1] != old_val) {
		if (type != DEVCONF_DFLT) {
			if (!rtnl_trylock()) {
				conf->data[i - 1] = old_val;
				return -EBUSY;
			}
			if (type == DEVCONF_ALL)
				inet_forward_change(net); // inline
			else {
				struct in_device *idev =
					container_of(conf, struct in_device, cnf);
				dev_disable_lro(idev->dev);
				inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
								NETCONFA_FORWARDING,
								idev->dev->ifindex,
								conf);
			}
		} else
			inet_netconf_notify_devconf_ptr(net, RTM_NEWNETCONF,
							NETCONFA_FORWARDING,
							NETCONFA_IFINDEX_DEFAULT,
							conf);
	}

	return 0;
}

int devconf_flush(struct net *net, struct ipv4_devconf *conf,
				int val, int i, int type)
{
	int old_val;

	old_val = conf->data[i - 1];
	conf->data[i - 1] = val;
	if (conf->data[i - 1] != old_val)
		rt_cache_flush_ptr(net);

	return 0;
}

int vkernel_set_sysctl_net(struct vkernel_sysctl_net *net, struct vkernel_sysctl_net_desc *desc)
{
	struct net *n = net->net;
	int weight;
	int val;
	int i;

	/* netns specific */
	if (desc->nf_conntrack_max)
		net->nf_conntrack_max = desc->nf_conntrack_max;

	/* core, poll/select specific */
	net->net_busy_poll = desc->core_busy_poll;
	net->net_busy_read = desc->core_busy_read;

	/* napi_struct specific */
	if (desc->core_dev_weight > 0) {
		net->weight_p = desc->core_dev_weight;
		weight = READ_ONCE(net->weight_p);
		WRITE_ONCE(net->dev_rx_weight, weight * net->dev_weight_rx_bias);
		WRITE_ONCE(net->dev_tx_weight, weight * net->dev_weight_tx_bias);
	}

	/* softnet_data specific */
	if (desc->core_netdev_budget > 0)
		net->netdev_budget = desc->core_netdev_budget;
	if (desc->core_netdev_budget_us > 0)
		net->netdev_budget_usecs = desc->core_netdev_budget_us;
	if (desc->core_netdev_max_backlog > 0)
		net->netdev_max_backlog = desc->core_netdev_max_backlog;

	/* sock specific (netns specific) */
	if (desc->core_optmem_max > 0)
		net->optmem_max = desc->core_optmem_max;
	if (desc->core_wmem_max)
		net->wmem_max = desc->core_wmem_max;
	if (desc->core_rmem_max)
		net->rmem_max = desc->core_rmem_max;
	if (desc->core_wmem_default)
		net->wmem_default = desc->core_wmem_default;
	if (desc->core_rmem_default)
		net->rmem_default = desc->core_rmem_default;

	/* net ns specific */

	/* core */
	if (desc->core_somaxconn)
		n->core.sysctl_somaxconn = desc->core_somaxconn;

	/* ipv4 */
	if (desc->ipv4_icmp_echo_ignore_broadcasts == 0 ||
	    desc->ipv4_icmp_echo_ignore_broadcasts == 1)
		n->ipv4.sysctl_icmp_echo_ignore_broadcasts = desc->ipv4_icmp_echo_ignore_broadcasts;
	if (desc->ipv4_ip_local_port_range[0] > 0 && desc->ipv4_ip_local_port_range[1] > 0) {
		n->ipv4.ip_local_ports.range[0] = desc->ipv4_ip_local_port_range[0];
		n->ipv4.ip_local_ports.range[1] = desc->ipv4_ip_local_port_range[1];
	}
	if (desc->ipv4_max_tw_buckets > 0)
		n->ipv4.tcp_death_row.sysctl_max_tw_buckets = desc->ipv4_max_tw_buckets;
	if (desc->ipv4_tcp_ecn <= 2)
		n->ipv4.sysctl_tcp_ecn = desc->ipv4_tcp_ecn;
	if (desc->ipv4_ip_default_ttl >= 1 && desc->ipv4_ip_default_ttl <= 255)
		n->ipv4.sysctl_ip_default_ttl = desc->ipv4_ip_default_ttl;
	if (desc->ipv4_ip_no_pmtu_disc == 0 || desc->ipv4_ip_no_pmtu_disc == 1)
		n->ipv4.sysctl_ip_no_pmtu_disc = desc->ipv4_ip_no_pmtu_disc;
	if (desc->ipv4_tcp_keepalive_time > 0)
		WRITE_ONCE(n->ipv4.sysctl_tcp_keepalive_time, desc->ipv4_tcp_keepalive_time * HZ);
	if (desc->ipv4_tcp_keepalive_intvl > 0)
		WRITE_ONCE(n->ipv4.sysctl_tcp_keepalive_intvl, desc->ipv4_tcp_keepalive_intvl * HZ);
	if (desc->ipv4_tcp_keepalive_probes)
		n->ipv4.sysctl_tcp_keepalive_probes = desc->ipv4_tcp_keepalive_probes;
	if (desc->ipv4_tcp_syn_retries >= 1 && desc->ipv4_tcp_syn_retries <= MAX_TCP_SYNCNT)
		n->ipv4.sysctl_tcp_syn_retries = desc->ipv4_tcp_syn_retries;
	if (desc->ipv4_tcp_synack_retries)
		n->ipv4.sysctl_tcp_synack_retries = desc->ipv4_tcp_synack_retries;
	if (desc->ipv4_tcp_syncookies >= 0 && desc->ipv4_tcp_syncookies <= 2)
		n->ipv4.sysctl_tcp_syncookies = desc->ipv4_tcp_syncookies;
	if (desc->ipv4_tcp_reordering > 0)
		n->ipv4.sysctl_tcp_reordering = desc->ipv4_tcp_reordering;
	if (desc->ipv4_tcp_retries1  && desc->ipv4_tcp_retries1 <= 255)
		n->ipv4.sysctl_tcp_retries1 = desc->ipv4_tcp_retries1;
	if (desc->ipv4_tcp_retries2)
		n->ipv4.sysctl_tcp_retries2 = desc->ipv4_tcp_retries2;
	if (desc->ipv4_tcp_orphan_retries)
		n->ipv4.sysctl_tcp_orphan_retries = desc->ipv4_tcp_orphan_retries;
	if (desc->ipv4_tcp_tw_reuse >= 0 && desc->ipv4_tcp_tw_reuse <= 2)
		n->ipv4.sysctl_tcp_tw_reuse = desc->ipv4_tcp_tw_reuse;
	if (desc->ipv4_tcp_fin_timeout > 0)
		WRITE_ONCE(n->ipv4.sysctl_tcp_fin_timeout, desc->ipv4_tcp_fin_timeout * HZ);
	if (desc->ipv4_tcp_sack == 0 || desc->ipv4_tcp_sack == 1)
		n->ipv4.sysctl_tcp_sack = desc->ipv4_tcp_sack;
	if (desc->ipv4_tcp_window_scaling == 0 ||
		desc->ipv4_tcp_window_scaling == 1)
		n->ipv4.sysctl_tcp_window_scaling = desc->ipv4_tcp_window_scaling;
	if (desc->ipv4_tcp_timestamps == 0 || desc->ipv4_tcp_timestamps == 1)
		n->ipv4.sysctl_tcp_timestamps = desc->ipv4_tcp_timestamps;
	if (desc->ipv4_tcp_thin_linear_timeouts == 0 ||
		desc->ipv4_tcp_thin_linear_timeouts == 1)
		n->ipv4.sysctl_tcp_thin_linear_timeouts = desc->ipv4_tcp_thin_linear_timeouts;
	if (desc->ipv4_tcp_retrans_collapse == 0 ||
		desc->ipv4_tcp_retrans_collapse == 1)
		n->ipv4.sysctl_tcp_retrans_collapse = desc->ipv4_tcp_retrans_collapse;
	if (desc->ipv4_tcp_fack == 0 || desc->ipv4_tcp_fack == 1)
		n->ipv4.sysctl_tcp_fack = desc->ipv4_tcp_fack;
	if (desc->ipv4_tcp_adv_win_scale >= 0 && desc->ipv4_tcp_adv_win_scale <= 4)
		n->ipv4.sysctl_tcp_adv_win_scale = desc->ipv4_tcp_adv_win_scale;
	if (desc->ipv4_tcp_dsack == 0 || desc->ipv4_tcp_dsack == 1)
		n->ipv4.sysctl_tcp_dsack = desc->ipv4_tcp_dsack;
	if (desc->ipv4_tcp_nometrics_save == 0 || desc->ipv4_tcp_nometrics_save == 1)
		n->ipv4.sysctl_tcp_nometrics_save = desc->ipv4_tcp_nometrics_save;
	if (desc->ipv4_tcp_moderate_rcvbuf == 0 || desc->ipv4_tcp_moderate_rcvbuf == 1)
		n->ipv4.sysctl_tcp_moderate_rcvbuf = desc->ipv4_tcp_moderate_rcvbuf;
	if (desc->ipv4_tcp_min_tso_segs)
		n->ipv4.sysctl_tcp_min_tso_segs = desc->ipv4_tcp_min_tso_segs;
	if (desc->ipv4_tcp_wmem[0] > 0 && desc->ipv4_tcp_wmem[1] > 0 &&
	    desc->ipv4_tcp_wmem[2] > 0) {
		n->ipv4.sysctl_tcp_wmem[0] = desc->ipv4_tcp_wmem[0];
		n->ipv4.sysctl_tcp_wmem[1] = desc->ipv4_tcp_wmem[1];
		n->ipv4.sysctl_tcp_wmem[2] = desc->ipv4_tcp_wmem[2];
	}
	if (desc->ipv4_tcp_rmem[0] > 0 && desc->ipv4_tcp_rmem[1] > 0 &&
	    desc->ipv4_tcp_rmem[2] > 0) {
		n->ipv4.sysctl_tcp_rmem[0] = desc->ipv4_tcp_rmem[0];
		n->ipv4.sysctl_tcp_rmem[1] = desc->ipv4_tcp_rmem[1];
		n->ipv4.sysctl_tcp_rmem[2] = desc->ipv4_tcp_rmem[2];
	}
	if (desc->ipv4_max_syn_backlog > 0)
		n->ipv4.sysctl_max_syn_backlog = desc->ipv4_max_syn_backlog;
	if (desc->ipv4_tcp_fastopen == 1 || desc->ipv4_tcp_fastopen == 2 ||
	    desc->ipv4_tcp_fastopen == 4)
		n->ipv4.sysctl_tcp_fastopen = desc->ipv4_tcp_fastopen;
	if (tcp_set_default_congestion_control_ptr && strlen(desc->ipv4_tcp_congestion_control) > 1)
		tcp_set_default_congestion_control_ptr(n, desc->ipv4_tcp_congestion_control);

	/* ipv4 conf */
	for (i = IPV4_DEVCONF_FORWARDING; i <= IPV4_DEVCONF_MAX; i++) {
		val = desc->ipv4_conf_all[i - 1];
		if (val < 0)
			continue;

		if (i == IPV4_DEVCONF_FORWARDING)
			devconf_forward(n, n->ipv4.devconf_all, val, i, DEVCONF_ALL);
		else if (i == IPV4_DEVCONF_NOXFRM ||
		    i == IPV4_DEVCONF_NOPOLICY ||
		    i == IPV4_DEVCONF_PROMOTE_SECONDARIES ||
		    i == IPV4_DEVCONF_ROUTE_LOCALNET ||
		    i == IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST)
			devconf_flush(n, n->ipv4.devconf_all, val, i, DEVCONF_ALL);
		else
			devconf_proc(n, n->ipv4.devconf_all, val, i, DEVCONF_ALL);
	}
	/* ipv4 conf default */
	for (i = IPV4_DEVCONF_FORWARDING; i <= IPV4_DEVCONF_MAX; i++) {
		val = desc->ipv4_conf_default[i - 1];
		if (val != 0 && val != 1)
			continue;

		if (i == IPV4_DEVCONF_FORWARDING)
			devconf_forward(n, n->ipv4.devconf_dflt, val, i, DEVCONF_DFLT);
		else if (i == IPV4_DEVCONF_NOXFRM ||
		    i == IPV4_DEVCONF_NOPOLICY ||
		    i == IPV4_DEVCONF_PROMOTE_SECONDARIES ||
		    i == IPV4_DEVCONF_ROUTE_LOCALNET ||
		    i == IPV4_DEVCONF_DROP_UNICAST_IN_L2_MULTICAST)
			devconf_flush(n, n->ipv4.devconf_dflt, val, i, DEVCONF_DFLT);
		else
			devconf_proc(n, n->ipv4.devconf_dflt, val, i, DEVCONF_DFLT);
	}

	/* unix */
	if (desc->unix_max_dgram_qlen > 0)
		n->unx.sysctl_max_dgram_qlen = desc->unix_max_dgram_qlen;

	return 0;
}
EXPORT_SYMBOL(vkernel_set_sysctl_net);
