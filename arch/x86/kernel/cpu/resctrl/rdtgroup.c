// SPDX-License-Identifier: GPL-2.0-only
/*
 * User interface for Resource Alloction in Resource Director Technology(RDT)
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Fenghua Yu <fenghua.yu@intel.com>
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/fs_parser.h>
#include <linux/sysfs.h>
#include <linux/kernfs.h>
#include <linux/seq_buf.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/user_namespace.h>

#include <asm/resctrl.h>
#include "internal.h"

DEFINE_STATIC_KEY_FALSE(rdt_enable_key);
DEFINE_STATIC_KEY_FALSE(rdt_mon_enable_key);
DEFINE_STATIC_KEY_FALSE(rdt_alloc_enable_key);

/*
 * This is safe against resctrl_sched_in() called from __switch_to()
 * because __switch_to() is executed with interrupts disabled. A local call
 * from update_closid_rmid() is protected against __switch_to() because
 * preemption is disabled.
 */
void resctrl_arch_sync_cpu_defaults(void *info)
{
	struct resctrl_cpu_sync *r = info;

	if (r) {
		this_cpu_write(pqr_state.default_closid, r->closid);
		this_cpu_write(pqr_state.default_rmid, r->rmid);
	}

	/*
	 * We cannot unconditionally write the MSR because the current
	 * executing task might have its own closid selected. Just reuse
	 * the context switch code.
	 */
	resctrl_sched_in(current);
}

static void l3_qos_cfg_update(void *arg)
{
	bool *enable = arg;

	wrmsrl(MSR_IA32_L3_QOS_CFG, *enable ? L3_QOS_CDP_ENABLE : 0ULL);
}

static void l2_qos_cfg_update(void *arg)
{
	bool *enable = arg;

	wrmsrl(MSR_IA32_L2_QOS_CFG, *enable ? L2_QOS_CDP_ENABLE : 0ULL);
}

static int set_cache_qos_cfg(int level, bool enable)
{
	void (*update)(void *arg);
	struct rdt_resource *r_l;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int cpu;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	if (level == RDT_RESOURCE_L3)
		update = l3_qos_cfg_update;
	else if (level == RDT_RESOURCE_L2)
		update = l2_qos_cfg_update;
	else
		return -EINVAL;

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	r_l = &rdt_resources_all[level].r_resctrl;
	list_for_each_entry(d, &r_l->domains, list) {
		if (r_l->cache.arch_has_per_cpu_cfg)
			/* Pick all the CPUs in the domain instance */
			for_each_cpu(cpu, &d->cpu_mask)
				cpumask_set_cpu(cpu, cpu_mask);
		else
			/* Pick one CPU from each domain instance to update MSR */
			cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);
	}

	/* Update QOS_CFG MSR on all the CPUs in cpu_mask */
	on_each_cpu_mask(cpu_mask, update, &enable, 1);

	free_cpumask_var(cpu_mask);

	return 0;
}

/* Restore the qos cfg state when a domain comes online */
void rdt_domain_reconfigure_cdp(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	if (!r->cdp_capable)
		return;

	if (r->rid == RDT_RESOURCE_L2)
		l2_qos_cfg_update(&hw_res->cdp_enabled);

	if (r->rid == RDT_RESOURCE_L3)
		l3_qos_cfg_update(&hw_res->cdp_enabled);
}

static int cdp_enable(int level)
{
	struct rdt_resource *r_l = &rdt_resources_all[level].r_resctrl;
	int ret;

	if (!r_l->alloc_capable)
		return -EINVAL;

	ret = set_cache_qos_cfg(level, true);
	if (!ret)
		rdt_resources_all[level].cdp_enabled = true;

	return ret;
}

static void cdp_disable(int level)
{
	struct rdt_hw_resource *r_hw = &rdt_resources_all[level];

	if (r_hw->cdp_enabled) {
		set_cache_qos_cfg(level, false);
		r_hw->cdp_enabled = false;
	}
}

int resctrl_arch_set_cdp_enabled(enum resctrl_res_level l, bool enable)
{
	struct rdt_hw_resource *hw_res = &rdt_resources_all[l];

	if (!hw_res->r_resctrl.cdp_capable)
		return -EINVAL;

	if (enable)
		return cdp_enable(l);

	cdp_disable(l);

	return 0;
}

/*
 * Update L3_QOS_EXT_CFG MSR on all the CPUs associated with the resource.
 */
static void resctrl_abmc_set_one_amd(void *arg)
{
	bool *enable = arg;

	if (*enable)
		msr_set_bit(MSR_IA32_L3_QOS_EXT_CFG, ABMC_ENABLE_BIT);
	else
		msr_clear_bit(MSR_IA32_L3_QOS_EXT_CFG, ABMC_ENABLE_BIT);
}

static void _resctrl_abmc_enable(struct rdt_resource *r, bool enable)
{
	struct rdt_domain *d;

	/*
	 * Hardware counters will reset after switching the monitor mode.
	 * Reset the architectural state so that reading of hardware
	 * counter is not considered as an overflow in the next update.
	 */
	list_for_each_entry(d, &r->domains, list) {
		on_each_cpu_mask(&d->cpu_mask,
				 resctrl_abmc_set_one_amd, &enable, 1);
		resctrl_arch_reset_rmid_all(r, d);
	}
}

bool resctrl_arch_get_abmc_enabled(void)
{
	return rdt_resources_all[RDT_RESOURCE_L3].mbm_cntr_assign_enabled;
}

int resctrl_arch_mbm_cntr_assign_enable(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	if (r->mon.mbm_cntr_assignable && !hw_res->mbm_cntr_assign_enabled) {
		_resctrl_abmc_enable(r, true);
		hw_res->mbm_cntr_assign_enabled = true;
	}

	return 0;
}

void resctrl_arch_mbm_cntr_assign_configure(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	bool enable = true;

	if (r->mon.mbm_cntr_assignable) {
		if (!hw_res->mbm_cntr_assign_enabled)
			hw_res->mbm_cntr_assign_enabled = true;
		resctrl_abmc_set_one_amd(&enable);
	}
}

void resctrl_arch_mbm_cntr_assign_disable(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	if (hw_res->mbm_cntr_assign_enabled) {
		_resctrl_abmc_enable(r, false);
		hw_res->mbm_cntr_assign_enabled = false;
	}
}

bool resctrl_arch_get_mbm_cntr_assign_enable(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	return hw_res->mbm_cntr_assign_enabled;
}

static void rdtgroup_abmc_cfg(void *info)
{
	u64 *msrval = info;

	wrmsrl(MSR_IA32_L3_QOS_ABMC_CFG, *msrval);
}

/*
 * Send an IPI to the domain to assign the counter id to RMID.
 */
int resctrl_arch_assign_cntr(void *dom, enum resctrl_event_id evtid,
			     u32 rmid, u32 cntr_id, u32 closid, bool assign)
{
	struct rdt_domain *d = dom;
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	union l3_qos_abmc_cfg abmc_cfg = { 0 };
	struct arch_mbm_state *arch_mbm;

	abmc_cfg.split.cfg_en = 1;
	abmc_cfg.split.cntr_en = assign ? 1 : 0;
	abmc_cfg.split.cntr_id = cntr_id;
	abmc_cfg.split.bw_src = rmid;

	/* Update the event configuration from the domain */
	if (evtid == QOS_L3_MBM_TOTAL_EVENT_ID) {
		abmc_cfg.split.bw_type = hw_dom->mbm_total_cfg;
		arch_mbm = &hw_dom->arch_mbm_total[rmid];
	} else {
		abmc_cfg.split.bw_type = hw_dom->mbm_local_cfg;
		arch_mbm = &hw_dom->arch_mbm_local[rmid];
	}

	smp_call_function_any(&d->cpu_mask, rdtgroup_abmc_cfg, &abmc_cfg, 1);

	/*
	 * Reset the architectural state so that reading of hardware
	 * counter is not considered as an overflow in next update.
	 */
	if (arch_mbm)
		memset(arch_mbm, 0, sizeof(struct arch_mbm_state));

	return 0;
}

bool resctrl_arch_is_hwdrc_mb_capable(void)
{
	return is_hwdrc_mb_capable();
}

static void mba_enable(enum resctrl_res_level l)
{
	struct rdt_hw_resource *r_hw = &rdt_resources_all[l];
	struct rdt_resource *r = &r_hw->r_resctrl;

	r->alloc_capable = true;
}

static void mba_disable(enum resctrl_res_level l)
{
	struct rdt_hw_resource *r_hw = &rdt_resources_all[l];
	struct rdt_resource *r = &r_hw->r_resctrl;

	r->alloc_capable = false;
}

/*
 * Currently memory bandwidth HWDRC feature is enabled or disabled by the user
 * outside of the scope of the resctrl filesystem. When memory bandwidth HWDRC
 * is enabled, it takes over MBA hooks in resctrl for memory bandwidth
 * throttling.
 *
 * Set memory bandwidth HWDRC enabled in resctrl so that the user who enables
 * memory bandwidth HWDRC can make sure that resctrl doesn't provide any hooks
 * to control MBA.
 *
 * Set memory bandwidth HWDRC disabled in resctrl, MBA is enabled by default.
 */
int resctrl_arch_set_hwdrc_enabled(enum resctrl_res_level l, bool hwdrc_mb)
{
	struct rdt_resource *r = &rdt_resources_all[l].r_resctrl;

	if (!is_hwdrc_mb_capable() || hwdrc_mb == r->membw.hwdrc_mb)
		return -EINVAL;

	/* MBA and memory bandwidth HWDRC features are mutually exclusive */
	if (hwdrc_mb)
		mba_disable(l);
	else
		mba_enable(l);

	r->membw.hwdrc_mb = hwdrc_mb;

	return 0;
}

u32 resctrl_arch_event_config_get(void *dom,
				  enum resctrl_event_id eventid)
{
	struct rdt_domain *d = dom;
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);

	switch (eventid) {
	case QOS_L3_OCCUP_EVENT_ID:
	case QOS_MC_MBM_BPS_EVENT_ID:
		break;
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return hw_dom->mbm_total_cfg;
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		return hw_dom->mbm_local_cfg;
	}

	/* Never expect to get here */
	WARN_ON_ONCE(1);

	return INVALID_CONFIG_VALUE;
}

void resctrl_arch_event_config_set(void *info)
{
	struct mon_config_info *mon_info = info;
	struct rdt_hw_domain *hw_dom;
	unsigned int index;

	index = mon_event_config_index_get(mon_info->evtid);
	if (index == INVALID_CONFIG_INDEX)
		return;

	wrmsr(MSR_IA32_EVT_CFG_BASE + index, mon_info->mon_config, 0);

	hw_dom = resctrl_to_arch_dom(mon_info->d);

	switch (mon_info->evtid) {
	case QOS_L3_OCCUP_EVENT_ID:
		break;
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		hw_dom->mbm_total_cfg = mon_info->mon_config;
		break;
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		hw_dom->mbm_local_cfg =  mon_info->mon_config;
		break;
	}
}

int resctrl_arch_mbm_config_write_domain(void *rdt_resource, void *dom, u32 evtid, u32 val)
{
	struct rdt_resource *r = rdt_resource;
	struct rdt_domain *d = dom;
	struct mon_config_info mon_info = {0};
	u32 config_val;
	int ret = 0;

	/* mon_config cannot be more than the supported set of events */
	if (val > MAX_EVT_CONFIG_BITS)
		return -EINVAL;

	/*
	 * Check the current config value first. If both are the same then
	 * no need to write it again.
	 */
	config_val = resctrl_arch_event_config_get(d, evtid);
	if (config_val == INVALID_CONFIG_VALUE || config_val == val)
		goto out;

	mon_info.d = d;
	mon_info.evtid = evtid;
	mon_info.mon_config = val;

	/*
	 * Update MSR_IA32_EVT_CFG_BASE MSR on one of the CPUs in the
	 * domain. The MSRs offset from MSR MSR_IA32_EVT_CFG_BASE
	 * are scoped at the domain level. Writing any of these MSRs
	 * on one CPU is observed by all the CPUs in the domain.
	 */
	smp_call_function_any(&d->cpu_mask,
			      resctrl_arch_event_config_set,
			      &mon_info, 1);

	/*
	 * When an Event Configuration is changed, the bandwidth counters
	 * for all RMIDs and Events will be cleared by the hardware. The
	 * hardware also sets MSR_IA32_QM_CTR.Unavailable (bit 62) for
	 * every RMID on the next read to any event for every RMID.
	 * Subsequent reads will have MSR_IA32_QM_CTR.Unavailable (bit 62)
	 * cleared while it is tracked by the hardware. Clear the
	 * mbm_local and mbm_total counts for all the RMIDs.
	 */
	resctrl_arch_reset_rmid_all(r, d);

out:
	return ret;
}

static int reset_all_ctrls(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	struct rdt_hw_domain *hw_dom;
	struct msr_param msr_param;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	int i;

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	msr_param.res = r;
	msr_param.low = 0;
	msr_param.high = hw_res->num_closid;

	/*
	 * Disable resource control for this resource by setting all
	 * CBMs in all domains to the maximum mask value. Pick one CPU
	 * from each domain to update the MSRs below.
	 */
	list_for_each_entry(d, &r->domains, list) {
		hw_dom = resctrl_to_arch_dom(d);
		cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);

		for (i = 0; i < hw_res->num_closid; i++)
			hw_dom->ctrl_val[i] = r->default_ctrl;
	}

	/* Update CBM on all the CPUs in cpu_mask */
	on_each_cpu_mask(cpu_mask, rdt_ctrl_update, &msr_param, 1);

	free_cpumask_var(cpu_mask);

	return 0;
}

void resctrl_arch_reset_resources(void)
{
	struct rdt_resource *r;

	for_each_capable_rdt_resource(r)
		reset_all_ctrls(r);
}
