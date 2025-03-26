// SPDX-License-Identifier: GPL-2.0+
/*
 * ARM CMN-700 performance erratum 3688582 mitigation
 *
 * Copyright (c) 2024-2025 Alibaba Corp.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/hashtable.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/sched/clock.h>
#include <linux/memory.h>

#include <linux/arm-smccc.h>

#define SMC_AIS_OEM_CMD			0xc300ffecUL
#define SMC_CMN_SCAN			0x61UL

#define CSP_MAX(a, b)			((a) > (b) ? (a) : (b))
#define CSP_MIN(a, b)			((a) < (b) ? (a) : (b))

#define CSP_MESI_I			0
#define CSP_MESI_S			1
#define CSP_MESI_E			2
#define CSP_MESI_M			3
#define CSP_MESI_MIN			1
#define CSP_MESI_MAX			3
#define CSP_TARGET_SF_SLC		2
#define CSP_NR_DIE			2
#define CSP_MAX_NR_HNF_DIE		64
#define CSP_MAX_NR_HNF			(CSP_NR_DIE * CSP_MAX_NR_HNF_DIE)
#define CSP_NR_WAYS			16
#define CSP_NR_SLC_SET			1024
#define CSP_NR_SF_SET			2048
#define CSP_MAX_NR_SET			CSP_MAX(CSP_NR_SF_SET, CSP_NR_SLC_SET)
#define CSP_MIN_NR_SET			CSP_MIN(CSP_NR_SF_SET, CSP_NR_SLC_SET)
#define CSP_MAX_NR_PADDR		(CSP_NR_WAYS * CSP_MAX_NR_SET)
#define CSP_PADDR_UNIQ_SHIFT		0
#define CSP_PADDR_MESI_SHIFT		1
#define CSP_PADDR_PAD1_SHIFT		3
#define CSP_PADDR_CL_ENTRY_SHIFT	6
#define CSP_PADDR_PA_SLC_SHIFT		16

#define CSP_PADDR_UNIQ(paddr)		(((paddr) >> CSP_PADDR_UNIQ_SHIFT) & 0x1)
#define CSP_PADDR_MESI(paddr)		(((paddr) >> CSP_PADDR_MESI_SHIFT) & 0x3)
#define CSP_PADDR_PAD1(paddr)		(((paddr) >> CSP_PADDR_PAD1_SHIFT) & 0x7)
#define CSP_PADDR_SLC_SET(paddr)	(((paddr) >> CSP_PADDR_CL_ENTRY_SHIFT) &	\
						((1ULL << (CSP_PADDR_PA_SLC_SHIFT -	\
							   CSP_PADDR_CL_ENTRY_SHIFT)) - 1))
#define CSP_PADDR_PA(paddr)		((paddr) & ~((1ULL << CSP_PADDR_CL_ENTRY_SHIFT) - 1))

struct cmn_scan_param {
	uint8_t scan_target;		/* in; 2: scan SF and SLC */
	uint8_t check_mesi;		/* in; 1: filter addresses with the following "mesi" field
					 *     0: don't filter addresses
					 *     always filter addresses with mesi "I" state
					 *     if scan_target == 2, only used for SF, SLC use all
					 *     but "I"
					 */
	uint8_t mesi;			/* in; discard address if mesi of SF/SLC tag doesn't
					 *     match
					 */
	uint8_t check_uniq;		/* in; 1: filter addresses with the following "uniq" filed
					 *     0: don't filter addresses
					 *     if scan_target == 2, only used for SF, SLC use uniq=0
					 */
	uint8_t uniq;			/* in; discard address if uniq of SF/SLC tag doesn't
					 *     match
					 */
	uint8_t reserved1[3];
	uint32_t hnf_id;		/* in; [0, 127] */
	uint32_t scan_start;		/* in; [0, 2048), first cache line set to scan */
	uint32_t scan_len;		/* in; (0, 2048], total number of cache line set to scan */
	uint32_t nr_paddr;		/* out; [0, CSP_MAX_NR_PADDR], number of addresses in
					 *      "paddrs" filed
					 */
	uint64_t paddrs[CSP_MAX_NR_PADDR]; /* out; returned addresses, mesi, and uniq, for each
					    *	   entry of array:
					    *	bit 0:        uniq in tag
					    *	bit 1-2:      mesi in tag
					    *	bit 3-5:      0
					    *	bit 6-15/16:  cache line set, SLC: bit 6-15,
					    *		      SF: bit 6-16
					    *	bit 16/17-47: physical address in tag, SLC:
					    *		      bit 16-47, SF: bit 17-47
					    *	bit 48-63:    0
					    */
} __packed;

#define CCS_FLUSH_LATENCY_LIMIT	1024

#define CCS_STEP_DEFAULT	16
#define CCS_STEP_MIN		1
#define CCS_STEP_MAX		128

struct ccs_param {
	int step;
	int sf_check_mesi;
	int sf_mesi;
	int hnf_start;
	int hnf_end;
	int max_frequ;
};

enum {
	CCS_FREQU_1,
	CCS_FREQU_4,
	CCS_FREQU_16,
	CCS_FREQU_N,
	CCS_NR_FREQU,
};

struct ccs_paddr_hnode {
	unsigned long paddr;
	struct page *page;
	struct hlist_node hnode;
};

#define CCS_HASH_BITS			9

struct ccs_hash {
	DECLARE_HASHTABLE(buckets, CCS_HASH_BITS);
};

struct ccs_addrs {
	int nr_round;
	int nrs[CCS_NR_FREQU];
	struct hlist_head (*lists)[CSP_MAX_NR_HNF][CCS_NR_FREQU];
};

struct ccs_stats {
	int nr_entry_total;
	int nr_entry_max;
	int nr_flush_total;
	int nr_flush_max;
	u64 ns_smc_total;
	u64 ns_smc_max;
	u64 ns_flush_total;
	u64 ns_flush_max;
};

struct ccs_cml_stats {
	int nr_scan;
	int nr_entry_total;
	int nr_entry_max;
	int nr_flush_total;
	int nr_flush_max;
	int nr_invalid_addr;
	int nr_dup_addr;
	int nr_addr_frequ[CCS_NR_FREQU];
	int nr_addr_frequ_max[CCS_NR_FREQU];
	u64 ns_smc_total;
	u64 ns_smc_max;
	u64 ns_flush_total;
	u64 ns_flush_max;
};

struct ccs_pos {
	int hnf;
	int round;
};

struct ccs {
	unsigned int mem_hotplugging : 1;
	atomic_t open_count;
	struct ccs_param param;
	struct cmn_scan_param *smc_param;
	struct ccs_hash *hash_tbl;
	struct ccs_addrs addrs;
	struct ccs_pos pos;
	struct mutex mutex;
	struct ccs_stats stats;
	struct ccs_cml_stats cml_stats;
};

static struct kmem_cache *ccs_anode_cachep __read_mostly;

static struct ccs_paddr_hnode *ccs_alloc_anode(void)
{
	struct ccs_paddr_hnode *anode;

	anode = kmem_cache_alloc(ccs_anode_cachep, GFP_KERNEL);
	if (anode)
		INIT_HLIST_NODE(&anode->hnode);

	return anode;
}

static void ccs_free_anode(struct ccs_paddr_hnode *anode)
{
	kmem_cache_free(ccs_anode_cachep, anode);
}

static DEFINE_MUTEX(ccs_die_mutex);
static bool ccs_mem_hotplugging;
static struct ccs *ccs_dies[CSP_NR_DIE];

static int smc_cmn_scan(struct ccs *ccs, int hnf, int round)
{
	struct cmn_scan_param *csp = ccs->smc_param;
	struct ccs_param *param = &ccs->param;
	struct ccs_stats *stats = &ccs->stats;
	struct arm_smccc_res smc_res = {0};
	u64 start, duration;

	start = sched_clock();
	csp->hnf_id = hnf;
	csp->scan_target = CSP_TARGET_SF_SLC;
	csp->check_mesi = param->sf_check_mesi;
	csp->mesi = param->sf_mesi;
	csp->check_uniq = 1;
	csp->uniq = 0;
	csp->scan_start = round * param->step;
	csp->scan_len = min_t(int, param->step, CSP_MIN_NR_SET - csp->scan_start);
	csp->nr_paddr = 0;
	arm_smccc_smc(SMC_AIS_OEM_CMD, SMC_CMN_SCAN, (unsigned long)csp,
		      sizeof(*csp), 0, 0, 0, 0, &smc_res);
	if (smc_res.a0) {
		WARN_ONCE(1, "smc call fails with return value %ld\n",
			  (long)smc_res.a0);
		return -EIO;
	}
	duration = sched_clock() - start;
	stats->ns_smc_total += duration;
	stats->ns_smc_max = max(stats->ns_smc_max, duration);
	stats->nr_entry_total += csp->nr_paddr;
	stats->nr_entry_max = max_t(int, csp->nr_paddr, stats->nr_entry_max);

	return 0;
}

#ifdef CONFIG_KFENCE
bool page_is_kfence(struct page *page)
{
	return PageKfence(page);
}
#else
bool page_is_kfence(struct page *page)
{
	return false;
}
#endif

static struct page *ccs_paddr_to_page(unsigned long paddr)
{
	unsigned long pfn = PHYS_PFN(paddr);
	struct page *page;

	if (!pfn_valid(pfn))
		return NULL;
	/* Only online pages have kernel mapping (esp., not ZONE_DEVICE) */
	page = pfn_to_online_page(pfn);
	if (!page)
		return NULL;
	if (page_is_kfence(page) || PageHWPoison(page))
		return NULL;
	if (!PageLRU(page) && !PageCompound(page) && !PageReserved(page))
		return NULL;

	return page;
}

static void *ccs_map(unsigned long paddr, struct page *page)
{
	if (!page) {
		VM_WARN_ONCE(1, "Invalid page");
		return NULL;
	}
	preempt_disable();
	if (page_is_kfence(page) || PageHWPoison(page))
		goto fail;
	if (!PageLRU(page) && !PageCompound(page) && !PageReserved(page))
		goto fail;

	return kmap_atomic(page) + (paddr & (PAGE_SIZE - 1));
fail:
	preempt_enable();
	return NULL;
}

static void ccs_unmap(void *vaddr)
{
	if (!vaddr)
		return;
	kunmap_atomic(vaddr);
	preempt_enable();
}

static int ccs_flush_cache(struct ccs *ccs, unsigned long paddr, struct page *page)
{
	void *vaddr;

	vaddr = ccs_map(paddr, page);
	if (!vaddr) {
		ccs->cml_stats.nr_invalid_addr++;
		return -EINVAL;
	}
	asm volatile("dc civac, %0"
		     :: "r" (vaddr)
		     : "memory");
	ccs_unmap(vaddr);

	return 0;
}

static void ccs_check_paddr_info(struct cmn_scan_param *csp,
				 unsigned long paddr_info)
{
	int set, start, end;

	if (csp->check_mesi)
		WARN_ONCE(CSP_PADDR_MESI(paddr_info) != csp->mesi,
			  "ccs: MESI doesn't match: %d vs. 0x%016lx\n",
			  csp->mesi, paddr_info);
	else
		WARN_ONCE(CSP_PADDR_MESI(paddr_info) == CSP_MESI_I,
			  "ccs: should not collect MESI/I: 0x%016lx\n", paddr_info);
	WARN_ONCE(csp->check_uniq && CSP_PADDR_UNIQ(paddr_info) != csp->uniq,
		  "ccs: UNIQUE doesn't match: %d vs. 0x%016lx\n",
		  csp->uniq, paddr_info);
	set = CSP_PADDR_SLC_SET(paddr_info);
	start = csp->scan_start;
	end = start + csp->scan_len;
	WARN_ONCE(set < start || set >= end,
		  "ccs: Invalid cache set 0x%x not in [0x%x-0x%x) for addr: 0x%016lx\n",
		  set, start, end, paddr_info);
}

static void ccs_flush_paddrs_direct(struct ccs *ccs)
{
	struct ccs_cml_stats *cml_stats = &ccs->cml_stats;
	struct cmn_scan_param *csp = ccs->smc_param;
	struct ccs_stats *stats = &ccs->stats;
	int rc, i, nr_paddr, nr_flush = 0;
	unsigned long paddrinfo, paddr;
	struct page *page;

	nr_paddr = csp->nr_paddr;
	for (i = 0; i < nr_paddr; i++) {
		paddrinfo = csp->paddrs[i];
		ccs_check_paddr_info(csp, paddrinfo);
		paddr = CSP_PADDR_PA(paddrinfo);
		page = ccs_paddr_to_page(paddr);
		if (!page) {
			cml_stats->nr_invalid_addr++;
			continue;
		}
		rc = ccs_flush_cache(ccs, paddr, page);
		if (!rc) {
			nr_flush++;
			/* avoid too long latency */
			if ((nr_flush % CCS_FLUSH_LATENCY_LIMIT) == 0)
				cond_resched();
		} else {
			cml_stats->nr_invalid_addr++;
		}
	}
	stats->nr_flush_total += nr_flush;
	stats->nr_flush_max = max(nr_flush, stats->nr_flush_max);
}

static u32 ccs_hash(unsigned long paddr)
{
	return hash_long(paddr, CCS_HASH_BITS);
}

static struct ccs_paddr_hnode *__ccs_hash_find(struct ccs *ccs, unsigned long paddr)
{
	struct ccs_paddr_hnode *anode;

	hash_for_each_possible(ccs->hash_tbl->buckets, anode, hnode, ccs_hash(paddr)) {
		if (anode->paddr == paddr)
			return anode;
	}

	return NULL;
}

static int ccs_hash_add(struct ccs *ccs, unsigned long paddr, struct page *page)
{
	struct ccs_paddr_hnode *anode;

	if (__ccs_hash_find(ccs, paddr)) {
		ccs->cml_stats.nr_dup_addr++;
		return 0;
	}
	anode = ccs_alloc_anode();
	if (!anode)
		return -ENOMEM;
	anode->paddr = paddr;
	anode->page = page;
	hash_add(ccs->hash_tbl->buckets, &anode->hnode, ccs_hash(paddr));

	return 0;
}

static void ccs_hash_destroy(struct ccs_paddr_hnode *anode)
{
	hash_del(&anode->hnode);
	ccs_free_anode(anode);
}

static void ccs_hash_cleanup(struct ccs *ccs)
{
	struct ccs_paddr_hnode *anode;
	struct hlist_node *tmp;
	int bkt;

	hash_for_each_safe(ccs->hash_tbl->buckets, bkt, tmp, anode, hnode)
		ccs_free_anode(anode);

	hash_init(ccs->hash_tbl->buckets);
}

static int ccs_collect_paddrs(struct ccs *ccs)
{
	struct cmn_scan_param *csp = ccs->smc_param;
	unsigned long paddrinfo, paddr;
	int rc, i, nr_paddr;
	struct page *page;

	nr_paddr = csp->nr_paddr;
	for (i = 0; i < nr_paddr; i++) {
		paddrinfo = csp->paddrs[i];
		ccs_check_paddr_info(csp, paddrinfo);
		paddr = CSP_PADDR_PA(paddrinfo);
		page = ccs_paddr_to_page(paddr);
		if (!page) {
			ccs->cml_stats.nr_invalid_addr++;
			continue;
		}
		rc = ccs_hash_add(ccs, paddr, page);
		if (rc)
			return rc;
	}

	return 0;
}

static struct hlist_head *ccs_addrs_get_list(struct ccs_addrs *addrs,
					     int round, int hnf, int frequ)
{
	return &addrs->lists[round][hnf][frequ];
}

static void ccs_addrs_add(struct ccs_addrs *addrs, struct hlist_head *head,
			  struct ccs_paddr_hnode *anode, int frequ)
{
	hlist_add_head(&anode->hnode, head);
	addrs->nrs[frequ]++;
}

static void ccs_addrs_del(struct ccs_addrs *addrs, struct ccs_paddr_hnode *anode,
			  int frequ)
{
	hash_del(&anode->hnode);
	addrs->nrs[frequ]--;
}

static void ccs_addrs_move(struct ccs_addrs *addrs, struct hlist_head *head,
			   struct ccs_paddr_hnode *anode,
			   int frequ_from, int frequ_to)
{
	ccs_addrs_del(addrs, anode, frequ_from);
	ccs_addrs_add(addrs, head, anode, frequ_to);
}

static void ccs_addrs_destroy(struct ccs_addrs *addrs, struct ccs_paddr_hnode *anode,
			      int frequ)
{
	ccs_addrs_del(addrs, anode, frequ);
	ccs_free_anode(anode);
}

static void ccs_update_addrs_frequ(struct ccs *ccs, int hnf, int round)
{
	struct ccs_addrs *addrs = &ccs->addrs;
	struct hlist_head heads[CCS_NR_FREQU];
	struct ccs_paddr_hnode *anode, *hit;
	struct hlist_head *head;
	struct hlist_node *next;
	int max_frequ = ccs->param.max_frequ;
	int frequ, hit_frequ, miss_frequ;
	int bkt;

	for (frequ = 0; frequ <= max_frequ; frequ++)
		INIT_HLIST_HEAD(&heads[frequ]);

	for (frequ = 0; frequ <= max_frequ; frequ++) {
		head = ccs_addrs_get_list(addrs, round, hnf, frequ);
		hit_frequ = frequ;
		if (frequ < max_frequ)
			hit_frequ++;
		hlist_for_each_entry_safe(anode, next, head, hnode) {
			hit = __ccs_hash_find(ccs, anode->paddr);
			if (hit) {
				/* hit, try higher frequency */
				ccs_hash_destroy(hit);
				ccs_addrs_move(addrs, &heads[hit_frequ],
					       anode, frequ, hit_frequ);
			} else {
				if (frequ == 0) {
					/* miss lowest frequency, stop tracking */
					ccs_addrs_destroy(addrs, anode, frequ);
				} else {
					/* miss, try lower frequency */
					miss_frequ = frequ - 1;
					ccs_addrs_move(addrs, &heads[miss_frequ],
						       anode, frequ, miss_frequ);
				}
			}
		}
	}

	/*
	 * all addr nodes in addrs lists are moved to temp lists heads[] with
	 * frequency adjusted in above step, move them back to addrs lists.
	 */
	for (frequ = 0; frequ <= max_frequ; frequ++) {
		head = ccs_addrs_get_list(addrs, round, hnf, frequ);
		hlist_move_list(&heads[frequ], head);
	}

	/* new addresses starts with lowest frequency */
	head = ccs_addrs_get_list(addrs, round, hnf, 0);
	hash_for_each_safe(ccs->hash_tbl->buckets, bkt, next, anode, hnode) {
		hash_del(&anode->hnode);
		ccs_addrs_add(addrs, head, anode, 0);
	}
}

/*
 * flush frequency:
 *    1: flush once every full scan
 *    4: flush once every 1/4 HNF scan
 *   16: flush once every 1/16 HNF scan
 *    N: flush once every HNF scan
 */
static int ccs_frequ_to_nr_hnf(int nr_hnf_total, int frequ)
{
	int nr_hnf;

	switch (frequ) {
	case CCS_FREQU_1:
		nr_hnf = nr_hnf_total;
		break;
	case CCS_FREQU_4:
		nr_hnf = max(nr_hnf_total / 4, 1);
		break;
	case CCS_FREQU_16:
		nr_hnf = max(nr_hnf_total / 16, 1);
		break;
	case CCS_FREQU_N:
		nr_hnf = 1;
		break;
	default:
		WARN_ONCE(1, "ccs: Invalid flush frequency!");
		nr_hnf = nr_hnf_total;
		break;
	}

	return nr_hnf;
}

static void ccs_addrs_flush_cache(struct ccs *ccs, int hnf, int round)
{
	struct ccs_param *param = &ccs->param;
	struct ccs_stats *stats = &ccs->stats;
	struct ccs_paddr_hnode *anode;
	struct hlist_head *head;
	struct hlist_node *next;
	int nr_hnf_total, h, hdist;
	int f, p, pp = -1;
	int nr_flush = 0;
	int rc;

	nr_hnf_total = param->hnf_end - param->hnf_start + 1;
	for (f = 0; f <= param->max_frequ; f++) {
		p = ccs_frequ_to_nr_hnf(nr_hnf_total, f);
		if (p == pp)
			continue;
		pp = p;
		for (h = param->hnf_start; h <= param->hnf_end; h++) {
			if (hnf < h)
				hdist = hnf + nr_hnf_total - h;
			else
				hdist = hnf - h;
			/*
			 * flush addrs of HNF 'h' every 'p' HNF, 'p' is number
			 * of HNF for frequency 'f'.
			 */
			if (hdist % p != 0)
				continue;
			head = ccs_addrs_get_list(&ccs->addrs, round, h, f);
			hlist_for_each_entry_safe(anode, next, head, hnode) {
				rc = ccs_flush_cache(ccs, anode->paddr, anode->page);
				if (rc) {
					ccs_addrs_destroy(&ccs->addrs, anode, f);
				} else {
					nr_flush++;
					/* avoid too long latency */
					if ((nr_flush % CCS_FLUSH_LATENCY_LIMIT) == 0)
						cond_resched();
				}
			}
		}
	}
	stats->nr_flush_total += nr_flush;
	stats->nr_flush_max = max(nr_flush, stats->nr_flush_max);
}

static int ccs_nr_round(struct ccs_param *param)
{
	return DIV_ROUND_UP(CSP_MIN_NR_SET, param->step);
}

static int ccs_addrs_setup(struct ccs_addrs *addrs, struct ccs_param *param)
{
	int round, hnf, frequ;

	if (addrs->lists)
		return 0;

	addrs->nr_round = ccs_nr_round(param);
	addrs->lists = kvmalloc(sizeof(*addrs->lists) * addrs->nr_round,
				GFP_KERNEL);
	if (!addrs->lists)
		return -ENOMEM;

	for (round = 0; round < addrs->nr_round; round++) {
		for (hnf = 0; hnf < CSP_MAX_NR_HNF; hnf++) {
			for (frequ = 0; frequ < CCS_NR_FREQU; frequ++) {
				struct hlist_head *head;

				head = ccs_addrs_get_list(addrs, round, hnf, frequ);
				INIT_HLIST_HEAD(head);
			}
		}
	}

	return 0;
}

static bool ccs_addrs_need_cleanup(struct ccs_param *old_param, struct ccs_param *new_param)
{
	return ccs_nr_round(old_param) != ccs_nr_round(new_param) ||
		old_param->hnf_start != new_param->hnf_start ||
		old_param->hnf_end != new_param->hnf_end ||
		old_param->max_frequ != new_param->max_frequ;
}

static void ccs_addrs_cleanup(struct ccs_addrs *addrs)
{
	int round, hnf, frequ;

	if (!addrs->lists)
		return;

	for (round = 0; round < addrs->nr_round; round++) {
		for (hnf = 0; hnf < CSP_MAX_NR_HNF; hnf++) {
			for (frequ = 0; frequ < CCS_NR_FREQU; frequ++) {
				struct hlist_head *head;
				struct hlist_node *next;
				struct ccs_paddr_hnode *anode;

				head = ccs_addrs_get_list(addrs, round, hnf, frequ);
				hlist_for_each_entry_safe(anode, next, head, hnode)
					ccs_free_anode(anode);
			}
		}
	}

	kvfree(addrs->lists);
	addrs->lists = NULL;
	addrs->nr_round = 0;
}

static int ccs_flush_paddrs(struct ccs *ccs, int hnf, int round)
{
	struct ccs_stats *stats = &ccs->stats;
	u64 start, duration;
	int rc;

	start = sched_clock();
	if (ccs->param.max_frequ) {
		rc = ccs_collect_paddrs(ccs);
		if (rc)
			return rc;
		ccs_update_addrs_frequ(ccs, hnf, round);
		ccs_addrs_flush_cache(ccs, hnf, round);
	} else {
		ccs_flush_paddrs_direct(ccs);
	}
	duration = sched_clock() - start;
	stats->ns_flush_total += duration;
	stats->ns_flush_max = max(stats->ns_flush_max, duration);

	return 0;
}

static int ccs_scan_hnf_round(struct ccs *ccs, int hnf, int round)
{
	int rc;

	rc = smc_cmn_scan(ccs, hnf, round);
	if (rc)
		return rc;
	rc = ccs_flush_paddrs(ccs, hnf, round);
	if (rc)
		return rc;
	if (signal_pending(current))
		return -EINTR;

	return 0;
}

static int ccs_scan_hnf(struct ccs *ccs, int hnf)
{
	int round;
	int rc;

	for (round = 0; round < ccs->addrs.nr_round; round++) {
		rc = ccs_scan_hnf_round(ccs, hnf, round);
		if (rc)
			return rc;
		cond_resched();
	}

	return 0;
}

static void ccs_cumulate_stats(struct ccs *ccs)
{
	struct ccs_cml_stats *cml_stats = &ccs->cml_stats;
	struct ccs_stats *stats = &ccs->stats;
	int i, nr;

	cml_stats->nr_scan++;
	cml_stats->nr_entry_total += stats->nr_entry_total;
	cml_stats->nr_entry_max = max(cml_stats->nr_entry_max, stats->nr_entry_max);
	cml_stats->nr_flush_total += stats->nr_flush_total;
	cml_stats->nr_flush_max = max(cml_stats->nr_flush_max, stats->nr_flush_max);
	cml_stats->ns_smc_total += stats->ns_smc_total;
	cml_stats->ns_smc_max = max(cml_stats->ns_smc_max, stats->ns_smc_max);
	cml_stats->ns_flush_total += stats->ns_flush_total;
	cml_stats->ns_flush_max = max(cml_stats->ns_flush_max, stats->ns_flush_max);
	for (i = 0; i < CCS_NR_FREQU; i++) {
		nr = ccs->addrs.nrs[i];
		cml_stats->nr_addr_frequ[i] += nr;
		cml_stats->nr_addr_frequ_max[i] = max(cml_stats->nr_addr_frequ_max[i], nr);
	}
}

static int ccs_scan(struct ccs *ccs)
{
	struct ccs_param *param = &ccs->param;
	int rc = 0, hnf;

	rc = ccs_addrs_setup(&ccs->addrs, param);
	if (rc)
		goto out;
	memset(&ccs->stats, 0, sizeof(ccs->stats));
	for (hnf = param->hnf_start; hnf <= param->hnf_end; hnf++) {
		rc = ccs_scan_hnf(ccs, hnf);
		if (rc)
			goto out;
	}
	ccs_cumulate_stats(ccs);
out:
	ccs_hash_cleanup(ccs);

	return rc;
}

static int ccs_scan_step(struct ccs *ccs)
{
	struct ccs_param *param = &ccs->param;
	struct ccs_pos *pos = &ccs->pos;
	int rc;

	rc = ccs_addrs_setup(&ccs->addrs, param);
	if (rc)
		return rc;
	if (pos->hnf == param->hnf_start && pos->round == 0)
		memset(&ccs->stats, 0, sizeof(ccs->stats));
	rc = ccs_scan_hnf_round(ccs, pos->hnf, pos->round);
	if (rc)
		goto out;
	pos->round++;
	if (pos->round >= ccs->addrs.nr_round) {
		pos->round = 0;
		pos->hnf++;
		if (pos->hnf > param->hnf_end) {
			pos->hnf = param->hnf_start;
			ccs_cumulate_stats(ccs);
		}
	}
out:
	ccs_hash_cleanup(ccs);

	return rc;
}

static int ccs_set_param(struct ccs *ccs, struct ccs_param *param)
{
	if (ccs_addrs_need_cleanup(&ccs->param, param)) {
		ccs_addrs_cleanup(&ccs->addrs);
		ccs->pos.hnf = param->hnf_start;
		ccs->pos.round = 0;
	}
	ccs->param = *param;

	return 0;
}

static int ccs_die_id(void)
{
	int nr_cpu_die = num_possible_cpus() / CSP_NR_DIE;

	return min(raw_smp_processor_id() / nr_cpu_die, CSP_NR_DIE - 1);
}

static void ccs_init_param(struct ccs_param *param)
{
	*param = ((struct ccs_param) {
		.step		= CCS_STEP_DEFAULT,
		.sf_check_mesi	= 1,
		.sf_mesi	= CSP_MESI_S,
	});

	param->hnf_start = ccs_die_id() * CSP_MAX_NR_HNF_DIE;
	param->hnf_end = param->hnf_start + CSP_MAX_NR_HNF_DIE - 1;
}

static int ccs_init(struct ccs *ccs)
{
	ccs->smc_param = kvzalloc(sizeof(*ccs->smc_param), GFP_KERNEL);
	if (!ccs->smc_param)
		return -ENOMEM;
	mutex_init(&ccs->mutex);
	ccs_init_param(&ccs->param);
	ccs->pos.hnf = ccs->param.hnf_start;
	ccs->hash_tbl = kvzalloc(sizeof(*ccs->hash_tbl), GFP_KERNEL);
	if (!ccs->hash_tbl) {
		kvfree(ccs->smc_param);
		return -ENOMEM;
	}
	hash_init(ccs->hash_tbl->buckets);

	return 0;
}

static void ccs_fini(struct ccs *ccs)
{
	ccs_addrs_cleanup(&ccs->addrs);
	kvfree(ccs->hash_tbl);
	kvfree(ccs->smc_param);
}

static void *ccs_seq_start(struct seq_file *s, loff_t *pos)
{
	struct ccs *ccs = s->private;

	return *pos < 1 ? ccs : NULL;
}

static void *ccs_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	++*pos;

	return NULL;
}

static void ccs_seq_stop(struct seq_file *s, void *v)
{
}

static int ccs_seq_show(struct seq_file *s, void *v)
{
	struct ccs *ccs = v;
	struct ccs_stats *stats = &ccs->stats;
	struct ccs_cml_stats *cml_stats = &ccs->cml_stats;
	struct ccs_addrs *addrs = &ccs->addrs;
	int i;

	if (!mutex_trylock(&ccs->mutex))
		return -EBUSY;
	seq_printf(s, "nr_entry_total:     %10d\n", stats->nr_entry_total);
	seq_printf(s, "nr_entry_max:       %10d\n", stats->nr_entry_max);
	seq_printf(s, "nr_flush_total:     %10d\n", stats->nr_flush_total);
	seq_printf(s, "nr_flush_max:       %10d\n", stats->nr_flush_max);
	seq_printf(s, "us_smc_total:       %10llu\n",
		   stats->ns_smc_total / NSEC_PER_USEC);
	seq_printf(s, "us_smc_max:         %10llu\n",
		   stats->ns_smc_max / NSEC_PER_USEC);
	seq_printf(s, "us_flush_total:     %10llu\n",
		   stats->ns_flush_total / NSEC_PER_USEC);
	seq_printf(s, "us_flush_max:       %10llu\n",
		   stats->ns_flush_max / NSEC_PER_USEC);
	for (i = 0; i < CCS_NR_FREQU; i++)
		seq_printf(s, "nr_addrs[%d]:        %10d\n", i, addrs->nrs[i]);

	seq_printf(s, "cml_nr_scan:        %10d\n", cml_stats->nr_scan);
	seq_printf(s, "cml_nr_entry_total: %10d\n", cml_stats->nr_entry_total);
	seq_printf(s, "cml_nr_entry_max:   %10d\n", cml_stats->nr_entry_max);
	seq_printf(s, "cml_nr_flush_total: %10d\n", cml_stats->nr_flush_total);
	seq_printf(s, "cml_nr_flush_max:   %10d\n", cml_stats->nr_flush_max);
	seq_printf(s, "cml_us_smc_total:   %10llu\n",
		   cml_stats->ns_smc_total / NSEC_PER_USEC);
	seq_printf(s, "cml_us_smc_max:     %10llu\n",
		   cml_stats->ns_smc_max / NSEC_PER_USEC);
	seq_printf(s, "cml_us_flush_total: %10llu\n",
		   cml_stats->ns_flush_total / NSEC_PER_USEC);
	seq_printf(s, "cml_us_flush_max:   %10llu\n",
		   cml_stats->ns_flush_max / NSEC_PER_USEC);
	seq_printf(s, "cml_nr_invalid_addr:%10d\n", cml_stats->nr_invalid_addr);
	seq_printf(s, "cml_nr_dup_addr:    %10d\n", cml_stats->nr_dup_addr);
	for (i = 0; i < CCS_NR_FREQU; i++)
		seq_printf(s, "cml_nr_addr[%d]:     %10d\n", i,
			   cml_stats->nr_addr_frequ[i]);
	for (i = 0; i < CCS_NR_FREQU; i++)
		seq_printf(s, "cml_nr_addr_max[%d]: %10d\n", i,
			   cml_stats->nr_addr_frequ_max[i]);
	mutex_unlock(&ccs->mutex);

	return 0;
}

static const struct seq_operations ccs_seq_ops = {
	.start = ccs_seq_start,
	.next  = ccs_seq_next,
	.stop  = ccs_seq_stop,
	.show  = ccs_seq_show
};

static int ccs_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct ccs *ccs;
	int rc, die_id = ccs_die_id();

	mutex_lock(&ccs_die_mutex);
	ccs = ccs_dies[die_id];
	mutex_unlock(&ccs_die_mutex);
	if (!ccs) {
		ccs = kzalloc(sizeof(*ccs), GFP_KERNEL);
		if (!ccs)
			return -ENOMEM;
		rc = ccs_init(ccs);
		if (rc) {
			kfree(ccs);
			return rc;
		}
		mutex_lock(&ccs_die_mutex);
		if (!ccs_dies[die_id]) {
			if (ccs_mem_hotplugging)
				ccs->mem_hotplugging = 1;
			ccs_dies[die_id] = ccs;
		} else {
			ccs_fini(ccs);
			kfree(ccs);
			ccs = ccs_dies[die_id];
		}
		mutex_unlock(&ccs_die_mutex);
	}
	file->private_data = NULL;
	rc = seq_open(file, &ccs_seq_ops);
	if (rc)
		return rc;
	seq = file->private_data;
	seq->private = ccs;
	atomic_inc(&ccs->open_count);

	return 0;
}

static void ccs_cleanup(struct ccs *ccs)
{
	if (!mutex_trylock(&ccs->mutex))
		return;
	if (!atomic_read(&ccs->open_count))
		ccs_addrs_cleanup(&ccs->addrs);
	mutex_unlock(&ccs->mutex);
}

static int ccs_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct ccs *ccs;
	int rc;

	ccs = seq->private;
	seq->private = NULL;
	rc = seq_release(inode, file);
	if (atomic_dec_and_test(&ccs->open_count))
		ccs_cleanup(ccs);

	return rc;
}

enum {
	CCS_CMD_SET_PARAM = 0,
	CCS_CMD_SCAN,
	CCS_CMD_SCAN_STEP,
	CCS_CMD_NULL,
};

static const match_table_t ccs_cmd_tokens = {
	{ CCS_CMD_SET_PARAM,	"set_param" },
	{ CCS_CMD_SCAN,		"scan" },
	{ CCS_CMD_SCAN_STEP,	"scan_step" },
	{ CCS_CMD_NULL,		NULL },
};

enum {
	CCS_PARAM_STEP = 0,
	CCS_PARAM_SF_MESI,
	CCS_PARAM_SF_CHECK_MESI,
	CCS_PARAM_HNF,
	CCS_PARAM_MAX_FREQU,
	CCS_PARAM_NULL,
};

static const match_table_t ccs_param_tokens = {
	{ CCS_PARAM_STEP,		"step=%d" },
	{ CCS_PARAM_SF_MESI,		"sf_mesi=%d" },
	{ CCS_PARAM_SF_CHECK_MESI,	"sf_check_mesi=%d" },
	{ CCS_PARAM_HNF,		"hnf=%d-%d" },
	{ CCS_PARAM_MAX_FREQU,		"max_frequ=%d" },
	{ CCS_PARAM_NULL,		NULL },
};

static int ccs_parse_param(char *buf, struct ccs_param *param)
{
	substring_t args[MAX_OPT_ARGS];
	char *start;
	int rc = 0;

	ccs_init_param(param);
	while ((start = strsep(&buf, " ")) != NULL) {
		if (!strlen(start))
			continue;
		switch (match_token(start, ccs_param_tokens, args)) {
		case CCS_PARAM_STEP:
			if (match_int(&args[0], &param->step)) {
				rc = -EINVAL;
				goto out;
			}
			if (param->step < CCS_STEP_MIN ||
			    param->step > CCS_STEP_MAX) {
				rc = -EINVAL;
				goto out;
			}
			break;
		case CCS_PARAM_SF_MESI:
			if (match_int(&args[0], &param->sf_mesi)) {
				rc = -EINVAL;
				goto out;
			}
			if (param->sf_mesi < CSP_MESI_MIN ||
			    param->sf_mesi > CSP_MESI_MAX) {
				rc = -EINVAL;
				goto out;
			}
			param->sf_check_mesi = 1;
			break;
		case CCS_PARAM_SF_CHECK_MESI:
			if (match_int(&args[0], &param->sf_check_mesi)) {
				rc = -EINVAL;
				goto out;
			}
			if (param->sf_check_mesi < 0 ||
			    param->sf_check_mesi > 1) {
				rc = -EINVAL;
				goto out;
			}
			break;
		case CCS_PARAM_HNF:
			if (match_int(&args[0], &param->hnf_start)) {
				rc = -EINVAL;
				goto out;
			}
			if (match_int(&args[1], &param->hnf_end)) {
				rc = -EINVAL;
				goto out;
			}
			if (param->hnf_start < 0 ||
			    param->hnf_start >= CSP_MAX_NR_HNF ||
			    param->hnf_end >= CSP_MAX_NR_HNF ||
			    param->hnf_end < param->hnf_start) {
				rc = -EINVAL;
				goto out;
			}
			break;
		case CCS_PARAM_MAX_FREQU:
			if (match_int(&args[0], &param->max_frequ) ||
			    param->max_frequ < 0 ||
			    param->max_frequ >= CCS_NR_FREQU) {
				return -EINVAL;
				goto out;
			}
			break;
		default:
			rc = -EINVAL;
			goto out;
		}
	}
out:
	return rc;
}

static int ccs_parse_cmd(const char __user *ubuf, size_t count,
			 struct ccs_param *param)
{
	substring_t args[MAX_OPT_ARGS];
	char *rbuf, *buf, *start;
	int cmd = CCS_CMD_NULL, rc = -EINVAL;

	if (count + 1 > PAGE_SIZE)
		return -EINVAL;
	rbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!rbuf)
		return -ENOMEM;
	if (copy_from_user(rbuf, ubuf, count)) {
		rc = -EFAULT;
		goto out;
	}
	rbuf[count] = '\0';
	buf = strstrip(rbuf);
	while ((start = strsep(&buf, " ")) != NULL) {
		if (!strlen(start))
			continue;
		cmd = match_token(start, ccs_cmd_tokens, args);
		switch (cmd) {
		case CCS_CMD_SET_PARAM:
			rc = ccs_parse_param(buf, param);
			break;
		case CCS_CMD_SCAN:
			/* No parameter is allowed for scan */
			rc = buf ? -EINVAL : 0;
			break;
		case CCS_CMD_SCAN_STEP:
			/* No parameter is allowed for scan_step */
			rc = buf ? -EINVAL : 0;
			break;
		default:
			rc = -EINVAL;
			break;
		}
		break;
	}
out:
	kfree(rbuf);
	if (!rc)
		rc = cmd;

	return rc;
}

static ssize_t ccs_write(struct file *file, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct ccs *ccs = seq->private;
	struct ccs_param param;
	int rc, cmd;

	if (!mutex_trylock(&ccs->mutex))
		return -EBUSY;
	if (ccs->mem_hotplugging) {
		rc = -EBUSY;
		goto unlock_out;
	}
	cmd = ccs_parse_cmd(ubuf, count, &param);
	switch (cmd) {
	case CCS_CMD_SET_PARAM:
		rc = ccs_set_param(ccs, &param);
		break;
	case CCS_CMD_SCAN:
		rc = ccs_scan(ccs);
		break;
	case CCS_CMD_SCAN_STEP:
		rc = ccs_scan_step(ccs);
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (!rc)
		rc = count;
unlock_out:
	mutex_unlock(&ccs->mutex);

	return rc;
}

static long ccs_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct file_operations ccs_fops = {
	.owner		= THIS_MODULE,
	.open           = ccs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= ccs_write,
	.unlocked_ioctl	= ccs_ioctl,
	.release        = ccs_release,
};

static struct miscdevice ccs_miscdev = {
	.name = "cmn700_cache_scan",
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &ccs_fops,
};

static void ccs_memory_hotplugging(void)
{
	struct ccs *ccs;
	int i;

	mutex_lock(&ccs_die_mutex);
	ccs_mem_hotplugging = true;
	mutex_unlock(&ccs_die_mutex);
	for (i = 0; i < CSP_NR_DIE; i++) {
		mutex_lock(&ccs_die_mutex);
		ccs = ccs_dies[i];
		mutex_unlock(&ccs_die_mutex);
		if (ccs) {
			mutex_lock(&ccs->mutex);
			ccs->mem_hotplugging = 1;
			mutex_unlock(&ccs->mutex);
		}
	}
}

static void ccs_memory_hotplugged(void)
{
	struct ccs *ccs;
	int i;

	mutex_lock(&ccs_die_mutex);
	ccs_mem_hotplugging = false;
	mutex_unlock(&ccs_die_mutex);
	for (i = 0; i < CSP_NR_DIE; i++) {
		mutex_lock(&ccs_die_mutex);
		ccs = ccs_dies[i];
		mutex_unlock(&ccs_die_mutex);
		if (ccs) {
			mutex_lock(&ccs->mutex);
			ccs_addrs_cleanup(&ccs->addrs);
			ccs->mem_hotplugging = 0;
			mutex_unlock(&ccs->mutex);
		}
	}
}

static int ccs_memory_callback(struct notifier_block *self,
			       unsigned long action, void *_arg)
{
	switch (action) {
	case MEM_GOING_OFFLINE:
	case MEM_GOING_ONLINE:
		ccs_memory_hotplugging();
		break;
	case MEM_OFFLINE:
	case MEM_CANCEL_OFFLINE:
	case MEM_ONLINE:
	case MEM_CANCEL_ONLINE:
		ccs_memory_hotplugged();
		break;
	}
	return 0;
}

static struct notifier_block ccs_memory_nb = {
	.notifier_call = ccs_memory_callback,
	.priority = 0
};

static int __init ccs_module_init(void)
{
	int rc;

	ccs_anode_cachep = kmem_cache_create("ccs_anode_cache",
					     sizeof(struct ccs_paddr_hnode),
					     0, SLAB_ACCOUNT, NULL);
	if (!ccs_anode_cachep)
		return -ENOMEM;

	mutex_init(&ccs_die_mutex);
	register_memory_notifier(&ccs_memory_nb);
	rc = misc_register(&ccs_miscdev);
	if (rc) {
		pr_err("ccs: misc registration failed\n");
		goto out;
	}

	return 0;
out:
	unregister_memory_notifier(&ccs_memory_nb);
	kmem_cache_destroy(ccs_anode_cachep);
	return rc;
}

static void __exit ccs_module_exit(void)
{
	struct ccs *ccs;
	int i;

	misc_deregister(&ccs_miscdev);
	unregister_memory_notifier(&ccs_memory_nb);
	for (i = 0; i < CSP_NR_DIE; i++) {
		ccs = ccs_dies[i];
		if (ccs) {
			ccs_fini(ccs);
			kfree(ccs);
		}
	}
	kmem_cache_destroy(ccs_anode_cachep);
}

module_init(ccs_module_init);
module_exit(ccs_module_exit);

MODULE_AUTHOR("Ying Huang <ying.huang@linux.alibaba.com>");
MODULE_DESCRIPTION("ARM CMN-700 performance erratum 3688582 mitigation");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
