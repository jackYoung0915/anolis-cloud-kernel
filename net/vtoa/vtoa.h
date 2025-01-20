/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NET__TOA_H__
#define __NET__TOA_H__
#include <net/tcp.h>
#include <net/inet_common.h>
#include <linux/proc_fs.h>

#include <linux/hookers.h>

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#include <net/ipv6.h>
#include <net/transp_v6.h>
#endif

#define SK_TOA_DATA(sock) ((void *)((sock)->sk_toa_data))

#define NIPQUAD(addr) ({\
	(uint8_t *)_a = (uint8_t *)&(addr); \
	_a[0], _a[1], _a[2], _a[3]}})

#ifdef TOA_DEBUG
#define TOA_DBG(msg...)	pr_debug("[DEBUG] TOA: " msg)
#else
#define TOA_DBG(msg...)
#endif

#define TOA_INFO(msg...)				\
	do {						\
		if (net_ratelimit())			\
			pr_info("TOA: " msg);	\
	} while (0)

#define TCPOPT_TOA  254
#define TCPOPT_TOA_VIP 250
#define TCPOPT_VTOA 252
#define TCPOPT_V6VTOA	249

/* MUST be 4n !!!! */
/* |opcode|size|ip+port| = 1 + 1 + 6 */
#define TCPOLEN_TOA 8
/* |opcode|size|ip+port| = 1 + 1 + 6 */
#define TCPOLEN_TOA_VIP 8
/* |opcode|size|cport+cip+vid+vip+vport+pad[2]| = 1 + 1 + 16 + 2 */
#define TCPOLEN_VTOA 20
/* |opcode|size|cport+v6cip+vid+v6vip| = 1 + 1 + 2 + 16 + 4 + 16 */
#define TCPOLEN_V6VTOA 40

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#define TCPOPT_TOA_V6	253
/* |opcode|size|port|ipv6| = 1 + 1 + 2 + 16 */
#define TCPOLEN_TOA_V6	20
#endif

/* can be readed from ipv4 or ipv6 */
#define TCPOPT_TOA_V6_EX	254
#define TCPOLEN_TOA_V6_EX	20

struct toa_data_v6 {
	__u8 optcode;
	__u8 optsize;
	__be16 port;
	struct in6_addr in6;
};

/* MUST be 4 bytes alignment */
struct toa_data {
	__u8 optcode;
	__u8 optsize;
	__be16 port;
	union {
		__be32 ip;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		struct in6_addr in6;
#endif
	};
};

/* MUST be 4 bytes alignment */

struct toa_vip_data {
	__u8 optcode;
	__u8 optsize;
	__be16 port;
	__be32 ip;
};

/* MUST be 4 bytes alignment */
struct vtoa_data {
	__u8 optcode;
	__u8 optsize;
	__be16 cport;
	__be32 cip;
	__be32 vid;
	__be32 vip;
	__be16 vport;
	__u8 pad[2];
};

struct v6vtoa_data {
	u8 opcode;
	u8 opsize;
	__be16 cport;
	__be32 cip[4];
	__be32 vid:24,
	       reserved:8;
	__be32 vip[4];
} __packed;

/* we define this because gcc cannot take address of bit-field structure member 'vid' */
#define OFFSETOF_VID(xptr) (offsetof(struct v6vtoa_data, cip) + sizeof(xptr->cip))
#define SIZEOF_VID 3
#define OFFSETOF_RESERVED(xptr) (offsetof(struct v6vtoa_data, cip) + sizeof(xptr->cip) + SIZEOF_VID)

#define IPV6_PREFIX_4BYTES 4
#define IPV6_PREFIX_7BYTES 7

#define VID_BE_UNFOLD(vni) ((vni) << 8)

/* statistics about toa in proc /proc/net/vtoa_stat */
enum {
	SYN_RECV_SOCK_TOA_CNT = 1,
	SYN_RECV_SOCK_NO_TOA_CNT,
	GETNAME_TOA_OK_CNT_V4,
	GETNAME_V6VTOA_OK_CNT,
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	GETNAME_TOA_OK_CNT_V6,
	GETNAME_TOA_OK_CNT_MAPPED,
#endif
	GETNAME_TOA_MISMATCH_CNT,
	GETNAME_TOA_BYPASS_CNT,
	GETNAME_TOA_EMPTY_CNT,
	TOA_STAT_LAST
};

struct toa_stats_entry {
	char *name;
	int entry;
};

#define TOA_STAT_ITEM(_name, _entry) { \
	.name = _name,		\
	.entry = _entry,	\
}

#define TOA_STAT_END {	\
	NULL,		\
	0,		\
}

struct toa_stat_mib {
	unsigned long mibs[TOA_STAT_LAST];
};

#define TOA_INC_STATS(mib, field)         \
	(per_cpu_ptr(mib, smp_processor_id())->mibs[field]++)

extern int sysctl_v6vtoa_info_mode;
extern int v6vtoa_vip_prefixlen_learned;
extern struct in6_addr v6vtoa_vip_prefix;
#endif
