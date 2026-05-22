/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Nettrace support.
 *
 * Copyright (C) 2025 ZTE Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETDUMP_DUMP_H
#define NETDUMP_DUMP_H

#include <linux/skbuff.h>
#include "group.h"

#define PCAP_MAGIC 0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4

#define DEFAULT_SNAPLEN 0x40000

#define LINKTYPE_NULL           0
#define LINKTYPE_ETHERNET       1      /* also for 100Mb and up */
#define LINKTYPE_EXP_ETHERNET   2       /* 3Mb experimental Ethernet */
#define LINKTYPE_AX25           3
#define LINKTYPE_PRONET         4
#define LINKTYPE_CHAOS          5
#define LINKTYPE_TOKEN_RING     6     /* DLT_IEEE802 is used for Token Ring */
#define LINKTYPE_ARCNET         7
#define LINKTYPE_SLIP           8
#define LINKTYPE_PPP            9
#define LINKTYPE_FDDI           10
#define LINKTYPE_PPP_HDLC       50              /* PPP in HDLC-like framing */
#define LINKTYPE_PPP_ETHER      51              /* NetBSD PPP-over-Ethernet */
#define LINKTYPE_ATM_RFC1483    100             /* LLC/SNAP-encapsulated ATM */
#define LINKTYPE_RAW            101             /* raw IP */
#define LINKTYPE_SLIP_BSDOS     102             /* BSD/OS SLIP BPF header */
#define LINKTYPE_PPP_BSDOS      103             /* BSD/OS PPP BPF header */
#define LINKTYPE_C_HDLC         104             /* Cisco HDLC */
#define LINKTYPE_IEEE802_11     105             /* IEEE 802.11 (wireless) */
#define LINKTYPE_ATM_CLIP       106             /* Linux Classical IP over ATM */
#define LINKTYPE_LOOP           108             /* OpenBSD loopback */
#define LINKTYPE_LINUX_SLL      113             /* Linux cooked socket capture */
#define LINKTYPE_LTALK          114             /* Apple LocalTalk hardware */
#define LINKTYPE_ECONET         115             /* Acorn Econet */
#define LINKTYPE_CISCO_IOS      118             /* For Cisco-internal use */
#define LINKTYPE_PRISM_HEADER   119             /* 802.11+Prism II monitor mode */
#define LINKTYPE_AIRONET_HEADER 120             /* FreeBSD Aironet driver stuff */

struct pcap_file_header {
	uint32_t magic;
	uint16_t version_major;
	uint16_t version_minor;
	int32_t thiszone;     /* gmt to local correction */
	uint32_t sigfigs;    /* accuracy of timestamps */
	uint32_t snaplen;    /* max length saved portion of each pkt */
	uint32_t linktype;   /* data link type (LINKTYPE_*) */
};

struct timeval_compat {
	uint32_t tv_sec;     /* seconds */
	uint32_t tv_usec;    /* microseconds */
};

struct pcap_pkthdr {
	struct timeval_compat ts;      /* time stamp using 32 bits fields */
	uint32_t caplen;     /* length of portion present */
	uint32_t len;        /* length this packet (off wire) */
};

extern unsigned int dump_loss_due_to_no_memory;
extern unsigned int max_dump_skb_cnt, max_dump_file_size, dump_skb_over_cnt, dump_skb_over_size;

extern void try_dump_skb(struct sk_buff *skb, TRACE_POINT *tp);

extern int init_pcap(struct file *f);

extern void init_dump_work(TRACE_POINT *tp);

#endif //NETDUMP_DUMP_H
