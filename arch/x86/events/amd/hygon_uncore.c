// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Chengdu Haiguang IC Design Co., Ltd.
 *
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/amd_nb.h>
#include <asm/perf_event.h>
#include <asm/msr.h>

#define NUM_COUNTERS_L3		6
#define NUM_COUNTERS_DF		4

#define UNCORE_NAME_LEN		16
#define COUNTER_SHIFT		16

struct hygon_uncore_ctx {
	int refcnt;
	int cpu;
	struct perf_event **events;
};

struct hygon_uncore_pmu {
	char name[UNCORE_NAME_LEN];
	int type;
	int num_counters;
	u32 msr_base;
	cpumask_t active_mask;
	struct pmu pmu;
	struct hygon_uncore_ctx * __percpu *ctx;
};

enum {
	UNCORE_TYPE_L3,
	UNCORE_TYPE_DF,
	UNCORE_TYPE_DF_IOD,
	UNCORE_TYPE_MAX
};

union hygon_uncore_info {
	struct {
		u64	num_iods:8;	/* number of iods of each package */
		u64	num_pmcs:8;	/* number of counters */
		u64	cid:8;		/* context id */
	} split;
	u64		full;
};

struct hygon_uncore {
	union hygon_uncore_info  __percpu *info;
	struct hygon_uncore_pmu pmu;
	bool pmu_registered;
	bool init_done;
	void (*scan)(struct hygon_uncore *uncore, unsigned int cpu);
	int  (*init)(struct hygon_uncore *uncore, unsigned int cpu);
	void (*move)(struct hygon_uncore *uncore, unsigned int cpu);
	void (*free)(struct hygon_uncore *uncore, unsigned int cpu);
};
static struct hygon_uncore uncores[];

static struct hygon_uncore_pmu *event_to_hygon_uncore_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct hygon_uncore_pmu, pmu);
}

static __always_inline bool is_uncore_df_iod_event(struct hygon_uncore_pmu *pmu)
{
	return pmu->type == UNCORE_TYPE_DF_IOD;
}

static __always_inline int hygon_uncore_ctx_num_pmcs(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info *info = per_cpu_ptr(uncore->info, cpu);

	return info->split.num_pmcs;
}

static __always_inline int hygon_uncore_ctx_cid(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info *info = per_cpu_ptr(uncore->info, cpu);

	return info->split.cid;
}

static __always_inline int hygon_uncore_ctx_num_iods(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info *info = per_cpu_ptr(uncore->info, cpu);

	return info->split.num_iods;
}

static void hygon_uncore_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, new;
	s64 delta;

	prev = local64_read(&hwc->prev_count);
	rdmsrl(hwc->event_base, new);
	local64_set(&hwc->prev_count, new);
	delta = (new << COUNTER_SHIFT) - (prev << COUNTER_SHIFT);
	delta >>= COUNTER_SHIFT;
	local64_add(delta, &event->count);
}

static void hygon_uncore_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (flags & PERF_EF_RELOAD)
		wrmsrl(hwc->event_base, (u64)local64_read(&hwc->prev_count));

	hwc->state = 0;
	wrmsrl(hwc->config_base, (hwc->config | ARCH_PERFMON_EVENTSEL_ENABLE));
	perf_event_update_userpage(event);
}

static void hygon_uncore_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		event->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int hygon_uncore_add(struct perf_event *event, int flags)
{
	struct hygon_uncore_pmu *pmu = event_to_hygon_uncore_pmu(event);
	struct hygon_uncore_ctx *ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	struct hw_perf_event *hwc = &event->hw;
	int iod_idx;
	int i;

	if (hwc->idx != -1 && ctx->events[hwc->idx] == event)
		goto out;

	for (i = 0; i < pmu->num_counters; i++) {
		if (ctx->events[i] == event) {
			hwc->idx = i;
			goto out;
		}
	}

	hwc->idx = -1;
	if (is_uncore_df_iod_event(pmu)) {
		iod_idx = event->attr.config1;
		for (i = iod_idx * NUM_COUNTERS_DF; i < (iod_idx + 1) * NUM_COUNTERS_DF; i++) {
			struct perf_event *tmp = NULL;

			if (try_cmpxchg(&ctx->events[i], &tmp, event)) {
				hwc->idx = i;
				break;
			}
		}
	} else {
		for (i = 0; i < pmu->num_counters; i++) {
			struct perf_event *tmp = NULL;

			if (try_cmpxchg(&ctx->events[i], &tmp, event)) {
				hwc->idx = i;
				break;
			}
		}
	}
out:
	if (hwc->idx == -1)
		return -EBUSY;

	hwc->config_base = pmu->msr_base + (2 * hwc->idx);
	hwc->event_base = pmu->msr_base + 1 + (2 * hwc->idx);
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		event->pmu->start(event, PERF_EF_RELOAD);

	return 0;
}

static void hygon_uncore_del(struct perf_event *event, int flags)
{
	int i;
	struct hygon_uncore_pmu *pmu = event_to_hygon_uncore_pmu(event);
	struct hygon_uncore_ctx *ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	struct hw_perf_event *hwc = &event->hw;

	event->pmu->stop(event, PERF_EF_UPDATE);
	for (i = 0; i < pmu->num_counters; i++) {
		struct perf_event *tmp = event;

		if (try_cmpxchg(&ctx->events[i], &tmp, NULL))
			break;
	}

	hwc->idx = -1;
}

static int hygon_uncore_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hygon_uncore_pmu *pmu;
	struct hygon_uncore_ctx *ctx;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (event->cpu < 0)
		return -EINVAL;

	pmu = event_to_hygon_uncore_pmu(event);
	ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	if (!ctx)
		return -ENODEV;

	event->cpu = ctx->cpu;
	hwc->config = event->attr.config;
	hwc->idx = -1;

	return 0;
}

static u64 hygon_uncore_l3_thread_slice_mask(u64 config)
{
	if (boot_cpu_data.x86_model >= 0x6 && boot_cpu_data.x86_model <= 0xf)
		return ((config & HYGON_L3_SLICE_MASK) ? : HYGON_L3_SLICE_MASK) |
		       ((config & HYGON_L3_THREAD_MASK) ? : HYGON_L3_THREAD_MASK);

	return ((config & AMD64_L3_SLICE_MASK) ? : AMD64_L3_SLICE_MASK) |
	       ((config & AMD64_L3_THREAD_MASK) ? : AMD64_L3_THREAD_MASK);
}

static int hygon_uncore_l3_event_init(struct perf_event *event)
{
	int ret = hygon_uncore_event_init(event);
	struct hw_perf_event *hwc = &event->hw;
	u64 config = event->attr.config;

	if (ret)
		return ret;

	hwc->config = config & AMD64_RAW_EVENT_MASK_NB;
	hwc->config |= hygon_uncore_l3_thread_slice_mask(config);
	return 0;
}

static int hygon_uncore_df_event_init(struct perf_event *event)
{
	int ret = hygon_uncore_event_init(event);
	struct hw_perf_event *hwc = &event->hw;
	struct hygon_uncore_pmu *pmu;
	struct hygon_uncore *uncore;
	u64 event_mask = HYGON_F18H_RAW_EVENT_MASK_DF;

	if (ret)
		return ret;

	pmu = event_to_hygon_uncore_pmu(event);
	uncore = &uncores[pmu->type];
	if (is_uncore_df_iod_event(pmu) &&
	    (event->attr.config1 >= hygon_uncore_ctx_num_iods(uncore, event->cpu)))
		return -EINVAL;

	if (boot_cpu_data.x86_model == 0x4 ||
	    boot_cpu_data.x86_model == 0x5)
		event_mask = HYGON_F18H_M4H_RAW_EVENT_MASK_DF;
	else if (boot_cpu_data.x86_model >= 0x6 &&
		 boot_cpu_data.x86_model <= 0x18)
		event_mask = HYGON_F18H_M6H_RAW_EVENT_MASK_DF;

	hwc->config = event->attr.config & event_mask;
	return 0;
}

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct pmu *ptr = dev_get_drvdata(dev);
	struct hygon_uncore_pmu *pmu = container_of(ptr, struct hygon_uncore_pmu, pmu);

	return cpumap_print_to_pagebuf(true, buf, &pmu->active_mask);
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *hygon_uncore_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group hygon_uncore_attr_group = {
	.attrs = hygon_uncore_attrs,
};

#define DEFINE_UNCORE_FORMAT_ATTR(_var, _name, _format)			\
static ssize_t __uncore_##_var##_show(struct device *dev,		\
				struct device_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct device_attribute format_attr_##_var =			\
	__ATTR(_name, 0444, __uncore_##_var##_show, NULL)

DEFINE_UNCORE_FORMAT_ATTR(event,	event,		"config:0-5");
DEFINE_UNCORE_FORMAT_ATTR(event8,	event,		"config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(umask8,	umask,		"config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(slicemask,	slicemask,	"config:48-51");	/* F18h L3 */
DEFINE_UNCORE_FORMAT_ATTR(slicemask4,	slicemask,	"config:28-31");	/* F18h M6H L3 */
DEFINE_UNCORE_FORMAT_ATTR(threadmask8,	threadmask,	"config:56-63");	/* F18h L3 */
DEFINE_UNCORE_FORMAT_ATTR(threadmask32,	threadmask,	"config:32-63");	/* F18h M6H L3 */
DEFINE_UNCORE_FORMAT_ATTR(umask10,	umask,		"config:8-17");		/* F18h M4h DF */
DEFINE_UNCORE_FORMAT_ATTR(umask12,	umask,		"config:8-19");		/* F18h M6h DF */
DEFINE_UNCORE_FORMAT_ATTR(constid,	constid,	"config:6-7,32-35,61-62");
DEFINE_UNCORE_FORMAT_ATTR(iod,		iod,		"config1:0-1");


static struct attribute *hygon_uncore_l3_format_attr[] = {
	&format_attr_event8.attr,	/* event */
	&format_attr_umask8.attr,	/* umask */
	&format_attr_slicemask.attr,		/* slicemask */
	&format_attr_threadmask8.attr,	/* threadmask */
	NULL,
};

static struct attribute *hygon_uncore_df_format_attr[] = {
	&format_attr_event.attr,	/* event */
	&format_attr_umask8.attr,	/* umask */
	&format_attr_constid.attr,	/* constid */
	NULL,
};

static struct attribute_group hygon_uncore_l3_format_group = {
	.name = "format",
	.attrs = hygon_uncore_l3_format_attr,
};

static struct attribute_group hygon_uncore_df_format_group = {
	.name = "format",
	.attrs = hygon_uncore_df_format_attr,
};

static const struct attribute_group *hygon_uncore_l3_attr_groups[] = {
	&hygon_uncore_attr_group,
	&hygon_uncore_l3_format_group,
	NULL,
};

static const struct attribute_group *hygon_uncore_df_attr_groups[] = {
	&hygon_uncore_attr_group,
	&hygon_uncore_df_format_group,
	NULL,
};

static struct attribute *hygon_uncore_df_iod_format_attr[] = {
	&format_attr_event.attr,	/* event */
	&format_attr_umask10.attr,	/* umask */
	&format_attr_constid.attr,	/* constid */
	&format_attr_iod.attr,		/* iod */
	NULL,
};

static struct attribute_group hygon_uncore_df_iod_format_group = {
	.name = "format",
	.attrs = hygon_uncore_df_iod_format_attr,
};

static const struct attribute_group *hygon_uncore_df_iod_attr_groups[] = {
	&hygon_uncore_attr_group,
	&hygon_uncore_df_iod_format_group,
	NULL,
};

static int hygon_uncore_cpu_starting(unsigned int cpu)
{
	struct hygon_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->scan(uncore, cpu);
	}

	return 0;
}

static int hygon_uncore_cpu_dead(unsigned int cpu)
{
	struct hygon_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->free(uncore, cpu);
	}

	return 0;
}

static int hygon_uncore_cpu_online(unsigned int cpu)
{
	struct hygon_uncore *uncore;
	int ret;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		ret = uncore->init(uncore, cpu);
		if (ret)
			return ret;
	}

	return 0;
}

static int hygon_uncore_cpu_down_prepare(unsigned int cpu)
{
	struct hygon_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->move(uncore, cpu);
	}

	return 0;
}

static void hygon_uncore_ctx_free(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	struct hygon_uncore_ctx *ctx;

	if (!uncore->init_done || !uncore->pmu_registered)
		return;

	if (!pmu->ctx)
		return;

	ctx = *per_cpu_ptr(pmu->ctx, cpu);
	if (!ctx)
		return;

	if (cpu == ctx->cpu)
		cpumask_clear_cpu(cpu, &pmu->active_mask);

	if (!--ctx->refcnt) {
		kfree(ctx->events);
		kfree(ctx);
	}

	*per_cpu_ptr(pmu->ctx, cpu) = NULL;
}

static void hygon_uncore_ctx_move(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	struct hygon_uncore_ctx *curr, *next;
	int i;

	if (!uncore->init_done || !uncore->pmu_registered)
		return;

	if (!pmu->ctx)
		return;

	curr = *per_cpu_ptr(pmu->ctx, cpu);
	if (!curr)
		return;

	for_each_online_cpu(i) {
		next = *per_cpu_ptr(pmu->ctx, i);
		if (!next || cpu == i)
			continue;

		if (curr == next) {
			perf_pmu_migrate_context(&pmu->pmu, cpu, i);
			cpumask_clear_cpu(cpu, &pmu->active_mask);
			cpumask_set_cpu(i, &pmu->active_mask);
			next->cpu = i;
			break;
		}
	}
}

static int hygon_uncore_ctx_init(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct hygon_uncore_ctx *curr, *prev;
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	int node, cid, i;

	if (!uncore->init_done || !uncore->pmu_registered)
		return 0;

	if (!pmu->ctx)
		return 0;

	cid = hygon_uncore_ctx_cid(uncore, cpu);
	*per_cpu_ptr(pmu->ctx, cpu) = NULL;
	curr = NULL;

	for_each_online_cpu(i) {
		if (cpu == i)
			continue;

		prev = *per_cpu_ptr(pmu->ctx, i);
		if (!prev)
			continue;
		if (cid == hygon_uncore_ctx_cid(uncore, i)) {
			curr = prev;
			break;
		}
	}

	if (!curr) {
		node = cpu_to_node(cpu);
		curr = kzalloc_node(sizeof(*curr), GFP_KERNEL, node);
		if (!curr)
			goto fail;

		curr->cpu = cpu;
		curr->events = kzalloc_node(sizeof(*curr->events) *
					    pmu->num_counters,
					    GFP_KERNEL, node);
		if (!curr->events) {
			kfree(curr);
			goto fail;
		}

		cpumask_set_cpu(cpu, &pmu->active_mask);
	}

	curr->refcnt++;
	*per_cpu_ptr(pmu->ctx, cpu) = curr;

	return 0;

fail:
	hygon_uncore_ctx_free(uncore, cpu);

	return -ENOMEM;
}

static void hygon_uncore_l3_ctx_scan(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info info = {};

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_LLC))
		return;

	info.split.num_pmcs = NUM_COUNTERS_L3;
	info.split.cid = get_llc_id(cpu);
	info.split.num_iods = 0;

	*per_cpu_ptr(uncore->info, cpu) = info;
}

static int hygon_uncore_l3_ctx_init(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	int num_counters;

	if (uncore->init_done)
		return hygon_uncore_ctx_init(uncore, cpu);

	num_counters = hygon_uncore_ctx_num_pmcs(uncore, cpu);
	if (!num_counters)
		goto done;

	strscpy(pmu->name, "hygon_l3", sizeof(pmu->name));
	pmu->num_counters = num_counters;
	pmu->msr_base = MSR_F16H_L2I_PERF_CTL;
	pmu->type = UNCORE_TYPE_L3;

	if (boot_cpu_data.x86_model >= 0x6 && boot_cpu_data.x86_model <= 0xf) {
		hygon_uncore_l3_format_attr[2] = &format_attr_slicemask4.attr;
		hygon_uncore_l3_format_attr[3] = &format_attr_threadmask32.attr;
	}

	pmu->ctx = alloc_percpu(struct hygon_uncore_ctx *);
	if (!pmu->ctx)
		goto done;

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hygon_uncore_l3_attr_groups,
		.name		= pmu->name,
		.event_init	= hygon_uncore_l3_event_init,
		.add		= hygon_uncore_add,
		.del		= hygon_uncore_del,
		.start		= hygon_uncore_start,
		.stop		= hygon_uncore_stop,
		.read		= hygon_uncore_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
		.module		= THIS_MODULE,
	};

	if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
		goto done;
	}
	uncore->pmu_registered = true;
done:
	uncore->init_done = true;
	return hygon_uncore_ctx_init(uncore, cpu);
}

static void hygon_uncore_df_ctx_scan(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info info = {};
	unsigned int eax, ebx, ecx, edx;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_NB))
		return;

	info.split.num_iods = 0;
	info.split.num_pmcs = NUM_COUNTERS_DF;
	cpuid(0x8000001e, &eax, &ebx, &ecx, &edx);
	info.split.cid = ecx & 0xff;

	*per_cpu_ptr(uncore->info, cpu) = info;
}

static
int hygon_uncore_df_ctx_init(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct attribute *df_attr;
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	int num_counters;

	if (uncore->init_done)
		return hygon_uncore_ctx_init(uncore, cpu);

	num_counters = hygon_uncore_ctx_num_pmcs(uncore, cpu);
	if (!num_counters)
		goto done;

	strscpy(pmu->name, "hygon_df", sizeof(pmu->name));
	pmu->num_counters = num_counters;
	pmu->msr_base = MSR_HYGON_F18H_DF_CTL;
	pmu->type = UNCORE_TYPE_DF;

	df_attr = &format_attr_umask8.attr;
	if (boot_cpu_data.x86_model == 0x4 ||
	    boot_cpu_data.x86_model == 0x5)
		df_attr = &format_attr_umask10.attr;
	else if (boot_cpu_data.x86_model >= 0x6 &&
		 boot_cpu_data.x86_model <= 0x18)
		df_attr = &format_attr_umask12.attr;
	hygon_uncore_df_format_attr[1] = df_attr;

	pmu->ctx = alloc_percpu(struct hygon_uncore_ctx *);
	if (!pmu->ctx)
		goto done;

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hygon_uncore_df_attr_groups,
		.name		= pmu->name,
		.event_init	= hygon_uncore_df_event_init,
		.add		= hygon_uncore_add,
		.del		= hygon_uncore_del,
		.start		= hygon_uncore_start,
		.stop		= hygon_uncore_stop,
		.read		= hygon_uncore_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
		.module		= THIS_MODULE,
	};

	if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
		goto done;
	}
	uncore->pmu_registered = true;

done:
	uncore->init_done = true;
	return hygon_uncore_ctx_init(uncore, cpu);
}

static
void hygon_uncore_df_iod_ctx_scan(struct hygon_uncore *uncore, unsigned int cpu)
{
	union hygon_uncore_info info = {};
	int num_package, num_cdds, iods_per_package;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_NB))
		return;

	if (boot_cpu_data.x86_model < 0x4 || boot_cpu_data.x86_model == 0x6)
		return;

	num_package = topology_max_packages();
	num_cdds = topology_max_die_per_package() * num_package;
	iods_per_package = (amd_nb_num() - num_cdds) / num_package;

	info.split.cid = topology_physical_package_id(cpu);
	info.split.num_iods = iods_per_package;
	info.split.num_pmcs = NUM_COUNTERS_DF * iods_per_package;

	*per_cpu_ptr(uncore->info, cpu) = info;
}

static
int hygon_uncore_df_iod_ctx_init(struct hygon_uncore *uncore, unsigned int cpu)
{
	struct hygon_uncore_pmu *pmu = &uncore->pmu;
	int num_counters;

	if (uncore->init_done)
		return hygon_uncore_ctx_init(uncore, cpu);

	num_counters = hygon_uncore_ctx_num_pmcs(uncore, cpu);
	if (!num_counters)
		goto done;

	strscpy(pmu->name, "hygon_df_iod", sizeof(pmu->name));
	pmu->num_counters = num_counters;
	pmu->msr_base = MSR_HYGON_F18H_DF_IOD_CTL;
	pmu->type = UNCORE_TYPE_DF_IOD;

	if (boot_cpu_data.x86_model >= 0x6 &&
	    boot_cpu_data.x86_model <= 0x18)
		hygon_uncore_df_iod_format_attr[1] = &format_attr_umask12.attr;

	pmu->ctx = alloc_percpu(struct hygon_uncore_ctx *);
	if (!pmu->ctx)
		goto done;

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hygon_uncore_df_iod_attr_groups,
		.name		= pmu->name,
		.event_init	= hygon_uncore_df_event_init,
		.add		= hygon_uncore_add,
		.del		= hygon_uncore_del,
		.start		= hygon_uncore_start,
		.stop		= hygon_uncore_stop,
		.read		= hygon_uncore_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
		.module		= THIS_MODULE,
	};

	if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
		goto done;
	}
	uncore->pmu_registered = true;
done:
	uncore->init_done = true;
	return hygon_uncore_ctx_init(uncore, cpu);
}


static struct hygon_uncore uncores[UNCORE_TYPE_MAX] = {
	/* HYGON L3 */
	{
		.scan = hygon_uncore_l3_ctx_scan,
		.init = hygon_uncore_l3_ctx_init,
		.move = hygon_uncore_ctx_move,
		.free = hygon_uncore_ctx_free,
	},
	/* HYGON DF */
	{
		.scan = hygon_uncore_df_ctx_scan,
		.init = hygon_uncore_df_ctx_init,
		.move = hygon_uncore_ctx_move,
		.free = hygon_uncore_ctx_free,
	},
	/* HYGON DF IOD */
	{
		.scan = hygon_uncore_df_iod_ctx_scan,
		.init = hygon_uncore_df_iod_ctx_init,
		.move = hygon_uncore_ctx_move,
		.free = hygon_uncore_ctx_free,
	}
};

static int __init hygon_uncore_init(void)
{
	struct hygon_uncore *uncore;
	int ret = -ENODEV;
	int i;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return -ENODEV;

	if (!boot_cpu_has(X86_FEATURE_TOPOEXT))
		return -ENODEV;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];

		BUG_ON(!uncore->scan);
		BUG_ON(!uncore->init);
		BUG_ON(!uncore->move);
		BUG_ON(!uncore->free);

		uncore->info = alloc_percpu(union hygon_uncore_info);
		if (!uncore->info) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	ret = cpuhp_setup_state(CPUHP_PERF_X86_HYGON_UNCORE_PREP,
				  "perf/x86/hygon/uncore:prepare",
				  NULL, hygon_uncore_cpu_dead);
	if (ret)
		goto fail;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_HYGON_UNCORE_STARTING,
				"perf/x86/hygon/uncore:starting",
				hygon_uncore_cpu_starting, NULL);
	if (ret)
		goto fail_prep;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_HYGON_UNCORE_ONLINE,
				"perf/x86/hygon/uncore:online",
				hygon_uncore_cpu_online,
				hygon_uncore_cpu_down_prepare);
	if (ret)
		goto fail_start;

	return 0;

fail_start:
	cpuhp_remove_state(CPUHP_AP_PERF_X86_HYGON_UNCORE_STARTING);
fail_prep:
	cpuhp_remove_state(CPUHP_PERF_X86_HYGON_UNCORE_PREP);
fail:
	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		if (uncore->info) {
			free_percpu(uncore->info);
			uncore->info = NULL;
		}
	}
	return ret;
}

static void __exit hygon_uncore_exit(void)
{
	struct hygon_uncore *uncore;
	struct hygon_uncore_pmu *pmu;
	int i;

	cpuhp_remove_state(CPUHP_AP_PERF_X86_HYGON_UNCORE_ONLINE);
	cpuhp_remove_state(CPUHP_AP_PERF_X86_HYGON_UNCORE_STARTING);
	cpuhp_remove_state(CPUHP_PERF_X86_HYGON_UNCORE_PREP);

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		if (!uncore->info)
			continue;

		free_percpu(uncore->info);
		uncore->info = NULL;

		if (!uncore->pmu_registered)
			continue;

		pmu = &uncore->pmu;
		if (!pmu->ctx)
			continue;

		uncore->pmu_registered = false;
		perf_pmu_unregister(&pmu->pmu);
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
	}
}
module_init(hygon_uncore_init);
module_exit(hygon_uncore_exit);

MODULE_DESCRIPTION("Hygon Uncore Driver");
MODULE_LICENSE("GPL v2");
