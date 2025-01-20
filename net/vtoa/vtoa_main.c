// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Alibaba Group Holding Limited.  All Rights Reserved. */

#define	KMSG_COMPONENT	"VTOA"
#define	pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include "vtoa.h"
#include "vtoa_ctl.h"

/* Statistics of toa in proc /proc/net/vtoa_stats */
struct toa_stats_entry toa_stats[] = {
	TOA_STAT_ITEM("syn_recv_sock_toa", SYN_RECV_SOCK_TOA_CNT),
	TOA_STAT_ITEM("syn_recv_sock_no_toa", SYN_RECV_SOCK_NO_TOA_CNT),
	TOA_STAT_ITEM("getname_toa_ok_v4", GETNAME_TOA_OK_CNT_V4),
	TOA_STAT_ITEM("getname_v6vtoa_ok", GETNAME_V6VTOA_OK_CNT),
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	TOA_STAT_ITEM("getname_toa_ok_v6", GETNAME_TOA_OK_CNT_V6),
	TOA_STAT_ITEM("getname_toa_ok_mapped", GETNAME_TOA_OK_CNT_MAPPED),
#endif
	TOA_STAT_ITEM("getname_toa_mismatch", GETNAME_TOA_MISMATCH_CNT),
	TOA_STAT_ITEM("getname_toa_bypass", GETNAME_TOA_BYPASS_CNT),
	TOA_STAT_ITEM("getname_toa_empty", GETNAME_TOA_EMPTY_CNT),
	TOA_STAT_END
};

struct toa_stat_mib *ext_stats;

/* Parse TCP options in skb, try to get client ip, port
 * @param skb [in] received skb, it should be a ack/get-ack packet.
 * @return NULL if we don't get client ip/port;
 *         value of toa_data in ret_ptr if we get client ip/port.
 */
static int get_toa_data(struct sk_buff *skb, void *sk_toa_data, int sk_toa_datalen)
{
	struct tcphdr *th;
	int length;
	unsigned char *ptr;

	if (!skb)
		return 0;

	th = tcp_hdr(skb);
	length = (th->doff * 4) - sizeof(struct tcphdr);
	ptr = (unsigned char *)(th + 1);

	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return 0;

		/* Ref: RFC 793 section 3.1 */
		case TCPOPT_NOP:
			length--;
			continue;
		}

		opsize = *ptr++;

		/* "silly options" */
		if (opsize < 2)
			return 0;

		/* don't parse partial options */
		if (opsize > length)
			return 0;

		if ((opcode == TCPOPT_TOA && opsize == TCPOLEN_TOA)) {
			struct toa_data *tdata;
			struct toa_vip_data *tdata_vip;

			memset(sk_toa_data, 0, sizeof(struct toa_data));
			memcpy(sk_toa_data, ptr - 2, TCPOLEN_TOA);
			tdata = sk_toa_data;

			TOA_DBG("find toa data: ip = %u.%u.%u.%u, port = %u\n",
				NIPQUAD(tdata->ip),
				ntohs(tdata->port));

			/* TOA_VIP: vip parse */
			length -= opsize;
			ptr += (opsize - 2);

			opcode = *ptr++;
			opsize = *ptr++;

			/* "silly options" */
			if (opsize < 2)
				return 0;

			/* don't parse partial options */
			if (opsize > length)
				return 0;

			if (TCPOPT_TOA_VIP == opcode && TCPOLEN_TOA_VIP == opsize) {
				sk_toa_data += TCPOLEN_TOA;
				memset(sk_toa_data, 0, sizeof(struct toa_vip_data));
				memcpy(sk_toa_data, ptr - 2, TCPOLEN_TOA_VIP);
				tdata_vip = sk_toa_data;

				TOA_DBG("find toa data: ip = %u.%u.%u.%u, port = %u\n",
					NIPQUAD(tdata_vip->ip), ntohs(tdata_vip->port));
			}

			return 1;

		} else if (opcode == TCPOPT_TOA_VIP && opsize == TCPOLEN_TOA_VIP) {
			struct toa_vip_data *tdata;

			memset(sk_toa_data, 0, sizeof(struct toa_vip_data));
			memcpy(sk_toa_data, ptr - 2, TCPOLEN_TOA_VIP);
			tdata = sk_toa_data;

			TOA_DBG("find toa data: ip = %u.%u.%u.%u, port = %u\n",
				NIPQUAD(tdata->ip), ntohs(tdata->port));
			return 1;

		} else if (opcode == TCPOPT_VTOA && opsize == TCPOLEN_VTOA) {
			struct vtoa_data *vtdata;

			memset(sk_toa_data, 0, sizeof(struct vtoa_data));
			memcpy(sk_toa_data, ptr - 2, TCPOLEN_VTOA);

			vtdata = sk_toa_data;

			TOA_DBG("find vtoa data: cip:cport->vid:vip:vport\n"
				"%u.%u.%u.%u:%u->%u:%u.%u.%u.%u:%u\n",
				NIPQUAD(vtdata->cip),
				ntohs(vtdata->cport),
				vtdata->vid,
				NIPQUAD(vtdata->vip),
				ntohs(vtdata->vport)
			       );
			return 1;

		} else if (opcode == TCPOPT_V6VTOA && opsize == TCPOLEN_V6VTOA) {
#ifdef TOA_DEBUG
			struct in6_addr dbg_v6vip = IN6ADDR_ANY_INIT;
			struct v6vtoa_data *saved;
			__be32 dbg_vid = 0;
#endif
			struct v6vtoa_data *v6vtdata = (struct v6vtoa_data *)(ptr - 2);

			memset(sk_toa_data, 0, sk_toa_datalen);

			if (sk_toa_datalen >= TCPOLEN_V6VTOA) {
				memcpy(sk_toa_data, v6vtdata, TCPOLEN_V6VTOA);

			} else if (sk_toa_datalen == 32) {
				memcpy(sk_toa_data, v6vtdata, OFFSETOF_VID(v6vtdata));

				if (sysctl_v6vtoa_info_mode == 0) {
					/* mode 0: default mode, save: cport +
					 * cip, do nothing
					 */
				} else if (sysctl_v6vtoa_info_mode == 1) {
					/* mode 1: save: cport + cip + vip,
					 * learn vip prefix-length 4bytes
					 */
					memcpy((char *)sk_toa_data + OFFSETOF_VID(v6vtdata),
					       (char *)v6vtdata->vip + IPV6_PREFIX_4BYTES,
					       sk_toa_datalen - OFFSETOF_VID(v6vtdata)); //12 bytes
					if (v6vtoa_vip_prefixlen_learned == 0) {
						memcpy(&v6vtoa_vip_prefix, (char *)v6vtdata->vip,
						       IPV6_PREFIX_4BYTES);
						v6vtoa_vip_prefixlen_learned = IPV6_PREFIX_4BYTES;

						TOA_INFO("v6vtoa origin data: cip:cport->vid:vip "
							 "[%pI6]:%u -> %u:[%pI6]\n"
							 "saved: [%pI6]/%d\n",
							 (struct in6_addr *)(v6vtdata->cip),
							 ntohs(v6vtdata->cport),
							 ntohl(VID_BE_UNFOLD(v6vtdata->vid)),
							 (struct in6_addr *)(v6vtdata->vip),
							 &v6vtoa_vip_prefix,
							 v6vtoa_vip_prefixlen_learned);
					}
				} else if (sysctl_v6vtoa_info_mode == 2) {
					/* mode 2: save: cport + cip + vid + vip,
					 * learn vip prefix-length 7bytes
					 * network order vid 1193046 in
					 * memory(low->high address): 0x12 0x34
					 * 0x56 0x00
					 */
					memcpy((char *)sk_toa_data + OFFSETOF_VID(v6vtdata),
					       (char *)v6vtdata + OFFSETOF_VID(v6vtdata),
					       SIZEOF_VID);
					memcpy((char *)sk_toa_data + OFFSETOF_RESERVED(v6vtdata),
					       (char *)v6vtdata->vip + IPV6_PREFIX_7BYTES,
					       sk_toa_datalen - OFFSETOF_RESERVED(v6vtdata));
					if (v6vtoa_vip_prefixlen_learned == 0) {
						memcpy(&v6vtoa_vip_prefix, (char *)v6vtdata->vip,
						       IPV6_PREFIX_7BYTES);
						v6vtoa_vip_prefixlen_learned = IPV6_PREFIX_7BYTES;

						TOA_INFO("v6vtoa origin data: cip:cport->vid:vip "
							 "[%pI6]:%u -> %u:[%pI6]\n"
							 "saved: [%pI6]/%d\n",
							 (struct in6_addr *)(v6vtdata->cip),
							 ntohs(v6vtdata->cport),
							 ntohl(VID_BE_UNFOLD(v6vtdata->vid)),
							 (struct in6_addr *)(v6vtdata->vip),
							 &v6vtoa_vip_prefix,
							 v6vtoa_vip_prefixlen_learned);
					}
				}
			}
#ifdef TOA_DEBUG
			saved = (struct v6vtoa_data *)sk_toa_data;
			if (sysctl_v6vtoa_info_mode == 1) {
				memcpy(&dbg_v6vip, &v6vtoa_vip_prefix, 4);
				memcpy((char *)&dbg_v6vip + 4, (char *)&saved->vip - 4,
				       sizeof(v6vtdata->vip) - 4);
			} else if (sysctl_v6vtoa_info_mode == 2) {
				memcpy((char *)&dbg_vid + 1, (char *)&saved->vip - 4, 3);
				memcpy(&dbg_v6vip, &v6vtoa_vip_prefix, 7);
				memcpy((char *)&dbg_v6vip + 7, (char *)&saved->vip + 3 - 4,
				       sizeof(v6vtdata->vip) - 7);
			}
#endif
			TOA_DBG("v6vtoa origin data: cip:cport->vid:vip [%pI6]:%u -> %u:[%pI6]\n"
				"saved: [%pI6]:%u -> %u:[%pI6]\n",
				(struct in6_addr *)(v6vtdata->cip), ntohs(v6vtdata->cport),
				ntohl(VID_BE_UNFOLD(v6vtdata->vid)),
				(struct in6_addr *)(v6vtdata->vip),
				(struct in6_addr *)(saved->cip), ntohs(saved->cport),
				ntohl(dbg_vid), &dbg_v6vip);

			return 1;
		} else if (opcode == TCPOPT_TOA_V6_EX && opsize == TCPOLEN_TOA_V6_EX) {
			struct toa_data_v6 *tdata;

			memset(sk_toa_data, 0, sizeof(struct toa_data_v6));
			memcpy(sk_toa_data, ptr - 2, TCPOLEN_TOA_V6_EX);
			tdata = (struct toa_data_v6 *)sk_toa_data;
			return 1;
		}
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		else if (opcode == TCPOPT_TOA_V6 && opsize == TCPOLEN_TOA_V6) {
			struct toa_data *tdata;

			memset(sk_toa_data, 0, sizeof(struct toa_data));
			memcpy(sk_toa_data, ptr - 2, TCPOLEN_TOA_V6);
			tdata = (struct toa_data *)sk_toa_data;
			TOA_DBG("find toa data: ipv6 = %pI6, port = %u\n",
				&tdata->in6, ntohs(tdata->port));
			return 1;
		}
#endif
		ptr += opsize - 2;
		length -= opsize;
	}
	return 0;
}

/* get client ip from socket
 * @param sock [in] the socket to getpeername() or getsockname()
 * @param uaddr [out] the place to put client ip, port
 * @param uaddr_len [out] length of @uaddr
 * @peer [in] if(peer), try to get remote address; if(!peer),
 *  try to get local address
 * @return: return what the original inet_getname() returns.
 */
static int inet_getname_toa(struct socket *sock, struct sockaddr *uaddr,
			    int peer, int *p_retval)
{
	int retval = *p_retval;
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	u8 *option = SK_TOA_DATA(sk);

	if (retval < 0 || !peer) {
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
		return retval;
	}

	if (TCPOPT_TOA == option[0] && TCPOLEN_TOA == option[1]) {
		struct toa_data *tdata = SK_TOA_DATA(sk);

		TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT_V4);
		sin->sin_port = tdata->port;
		sin->sin_addr.s_addr = tdata->ip;
		TOA_DBG("%s: set new sockaddr, ip %u.%u.%u.%u -> %u.%u.%u.%u, port %u -> %u\n",
			__func__, NIPQUAD(sin->sin_addr.s_addr),
			NIPQUAD(tdata->ip), ntohs(sin->sin_port),
			ntohs(tdata->port));

	} else if (TCPOPT_VTOA == option[0] && TCPOLEN_VTOA == option[1]) {
		struct vtoa_data *vtdata = SK_TOA_DATA(sk);

		TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT_V4);
		sin->sin_port = vtdata->cport;
		sin->sin_addr.s_addr = vtdata->cip;
		TOA_DBG("%s: set new sockaddr, ip %u.%u.%u.%u -> %u.%u.%u.%u, port %u -> %u\n",
			__func__, NIPQUAD(sin->sin_addr.s_addr),
			NIPQUAD(vtdata->cip), ntohs(sin->sin_port),
			ntohs(vtdata->cport));

	} else if (TCPOPT_V6VTOA == option[0] && TCPOLEN_V6VTOA == option[1]) {
		struct v6vtoa_data *v6vtdata = SK_TOA_DATA(sk);
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)uaddr;

		TOA_INC_STATS(ext_stats, GETNAME_V6VTOA_OK_CNT);
		*p_retval = sizeof(*sin6);      //must update *p_retval
		retval = *p_retval;
		sin6->sin6_family = AF_INET6;  //hack to AF_INET6
		sin6->sin6_port = v6vtdata->cport;
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;
		//memcpy(&sin6->sin6_addr, v6vtdata->cip, sizeof(v6vtdata->cip));
		ipv6_addr_set(&sin6->sin6_addr, v6vtdata->cip[0], v6vtdata->cip[1],
			      v6vtdata->cip[2], v6vtdata->cip[3]);
		TOA_DBG("%s: af: %d, cip [%pI6]:%u\n", sin6->sin6_family,
			__func__, &sin6->sin6_addr, ntohs(sin6->sin6_port));

	} else if (TCPOPT_TOA_V6_EX == option[0] && TCPOLEN_TOA_V6_EX == option[1]) {
		struct toa_data_v6 *tdata = SK_TOA_DATA(sk);
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)uaddr;

		/* hack to AF_INET6 */
		*p_retval = sizeof(*sin6);
		retval = *p_retval;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = tdata->port;
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;
		sin6->sin6_addr = tdata->in6;

	} else { /* doesn't belong to us */
#ifdef TOA_DEBUG
		struct toa_data *tdata = SK_TOA_DATA(sk);
#endif

		TOA_INC_STATS(ext_stats, GETNAME_TOA_MISMATCH_CNT);
		TOA_DBG("%s: invalid toa data, ip %u.%u.%u.%u port %u opcode %u opsize %u\n",
			__func__, NIPQUAD(tdata->ip), ntohs(tdata->port),
			tdata->optcode, tdata->optsize);
	}

	TOA_DBG("%s called, retval: %d, peer: %d\n", __func__, retval, peer);
	return retval;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int inet6_getname_toa(struct socket *sock, struct sockaddr *uaddr,
			     int peer, int *p_retval)
{
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *)uaddr;
	struct sock *sk = sock->sk;
	int retval = *p_retval;
	u8 *option = SK_TOA_DATA(sk);

	if (retval < 0 || !peer) {
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
		return retval;
	}

	/* set our value if need */
	if (TCPOPT_TOA_V6 == option[0] && TCPOLEN_TOA_V6 == option[1]) {
		struct toa_data *tdata = SK_TOA_DATA(sk);

		TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT_V6);
		sin->sin6_port = tdata->port;
		sin->sin6_addr = tdata->in6;
		TOA_DBG("%s: ipv6 = %pI6, port = %u\n",
			__func__, &sin->sin6_addr, ntohs(sin->sin6_port));

	} else if (TCPOPT_TOA_V6_EX == option[0] && TCPOLEN_TOA_V6_EX == option[1]) {
		struct toa_data_v6 *tdata = SK_TOA_DATA(sk);

		sin->sin6_port = tdata->port;
		sin->sin6_addr = tdata->in6;

	} else if (TCPOPT_TOA == option[0] && TCPOLEN_TOA == option[1]) {
		struct toa_data *tdata = SK_TOA_DATA(sk);

		TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT_MAPPED);
		sin->sin6_port = tdata->port;
		ipv6_addr_set(&sin->sin6_addr, 0, 0,
			      htonl(0x0000FFFF), tdata->ip);
		TOA_DBG("%s: ipv6_mapped = %pI6, port = %u\n",
			__func__, &sin->sin6_addr, ntohs(sin->sin6_port));

	} else if (TCPOPT_V6VTOA == option[0] && TCPOLEN_V6VTOA == option[1]) {
		struct v6vtoa_data *v6vtdata = SK_TOA_DATA(sk);
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)uaddr;

		TOA_INC_STATS(ext_stats, GETNAME_V6VTOA_OK_CNT);
		sin6->sin6_port = v6vtdata->cport;
		memcpy(&sin6->sin6_addr, v6vtdata->cip, sizeof(v6vtdata->cip));
		TOA_DBG("%s: cip [%pI6]:%u -> vid:vip %u:[%pI6]\n",
			__func__, &sin6->sin6_addr, sin6->sin6_port,
			ntohl(VID_BE_UNFOLD(v6vtdata->vid)), v6vtdata->vip);

	} else { /* doesn't belong to us */
		TOA_INC_STATS(ext_stats, GETNAME_TOA_MISMATCH_CNT);
	}

	TOA_DBG("inet_getname_toa called, retval: %d, peer: %d\n", retval, peer);
	return retval;
}
#endif

/* The three way handshake has completed - we got a valid synack -
 * now create the new socket.
 * We need to save toa data into the new socket.
 * @param sk [out]  the socket
 * @param skb [in] the ack/ack-get packet
 * @param req [in] the open request for this connection
 * @param dst [out] route cache entry
 * @return NULL if fail new socket if succeed.
 */
static struct sock *
tcp_v4_syn_recv_sock_toa(struct sock *sk, struct sk_buff *skb,
			 struct request_sock *req, struct dst_entry *dst,
			 struct request_sock *req_unhash, bool *own_req,
			 struct sock **p_newsock)
{
	struct sock *newsock = *p_newsock;

	if (!sk || !skb)
		return NULL;

	/* set our value if need */
	if (newsock) {
		if (get_toa_data(skb, newsock->sk_toa_data, sizeof(newsock->sk_toa_data)))
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		else
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_NO_TOA_CNT);
	}
	return newsock;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct sock *
tcp_v6_syn_recv_sock_toa(struct sock *sk, struct sk_buff *skb,
			 struct request_sock *req, struct dst_entry *dst,
			 struct request_sock *req_unhash, bool *own_req,
			 struct sock **p_newsock)
{
	struct sock *newsock = *p_newsock;

	if (!sk || !skb)
		return NULL;

	/* set our value if need */
	if (newsock) {
		if (get_toa_data(skb, newsock->sk_toa_data, sizeof(newsock->sk_toa_data)))
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		else
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_NO_TOA_CNT);
	}
	return newsock;
}
#endif

static struct hooker inet_getname_hooker = {
	.func = inet_getname_toa,
};

static struct hooker inet_tcp_hooker = {
	.func = tcp_v4_syn_recv_sock_toa,
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct hooker inet6_getname_hooker = {
	.func = inet6_getname_toa,
};

static struct hooker inet6_tcp_hooker = {
	.func = tcp_v6_syn_recv_sock_toa,
};
#endif

extern const struct inet_connection_sock_af_ops ipv6_specific;

/* replace the functions with our functions */
static inline int
hook_toa_functions(void)
{
	int ret;

	ret = hooker_install(&inet_stream_ops.getname, &inet_getname_hooker);
	ret |= hooker_install(&ipv4_specific.syn_recv_sock, &inet_tcp_hooker);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	ret |= hooker_install(&inet6_stream_ops.getname, &inet6_getname_hooker);
	ret |= hooker_install(&ipv6_specific.syn_recv_sock, &inet6_tcp_hooker);
#endif
	return ret;
}

/* replace the functions to original ones */
static void
unhook_toa_functions(void)
{
	hooker_uninstall(&inet_getname_hooker);
	hooker_uninstall(&inet_tcp_hooker);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	hooker_uninstall(&inet6_getname_hooker);
	hooker_uninstall(&inet6_tcp_hooker);
#endif
}

/* Statistics of toa in proc /proc/net/vtoa_stats */
static int toa_stats_show(struct seq_file *seq, void *v)
{
	int i, j, cpu_nr;

	/* print CPU first */
	seq_puts(seq, "                                  ");
	cpu_nr = num_possible_cpus();
	for (i = 0; i < cpu_nr; i++)
		if (cpu_online(i))
			seq_printf(seq, "CPU%d       ", i);
	seq_putc(seq, '\n');

	i = 0;
	while (toa_stats[i].name) {
		seq_printf(seq, "%-25s:", toa_stats[i].name);
		for (j = 0; j < cpu_nr; j++) {
			if (cpu_online(j))
				seq_printf(seq, "%10lu ",
					   *(((unsigned long *)per_cpu_ptr(ext_stats, j)) +
					     toa_stats[i].entry));
		}
		seq_putc(seq, '\n');
		i++;
	}
	return 0;
}

static int toa_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, toa_stats_show, NULL);
}

static const struct proc_ops toa_stats_fops = {
	.proc_open	= toa_stats_seq_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* module init */
static int __init
toa_init(void)
{
	/* alloc statistics array for toa */
	ext_stats = alloc_percpu(struct toa_stat_mib);
	if (!ext_stats)
		return -ENOMEM;

	if (!proc_create("vtoa_stats", 0, init_net.proc_net, &toa_stats_fops)) {
		TOA_INFO("cannot create procfs /proc/net/vtoa_stats.\n");
		goto err_percpu;
	}

	/* hook funcs for parse and get toa */
	if (hook_toa_functions())
		goto err_proc;

	if (vtoa_ctl_init() < 0) {
		TOA_INFO("vtoa_ctl_init() failed\n");
		goto err_ctl;
	}

	return 0;
err_ctl:
	unhook_toa_functions();
err_proc:
	remove_proc_entry("vtoa_stats", init_net.proc_net);
err_percpu:
	free_percpu(ext_stats);
	return -ENODEV;
}

/* module cleanup*/
static void __exit
toa_exit(void)
{
	vtoa_ctl_cleanup();

	unhook_toa_functions();
	remove_proc_entry("vtoa_stats", init_net.proc_net);
	free_percpu(ext_stats);
}

module_init(toa_init);
module_exit(toa_exit);
MODULE_LICENSE("GPL");
