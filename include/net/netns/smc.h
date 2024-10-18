/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_SMC_H__
#define __NETNS_SMC_H__
#include <linux/ck_kabi.h>
#include <linux/mutex.h>
#include <linux/percpu.h>

#define SMC_IWARP_RSVD_PORTS_NUM	16 /* must be 16 */

struct smc_stats_rsn;
struct smc_stats;
struct netns_smc {
	/* per cpu counters for SMC */
	struct smc_stats __percpu	*smc_stats;
	/* protect fback_rsn */
	spinlock_t			mutex_fback_rsn;
	struct smc_stats_rsn		*fback_rsn;
	int				limit_smc_hs;	/* constraint on handshake */
	atomic_t			iwarp_cnt;
	struct socket			*rsvd_sock[SMC_IWARP_RSVD_PORTS_NUM];
#ifdef CONFIG_SYSCTL
	struct ctl_table_header		*smc_hdr;
#endif
	unsigned int			sysctl_autocorking_size;
	unsigned int			sysctl_smcr_buf_type;
	unsigned int			sysctl_vendor_exp_options;
	int				sysctl_smcr_testlink_time;
	int				sysctl_wmem;
	int				sysctl_rmem;
	int				sysctl_tcp2smc;
	int				sysctl_max_links_per_lgr;
	int				sysctl_max_conns_per_lgr;
	CK_KABI_USE_SPLIT(1, unsigned int sysctl_autosplit_size)

	CK_KABI_RESERVE(2)
	CK_KABI_RESERVE(3)
	CK_KABI_RESERVE(4)
	CK_KABI_RESERVE(5)
	CK_KABI_RESERVE(6)
	CK_KABI_RESERVE(7)
	CK_KABI_RESERVE(8)
	CK_KABI_RESERVE(9)
	CK_KABI_RESERVE(10)
	CK_KABI_RESERVE(11)
	CK_KABI_RESERVE(12)
	CK_KABI_RESERVE(13)
	CK_KABI_RESERVE(14)
	CK_KABI_RESERVE(15)
	CK_KABI_RESERVE(16)
	CK_KABI_RESERVE(17)
	CK_KABI_RESERVE(18)
	CK_KABI_RESERVE(19)
	CK_KABI_RESERVE(20)
	CK_KABI_RESERVE(21)
	CK_KABI_RESERVE(22)
	CK_KABI_RESERVE(23)
	CK_KABI_RESERVE(24)
	CK_KABI_RESERVE(25)
	CK_KABI_RESERVE(26)
	CK_KABI_RESERVE(27)
	CK_KABI_RESERVE(28)
	CK_KABI_RESERVE(29)
	CK_KABI_RESERVE(30)
	CK_KABI_RESERVE(31)
	CK_KABI_RESERVE(32)
};
#endif
