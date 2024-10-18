// SPDX-License-Identifier: GPL-2.0
/*
 * CPU accounting code for task groups.
 *
 * Based on the work by Paul Menage (menage@google.com) and Balbir Singh
 * (balbir@in.ibm.com).
 */
#include <asm/irq_regs.h>
#include "sched.h"

/* Time spent by the tasks of the CPU accounting group executing in ... */
enum cpuacct_stat_index {
	CPUACCT_STAT_USER,	/* ... user mode */
	CPUACCT_STAT_SYSTEM,	/* ... kernel mode */

	CPUACCT_STAT_NSTATS,
};

static const char * const cpuacct_stat_desc[] = {
	[CPUACCT_STAT_USER] = "user",
	[CPUACCT_STAT_SYSTEM] = "system",
};

struct cpuacct_usage {
	u64	usages[CPUACCT_STAT_NSTATS];
	struct prev_cputime prev_cputime1; /* utime and stime */
	struct prev_cputime prev_cputime2; /* user and nice */
	struct prev_cputime prev_cputime3; /* sys and irq + softirq */
	struct prev_cputime prev_cputime4; /* irq and softirq */
} ____cacheline_aligned;

#ifdef CONFIG_SCHED_SLI
/* Maintain various statistics */
struct cpuacct_alistats {
	u64		nr_migrations;
	u64		steal_high;
} ____cacheline_aligned;
#endif


enum cpuacct_stat_sibidle_index {
	CPUACCT_STAT_FORCEIDLE,
	CPUACCT_STAT_SIB_REALIDLE,
	CPUACCT_STAT_SIB_IDLE,
};

struct sib_idle_cpustat {
	u64 sib_idle_stat[CPUACCT_STAT_SIB_IDLE];
};

/* track CPU usage of a group of tasks and its child groups */
struct cpuacct {
	struct cgroup_subsys_state	css;
	/* cpuusage holds pointer to a u64-type object on every CPU */
	struct cpuacct_usage __percpu	*cpuusage;
#ifdef CONFIG_SCHED_SLI
	struct cpuacct_alistats __percpu *alistats;
	struct list_head sli_list;
	bool sli_enabled;
	u64 next_load_update;
#endif
	struct kernel_cpustat __percpu	*cpustat;

	unsigned long avenrun[3];
#ifdef CONFIG_SCHED_SLI
	unsigned long avenrun_r[3];
#endif

#ifdef CONFIG_SCHED_CORE
	struct sib_idle_cpustat __percpu *sib_idle;
#endif

	CK_KABI_RESERVE(1)
	CK_KABI_RESERVE(2)
	CK_KABI_RESERVE(3)
	CK_KABI_RESERVE(4)
};

static inline struct cpuacct *cgroup_ca(struct cgroup *cgrp)
{
	return container_of(global_cgroup_css(cgrp, cpuacct_cgrp_id),
				struct cpuacct, css);
}

static inline struct cpuacct *css_ca(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct cpuacct, css) : NULL;
}

/* Return CPU accounting group to which this task belongs */
static inline struct cpuacct *task_ca(struct task_struct *tsk)
{
	return css_ca(task_css(tsk, cpuacct_cgrp_id));
}

static inline struct cpuacct *parent_ca(struct cpuacct *ca)
{
	return css_ca(ca->css.parent);
}

static DEFINE_PER_CPU(struct cpuacct_usage, root_cpuacct_cpuusage);
#ifdef CONFIG_SCHED_SLI
static DEFINE_PER_CPU(struct cpuacct_alistats, root_alistats);
#endif

static struct cpuacct root_cpuacct = {
	.cpustat	= &kernel_cpustat,
	.cpuusage	= &root_cpuacct_cpuusage,
#ifdef CONFIG_SCHED_SLI
	.alistats	= &root_alistats,
#endif
};

#ifdef CONFIG_SCHED_SLI

void task_ca_increase_nr_migrations(struct task_struct *tsk)
{
	struct cpuacct *ca;

	rcu_read_lock();
	ca = task_ca(tsk);
	if (ca)
		this_cpu_ptr(ca->alistats)->nr_migrations++;
	rcu_read_unlock();
}

#endif

#ifdef CONFIG_SCHED_SLI
static DEFINE_SPINLOCK(sli_ca_lock);
LIST_HEAD(sli_ca_list);

static void ca_enable_sli(struct cpuacct *ca, bool val)
{
	spin_lock(&sli_ca_lock);
	if (val && !READ_ONCE(ca->sli_enabled))
		list_add_tail_rcu(&ca->sli_list, &sli_ca_list);
	else if (!val && READ_ONCE(ca->sli_enabled))
		list_del_rcu(&ca->sli_list);
	WRITE_ONCE(ca->sli_enabled, val);
	spin_unlock(&sli_ca_lock);
}

void create_rich_container_reaper(struct task_struct *tsk)
{
	struct cpuacct *ca;
	struct cpuacct *parent_ca;
	struct cgroup_subsys_state *css;

	if (thread_group_leader(tsk)) {
		rcu_read_lock();
		css = task_css(tsk, cpuacct_cgrp_id);
		ca = css_ca(css);
		if (!ca || !in_rich_container(tsk, RC_LOADAVG)) {
			rcu_read_unlock();
			return;
		}

		ca_enable_sli(ca, true);
		parent_ca = css_ca(css->parent);
		if (parent_ca && parent_ca != &root_cpuacct)
			ca_enable_sli(parent_ca, true);
		rcu_read_unlock();
	}
}

static int enable_sli_write(struct cgroup_subsys_state *css,
		struct cftype *cft, u64 val)
{
	ca_enable_sli(css_ca(css), !!val);
	return 0;
}

static u64 enable_sli_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return READ_ONCE(css_ca(css)->sli_enabled);
}
#endif

/* Create a new CPU accounting group */
static struct cgroup_subsys_state *
cpuacct_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct cpuacct *ca;
	int i;

	if (!parent_css)
		return &root_cpuacct.css;

	ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	if (!ca)
		goto out;

	ca->cpuusage = alloc_percpu(struct cpuacct_usage);
	if (!ca->cpuusage)
		goto out_free_ca;

	ca->cpustat = alloc_percpu(struct kernel_cpustat);
	if (!ca->cpustat)
		goto out_free_cpuusage;

#ifdef CONFIG_SCHED_SLI
	INIT_LIST_HEAD(&ca->sli_list);

	ca->alistats = alloc_percpu(struct cpuacct_alistats);
	if (!ca->alistats)
		goto out_free_cpustat;

#endif

	for_each_possible_cpu(i) {
		prev_cputime_init(&per_cpu_ptr(ca->cpuusage, i)->prev_cputime1);
		prev_cputime_init(&per_cpu_ptr(ca->cpuusage, i)->prev_cputime2);
		prev_cputime_init(&per_cpu_ptr(ca->cpuusage, i)->prev_cputime3);
		prev_cputime_init(&per_cpu_ptr(ca->cpuusage, i)->prev_cputime4);
	}

	ca->avenrun[0] = ca->avenrun[1] = ca->avenrun[2] = 0;
#ifdef CONFIG_SCHED_SLI
	ca->avenrun_r[0] = ca->avenrun_r[1] = ca->avenrun_r[2] = 0;
#endif
	return &ca->css;

#ifdef CONFIG_SCHED_SLI
	free_percpu(ca->alistats);
out_free_cpustat:
	free_percpu(ca->cpustat);
#endif
out_free_cpuusage:
	free_percpu(ca->cpuusage);
out_free_ca:
	kfree(ca);
out:
	return ERR_PTR(-ENOMEM);
}

#ifdef CONFIG_SCHED_SLI
static void cpuacct_css_offline(struct cgroup_subsys_state *css)
{
	ca_enable_sli(css_ca(css), false);
}
#endif

/* Destroy an existing CPU accounting group */
static void cpuacct_css_free(struct cgroup_subsys_state *css)
{
	struct cpuacct *ca = css_ca(css);

	free_percpu(ca->cpustat);
	free_percpu(ca->cpuusage);
#ifdef CONFIG_SCHED_SLI
	free_percpu(ca->alistats);
#endif
	kfree(ca);
}

static u64 cpuacct_cpuusage_read(struct cpuacct *ca, int cpu,
				 enum cpuacct_stat_index index)
{
	struct cpuacct_usage *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
	u64 data;

	/*
	 * We allow index == CPUACCT_STAT_NSTATS here to read
	 * the sum of suages.
	 */
	BUG_ON(index > CPUACCT_STAT_NSTATS);

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit read safe on 32-bit platforms.
	 */
	raw_spin_rq_lock_irq(cpu_rq(cpu));
#endif

	if (index == CPUACCT_STAT_NSTATS) {
		int i = 0;

		data = 0;
		for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
			data += cpuusage->usages[i];
	} else {
		data = cpuusage->usages[index];
	}

#ifndef CONFIG_64BIT
	raw_spin_rq_unlock_irq(cpu_rq(cpu));
#endif

	return data;
}

static void cpuacct_cpuusage_write(struct cpuacct *ca, int cpu, u64 val)
{
	struct cpuacct_usage *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
	int i;

#ifndef CONFIG_64BIT
	/*
	 * Take rq->lock to make 64-bit write safe on 32-bit platforms.
	 */
	raw_spin_rq_lock_irq(cpu_rq(cpu));
#endif

	for (i = 0; i < CPUACCT_STAT_NSTATS; i++)
		cpuusage->usages[i] = val;

#ifndef CONFIG_64BIT
	raw_spin_rq_unlock_irq(cpu_rq(cpu));
#endif
}

/* Return total CPU usage (in nanoseconds) of a group */
static u64 __cpuusage_read(struct cgroup_subsys_state *css,
			   enum cpuacct_stat_index index)
{
	struct cpuacct *ca = css_ca(css);
	u64 totalcpuusage = 0;
	int i;

	for_each_possible_cpu(i)
		totalcpuusage += cpuacct_cpuusage_read(ca, i, index);

	return totalcpuusage;
}

static u64 cpuusage_user_read(struct cgroup_subsys_state *css,
			      struct cftype *cft)
{
	return __cpuusage_read(css, CPUACCT_STAT_USER);
}

static u64 cpuusage_sys_read(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	return __cpuusage_read(css, CPUACCT_STAT_SYSTEM);
}

static u64 cpuusage_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	return __cpuusage_read(css, CPUACCT_STAT_NSTATS);
}

static int cpuusage_write(struct cgroup_subsys_state *css, struct cftype *cft,
			  u64 val)
{
	struct cpuacct *ca = css_ca(css);
	int cpu;

	/*
	 * Only allow '0' here to do a reset.
	 */
	if (val)
		return -EINVAL;

	for_each_possible_cpu(cpu)
		cpuacct_cpuusage_write(ca, cpu, 0);

	return 0;
}

static int __cpuacct_percpu_seq_show(struct seq_file *m,
				     enum cpuacct_stat_index index)
{
	struct cpuacct *ca = css_ca(seq_css(m));
	u64 percpu;
	int i;

	for_each_possible_cpu(i) {
		percpu = cpuacct_cpuusage_read(ca, i, index);
		seq_printf(m, "%llu ", (unsigned long long) percpu);
	}
	seq_printf(m, "\n");
	return 0;
}

static int cpuacct_percpu_user_seq_show(struct seq_file *m, void *V)
{
	return __cpuacct_percpu_seq_show(m, CPUACCT_STAT_USER);
}

static int cpuacct_percpu_sys_seq_show(struct seq_file *m, void *V)
{
	return __cpuacct_percpu_seq_show(m, CPUACCT_STAT_SYSTEM);
}

static int cpuacct_percpu_seq_show(struct seq_file *m, void *V)
{
	return __cpuacct_percpu_seq_show(m, CPUACCT_STAT_NSTATS);
}

static int cpuacct_all_seq_show(struct seq_file *m, void *V)
{
	struct cpuacct *ca = css_ca(seq_css(m));
	int index;
	int cpu;

	seq_puts(m, "cpu");
	for (index = 0; index < CPUACCT_STAT_NSTATS; index++)
		seq_printf(m, " %s", cpuacct_stat_desc[index]);
	seq_puts(m, "\n");

	for_each_possible_cpu(cpu) {
		struct cpuacct_usage *cpuusage = per_cpu_ptr(ca->cpuusage, cpu);

		seq_printf(m, "%d", cpu);

		for (index = 0; index < CPUACCT_STAT_NSTATS; index++) {
#ifndef CONFIG_64BIT
			/*
			 * Take rq->lock to make 64-bit read safe on 32-bit
			 * platforms.
			 */
			raw_spin_rq_lock_irq(cpu_rq(cpu));
#endif

			seq_printf(m, " %llu", cpuusage->usages[index]);

#ifndef CONFIG_64BIT
			raw_spin_rq_unlock_irq(cpu_rq(cpu));
#endif
		}
		seq_puts(m, "\n");
	}
	return 0;
}

static int cpuacct_stats_show(struct seq_file *sf, void *v)
{
	struct cpuacct *ca = css_ca(seq_css(sf));
	s64 val[CPUACCT_STAT_NSTATS];
	int cpu;
	int stat;

	memset(val, 0, sizeof(val));
	for_each_possible_cpu(cpu) {
		u64 *cpustat = per_cpu_ptr(ca->cpustat, cpu)->cpustat;

		val[CPUACCT_STAT_USER]   += cpustat[CPUTIME_USER];
		val[CPUACCT_STAT_USER]   += cpustat[CPUTIME_NICE];
		val[CPUACCT_STAT_SYSTEM] += cpustat[CPUTIME_SYSTEM];
		val[CPUACCT_STAT_SYSTEM] += cpustat[CPUTIME_IRQ];
		val[CPUACCT_STAT_SYSTEM] += cpustat[CPUTIME_SOFTIRQ];
	}

	for (stat = 0; stat < CPUACCT_STAT_NSTATS; stat++) {
		seq_printf(sf, "%s %lld\n",
			   cpuacct_stat_desc[stat],
			   (long long)nsec_to_clock_t(val[stat]));
	}

	return 0;
}

#ifdef CONFIG_SCHED_SLI
#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

static unsigned long ca_running(struct cpuacct *ca, int cpu);

static void __get_cgroup_avenrun(struct cpuacct *ca, unsigned long *loads,
		unsigned long offset, int shift, bool running)
{
	unsigned long *avenrun;

	if (running)
		avenrun = ca->avenrun_r;
	else
		avenrun = ca->avenrun;

	loads[0] = (avenrun[0] + offset) << shift;
	loads[1] = (avenrun[1] + offset) << shift;
	loads[2] = (avenrun[2] + offset) << shift;
}

static inline unsigned long nr_uninterruptible(void)
{
	unsigned long i, sum = 0;

	for_each_possible_cpu(i)
		sum += cpu_rq(i)->nr_uninterruptible;

	/*
	 * Since we read the counters lockless, it might be slightly
	 * inaccurate. Do not allow it to go below zero though:
	 */
	if (unlikely((long)sum < 0))
		sum = 0;

	return sum;
}

#ifdef CONFIG_CFS_BANDWIDTH
static inline bool tg_cfs_throttled(struct task_group *tg, int cpu)
{
	return tg->cfs_rq[cpu]->throttle_count;
}
#else
static inline bool tg_cfs_throttled(struct task_group *tg, int cpu)
{
	return false;
}
#endif

#ifdef CONFIG_RT_GROUP_SCHED
static inline bool tg_rt_throttled(struct task_group *tg, int cpu)
{
	return tg->rt_rq[cpu]->rt_throttled && !tg->rt_rq[cpu]->rt_nr_boosted;
}
#endif

static unsigned long ca_running(struct cpuacct *ca, int cpu)
{
	unsigned long nr_running = 0;
	struct cgroup *cgrp = ca->css.cgroup;
	struct task_group *tg;

	/* Make sure it is only called for non-root cpuacct */
	if (ca == &root_cpuacct)
		return 0;

	rcu_read_lock();
	tg = cgroup_tg(cgrp);
	if (unlikely(!tg))
		goto out;

	if (!tg_cfs_throttled(tg, cpu))
		nr_running += tg->cfs_rq[cpu]->h_nr_running;
#ifdef CONFIG_RT_GROUP_SCHED
	if (!tg_rt_throttled(tg, cpu))
		nr_running += tg->rt_rq[cpu]->rt_nr_running;
#endif
	/* SCHED_DEADLINE doesn't support cgroup yet */

out:
	rcu_read_unlock();
	return nr_running;
}

static unsigned long ca_uninterruptible(struct cpuacct *ca, int cpu)
{
	unsigned long nr = 0;
	struct cgroup *cgrp = ca->css.cgroup;
	struct task_group *tg;

	/* Make sure it is only called for non-root cpuacct */
	if (ca == &root_cpuacct)
		return nr;

	rcu_read_lock();
	tg = cgroup_tg(cgrp);
	if (unlikely(!tg))
		goto out_rcu_unlock;

	nr = tg->cfs_rq[cpu]->nr_uninterruptible;
#ifdef CONFIG_RT_GROUP_SCHED
	nr += tg->rt_rq[cpu]->nr_uninterruptible;
#endif

out_rcu_unlock:
	rcu_read_unlock();
	return nr;
}

#ifdef CONFIG_GROUP_IDENTITY
#ifdef CONFIG_SCHED_SMT
static inline void idle_start_update_expel_sum(struct sched_entity *se)
{
	struct rq *rq;

	if (!schedstat_enabled())
		return;

	write_seqlock(&se->expel_seq);
	if (is_underclass(se)) {
		rq = se->my_q->rq;

		__schedstat_add(se->expel_sum, rq->expel_sum - schedstat_val(se->expel_start));
		if (rq_on_expel(rq))
			__schedstat_add(se->expel_sum, __rq_clock_broken(rq) -
				max(schedstat_val(se->expel_start_ts), rq->expel_start));
		__schedstat_set(se->expel_start_ts, 0);
	}
	write_sequnlock(&se->expel_seq);
}

static inline void idle_end_update_expel_sum(struct sched_entity *se)
{
	struct rq *rq;

	if (!schedstat_enabled())
		return;

	write_seqlock(&se->expel_seq);
	if (is_underclass(se)) {
		rq = se->my_q->rq;

		__schedstat_set(se->expel_start, rq->expel_sum);
		__schedstat_set(se->expel_start_ts, __rq_clock_broken(rq));
	}
	write_sequnlock(&se->expel_seq);
}

static inline u64 get_cpu_expel_sum(struct sched_entity *se, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned int se_seq, rq_seq;
	u64 cpu_expel_sum;

	do {
		se_seq = read_seqbegin(&se->expel_seq);
		cpu_expel_sum = schedstat_val(se->expel_sum);
		if (schedstat_val(se->expel_start_ts)) {
			do {
				rq_seq = read_seqcount_begin(&rq->expel_seq);
				cpu_expel_sum += rq->expel_sum -
					schedstat_val(se->expel_start);
				if (rq_on_expel(rq))
					cpu_expel_sum += __rq_clock_broken(rq) -
					max(rq->expel_start, schedstat_val(se->expel_start_ts));
			} while (read_seqcount_retry(&rq->expel_seq, rq_seq));
		}
	} while (read_seqretry(&se->expel_seq, se_seq));

	return cpu_expel_sum;
}
#else
static inline void idle_start_update_expel_sum(struct sched_entity *se) {}
static inline void idle_end_update_expel_sum(struct sched_entity *se) {}
static inline u64 get_cpu_expel_sum(struct sched_entity *se, int cpu) { return 0; }
#endif
static inline void update_steal_high(struct task_struct *tsk, u64 cputime)
{
	int cpu = tsk->cpu;
	struct rq *rq = cpu_rq(cpu);
	struct cpuacct *ca = task_ca(tsk);
	struct task_group *tg = cgroup_tg(task_ca(tsk)->css.cgroup);

	if (!schedstat_enabled())
		return;

	/* Make sure them bonded */
	if (unlikely(!tg) || tsk->sched_class != &fair_sched_class)
		return;

	if (is_underclass_task(tsk) && rq->nr_high_running)
		per_cpu_ptr(ca->alistats, cpu)->steal_high += cputime;
}
#else
static inline void idle_start_update_expel_sum(struct sched_entity *se) {}
static inline void idle_end_update_expel_sum(struct sched_entity *se) {}
static inline u64 get_cpu_expel_sum(struct sched_entity *se, int cpu) { return 0; }
static inline void update_steal_high(struct task_struct *tsk, u64 cputime) {}
#endif

void cgroup_idle_start(struct sched_entity *se)
{
	unsigned long flags;
	u64 clock;

	if (!schedstat_enabled())
		return;

	clock = __rq_clock_broken(se->cfs_rq->rq);

	write_seqcount_begin(&se->idle_seqcount);
	__schedstat_set(se->cg_idle_start, clock);
	write_seqcount_end(&se->idle_seqcount);

	spin_lock_irqsave(&se->iowait_lock, flags);
	if (schedstat_val(se->cg_nr_iowait))
		__schedstat_set(se->cg_iowait_start, clock);
	spin_unlock_irqrestore(&se->iowait_lock, flags);

	idle_start_update_expel_sum(se);
}

void cgroup_idle_end(struct sched_entity *se)
{
	unsigned long flags;
	u64 clock;
	u64 idle_start, iowait_start;

	if (!schedstat_enabled())
		return;

	clock = __rq_clock_broken(se->cfs_rq->rq);

	write_seqcount_begin(&se->idle_seqcount);
	idle_start = schedstat_val(se->cg_idle_start);
	__schedstat_add(se->cg_idle_sum, clock - idle_start);
	__schedstat_set(se->cg_idle_start, 0);
	write_seqcount_end(&se->idle_seqcount);

	spin_lock_irqsave(&se->iowait_lock, flags);
	if (schedstat_val(se->cg_nr_iowait)) {
		iowait_start = schedstat_val(se->cg_iowait_start);
		__schedstat_add(se->cg_iowait_sum, clock - iowait_start);
		__schedstat_set(se->cg_iowait_start, 0);
	}
	spin_unlock_irqrestore(&se->iowait_lock, flags);

	idle_end_update_expel_sum(se);
}

void cpuacct_cpuset_changed(struct cgroup *cgrp, struct cpumask *deleted,
		struct cpumask *added)
{
	struct rq_flags rf;
	struct rq *rq;
	struct task_group *tg;
	struct sched_entity *se;
	int cpu;

	if (!schedstat_enabled())
		return;

	rcu_read_lock();
	tg = cgroup_tg(cgrp);

	if (!tg) {
		rcu_read_unlock();
		return;
	}

	if (added) {
		/* Mark newly added cpus as newly-idle */
		for_each_cpu(cpu, added) {
			rq = cpu_rq(cpu);
			se = tg->se[cpu];
			rq_lock_irqsave(rq, &rf);
			cgroup_idle_start(se);
			rq_unlock_irqrestore(rq, &rf);
			__schedstat_add(se->cg_ineffective_sum,
				__rq_clock_broken(rq) -
					se->cg_ineffective_start);
			__schedstat_set(se->cg_ineffective_start, 0);
		}
	}

	if (deleted) {
		/* Mark ineffective_cpus as idle-invalid */
		for_each_cpu(cpu, deleted) {
			rq = cpu_rq(cpu);
			se = tg->se[cpu];
			rq_lock_irqsave(rq, &rf);
			cgroup_idle_end(se);
			rq_unlock_irqrestore(rq, &rf);
			/* Use __rq_clock_broken to avoid warning */
			__schedstat_set(se->cg_ineffective_start,
				__rq_clock_broken(rq));
		}
	}

	rcu_read_unlock();
}

static void cpuacct_calc_load(struct cpuacct *acct)
{
	long active = 0, active_r = 0, nr_r;
	int cpu;

	if (acct != &root_cpuacct) {
		for_each_possible_cpu(cpu) {
			nr_r = ca_running(acct, cpu);
			active   += nr_r;
			active_r += nr_r;
			active += ca_uninterruptible(acct, cpu);
		}
		active = active > 0 ? active * FIXED_1 : 0;
		acct->avenrun[0] = calc_load(acct->avenrun[0], EXP_1, active);
		acct->avenrun[1] = calc_load(acct->avenrun[1], EXP_5, active);
		acct->avenrun[2] = calc_load(acct->avenrun[2], EXP_15, active);

		active_r = active_r > 0 ? active_r * FIXED_1 : 0;
		acct->avenrun_r[0] = calc_load(acct->avenrun_r[0],
				EXP_1, active_r);
		acct->avenrun_r[1] = calc_load(acct->avenrun_r[1],
				EXP_5, active_r);
		acct->avenrun_r[2] = calc_load(acct->avenrun_r[2],
				EXP_15, active_r);
	} else {
		acct->avenrun[0] = avenrun[0];
		acct->avenrun[1] = avenrun[1];
		acct->avenrun[2] = avenrun[2];

		acct->avenrun_r[0] = avenrun_r[0];
		acct->avenrun_r[1] = avenrun_r[1];
		acct->avenrun_r[2] = avenrun_r[2];
	}
}

/*
 * We walk cpuacct whose SLI is enabled to perform per-cgroup load calculation
 * the overhead is acceptable if SLI is not enabled for most of the cgroups.
 */
void calc_cgroup_load(void)
{
	struct cpuacct *ca;

	rcu_read_lock();
	list_for_each_entry_rcu(ca, &sli_ca_list, sli_list)
		cpuacct_calc_load(ca);
	rcu_read_unlock();
}

static void __cpuacct_get_usage_result(struct cpuacct *ca, int cpu,
		struct task_group *tg, struct cpuacct_usage_result *res)
{
	struct kernel_cpustat *kcpustat;
	struct cpuacct_usage *cpuusage;
	struct task_cputime cputime;
	u64 tick_user, tick_nice, tick_sys, tick_irq, tick_softirq;
	u64 left, right, left2, right2;
	struct sched_entity *se;

	kcpustat = per_cpu_ptr(ca->cpustat, cpu);
	if (unlikely(!tg)) {
		memset(res, 0, sizeof(*res));
		return;
	}

	se = tg->se[cpu];
	cpuusage = per_cpu_ptr(ca->cpuusage, cpu);
	tick_user = kcpustat->cpustat[CPUTIME_USER];
	tick_nice = kcpustat->cpustat[CPUTIME_NICE];
	tick_sys = kcpustat->cpustat[CPUTIME_SYSTEM];
	tick_irq = kcpustat->cpustat[CPUTIME_IRQ];
	tick_softirq = kcpustat->cpustat[CPUTIME_SOFTIRQ];

	/* Calculate system run time */
	cputime.sum_exec_runtime = cpuusage->usages[CPUACCT_STAT_USER] +
			cpuusage->usages[CPUACCT_STAT_SYSTEM];
	cputime.utime = tick_user + tick_nice;
	cputime.stime = tick_sys + tick_irq + tick_softirq;
	cputime_adjust(&cputime, &cpuusage->prev_cputime1, &left, &right);

	/* Calculate user and nice run time */
	cputime.sum_exec_runtime = left; /* user + nice */
	cputime.utime = tick_user;
	cputime.stime = tick_nice;
	cputime_adjust(&cputime, &cpuusage->prev_cputime2, &left2, &right2);
	res->user = left2;
	res->nice = right2;

	/* Calculate sys and irq + softirq run time */
	cputime.sum_exec_runtime = right; /* sys + irq + softirq */
	cputime.utime = tick_sys;
	cputime.stime = tick_irq + tick_softirq;
	cputime_adjust(&cputime, &cpuusage->prev_cputime3, &left2, &right2);
	res->system = left2;

	/* Calculate irq and softirq run time */
	cputime.sum_exec_runtime = right2; /* irq + softirq */
	cputime.utime = tick_irq;
	cputime.stime = tick_softirq;
	cputime_adjust(&cputime, &cpuusage->prev_cputime4, &left, &right);
	res->irq = left;
	res->softirq = right;

	if (se && schedstat_enabled()) {
		unsigned int seq;
		unsigned long flags;
		u64 idle_start, ineff, ineff_start, elapse, complement;
		u64 clock, iowait_start;

		do {
			seq = read_seqcount_begin(&se->idle_seqcount);
			res->idle = schedstat_val(se->cg_idle_sum);
			idle_start = schedstat_val(se->cg_idle_start);
			clock = cpu_clock(cpu);
			if (idle_start && clock > idle_start)
				res->idle += clock - idle_start;
		} while (read_seqcount_retry(&se->idle_seqcount, seq));

		ineff = schedstat_val(se->cg_ineffective_sum);
		ineff_start = schedstat_val(se->cg_ineffective_start);
		if (ineff_start)
			__schedstat_add(ineff, clock - ineff_start);

		spin_lock_irqsave(&se->iowait_lock, flags);
		res->iowait = schedstat_val(se->cg_iowait_sum);
		iowait_start = schedstat_val(se->cg_iowait_start);
		if (iowait_start)
			__schedstat_add(res->iowait, clock - iowait_start);
		spin_unlock_irqrestore(&se->iowait_lock, flags);

		res->steal = 0;

		elapse = clock - schedstat_val(se->cg_init_time);
		complement = res->idle + se->sum_exec_runtime + ineff;
		if (elapse > complement)
			res->steal = elapse - complement;

		res->idle -= res->iowait;
	} else {
		res->idle = res->iowait = res->steal = 0;
	}

	res->guest = kcpustat->cpustat[CPUTIME_GUEST];
	res->guest_nice = kcpustat->cpustat[CPUTIME_GUEST_NICE];
}

static int cpuacct_proc_stats_show(struct seq_file *sf, void *v)
{
	struct cpuacct *ca = css_ca(seq_css(sf));
	struct cgroup *cgrp = seq_css(sf)->cgroup;
	u64 user, nice, system, idle, iowait, irq, softirq, steal, guest;
	u64 nr_migrations = 0;
	struct cpuacct_alistats *alistats;
	unsigned long load, avnrun[3], avnrun_r[3];
	unsigned long nr_run = 0, nr_uninter = 0;
	int cpu;

	user = nice = system = idle = iowait =
		irq = softirq = steal = guest = 0;

	if (ca != &root_cpuacct) {
		struct cpuacct_usage_result res;

		for_each_possible_cpu(cpu) {
			if (!housekeeping_cpu(cpu, HK_FLAG_DOMAIN))
				continue;

			rcu_read_lock();
			__cpuacct_get_usage_result(ca, cpu,
					cgroup_tg(cgrp), &res);
			rcu_read_unlock();

			user += res.user;
			nice += res.nice;
			system += res.system;
			irq += res.irq;
			softirq += res.softirq;
			steal += res.steal;
			guest += res.guest;
			guest += res.guest_nice;
			iowait += res.iowait;
			idle += res.idle;

			alistats = per_cpu_ptr(ca->alistats, cpu);
			nr_migrations += alistats->nr_migrations;
			nr_run += ca_running(ca, cpu);
			nr_uninter += ca_uninterruptible(ca, cpu);
		}

		__get_cgroup_avenrun(ca, avnrun, FIXED_1/200, 0, false);
		__get_cgroup_avenrun(ca, avnrun_r, FIXED_1/200, 0, true);
	} else {
		struct kernel_cpustat *kcpustat;

		for_each_possible_cpu(cpu) {
			kcpustat = per_cpu_ptr(ca->cpustat, cpu);
			user += kcpustat->cpustat[CPUTIME_USER];
			nice += kcpustat->cpustat[CPUTIME_NICE];
			system += kcpustat->cpustat[CPUTIME_SYSTEM];
			irq += kcpustat->cpustat[CPUTIME_IRQ];
			softirq += kcpustat->cpustat[CPUTIME_SOFTIRQ];
			guest += kcpustat->cpustat[CPUTIME_GUEST];
			guest += kcpustat->cpustat[CPUTIME_GUEST_NICE];
			idle += get_idle_time(kcpustat, cpu);
			iowait += get_iowait_time(kcpustat, cpu);
			steal += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
			alistats = per_cpu_ptr(ca->alistats, cpu);
			nr_migrations += alistats->nr_migrations;
		}

		nr_run = nr_running();
		nr_uninter = nr_uninterruptible();

		get_avenrun(avnrun, FIXED_1/200, 0);
		get_avenrun_r(avnrun_r, FIXED_1/200, 0);
	}

	seq_printf(sf, "user %lld\n", nsec_to_clock_t(user));
	seq_printf(sf, "nice %lld\n", nsec_to_clock_t(nice));
	seq_printf(sf, "system %lld\n", nsec_to_clock_t(system));
	seq_printf(sf, "idle %lld\n", nsec_to_clock_t(idle));
	seq_printf(sf, "iowait %lld\n", nsec_to_clock_t(iowait));
	seq_printf(sf, "irq %lld\n", nsec_to_clock_t(irq));
	seq_printf(sf, "softirq %lld\n", nsec_to_clock_t(softirq));
	seq_printf(sf, "steal %lld\n", nsec_to_clock_t(steal));
	seq_printf(sf, "guest %lld\n", nsec_to_clock_t(guest));

	load = LOAD_INT(avnrun[0]) * 100 + LOAD_FRAC(avnrun[0]);
	seq_printf(sf, "load average(1min) %lld\n", (u64)load);
	load = LOAD_INT(avnrun[1]) * 100 + LOAD_FRAC(avnrun[1]);
	seq_printf(sf, "load average(5min) %lld\n", (u64)load);
	load = LOAD_INT(avnrun[2]) * 100 + LOAD_FRAC(avnrun[2]);
	seq_printf(sf, "load average(15min) %lld\n", (u64)load);

	seq_printf(sf, "nr_running %lld\n", (u64)nr_run);
	if ((long) nr_uninter < 0)
		nr_uninter = 0;
	seq_printf(sf, "nr_uninterruptible %lld\n", (u64)nr_uninter);
	seq_printf(sf, "nr_migrations %lld\n", (u64)nr_migrations);

	load = LOAD_INT(avnrun_r[0]) * 100 + LOAD_FRAC(avnrun_r[0]);
	seq_printf(sf, "running load average(1min) %lld\n", (u64)load);
	load = LOAD_INT(avnrun_r[1]) * 100 + LOAD_FRAC(avnrun_r[1]);
	seq_printf(sf, "running load average(5min) %lld\n", (u64)load);
	load = LOAD_INT(avnrun_r[2]) * 100 + LOAD_FRAC(avnrun_r[2]);
	seq_printf(sf, "running load average(15min) %lld\n", (u64)load);

	return 0;
}

static int cpuacct_sched_cfs_show(struct seq_file *sf, void *v)
{
	struct cgroup *cgrp = seq_css(sf)->cgroup;
	struct cpuacct *ca;
	struct task_group *tg = cgroup_tg(cgrp);
	struct sched_entity *se;
	int cpu;
	u64 wait_max = 0, wait_sum = 0, wait_sum_other = 0, wait_sum_fi = 0, exec_sum = 0;
	u64 expel_sum = 0, steal_high = 0;
	u64 wait_sum_self = 0;

	if (!schedstat_enabled())
		goto out_show;

	rcu_read_lock();
	tg = cgroup_tg(cgrp);
	if (unlikely(!tg)) {
		WARN_ONCE(1, "cgroup \"cpu,cpuacct\" are not bound together");
		goto rcu_unlock_show;
	}

	ca = cgroup_ca(cgrp);

	for_each_online_cpu(cpu) {
		se = tg->se[cpu];
		if (!se)
			continue;
		exec_sum += schedstat_val(se->sum_exec_runtime);
		wait_sum_other +=
			schedstat_val(se->statistics.parent_wait_contrib);
#ifdef CONFIG_SCHED_CORE
		wait_sum_fi += schedstat_val(se->statistics.forceidled_sum);
#endif
		wait_sum += schedstat_val(se->statistics.wait_sum);
		wait_max =
			max(wait_max, schedstat_val(se->statistics.wait_max));
		expel_sum += get_cpu_expel_sum(se, cpu);
		steal_high += per_cpu_ptr(ca->alistats, cpu)->steal_high;
		wait_sum_self += schedstat_val(se->statistics.wait_self);
	}
rcu_unlock_show:
	rcu_read_unlock();
out_show:
	/*
	 * [Serve time] [On CPU time] [Queue other time]
	 * [Queue sibling time] [Queue max time] [Force idled time]
	 * [Queue self time]
	 */
	seq_printf(sf, "%lld %lld %lld %lld %lld %lld %lld\n",
			exec_sum + wait_sum, exec_sum, wait_sum_other,
			wait_sum - wait_sum_other - wait_sum_fi, wait_max, wait_sum_fi,
			wait_sum_self);
	seq_printf(sf, "%lld %lld %lld %lld\n",
		steal_high, 0llu, expel_sum, 0llu);

	return 0;
}
#else
static inline void update_steal_high(struct task_struct *tsk, u64 cputime) {}
#endif

static struct cftype files[] = {
	{
		.name = "usage",
		.read_u64 = cpuusage_read,
		.write_u64 = cpuusage_write,
	},
	{
		.name = "usage_user",
		.read_u64 = cpuusage_user_read,
	},
	{
		.name = "usage_sys",
		.read_u64 = cpuusage_sys_read,
	},
	{
		.name = "usage_percpu",
		.seq_show = cpuacct_percpu_seq_show,
	},
	{
		.name = "usage_percpu_user",
		.seq_show = cpuacct_percpu_user_seq_show,
	},
	{
		.name = "usage_percpu_sys",
		.seq_show = cpuacct_percpu_sys_seq_show,
	},
	{
		.name = "usage_all",
		.seq_show = cpuacct_all_seq_show,
	},
	{
		.name = "stat",
		.seq_show = cpuacct_stats_show,
	},
#ifdef CONFIG_SCHED_SLI
	{
		.name = "proc_stat",
		.seq_show = cpuacct_proc_stats_show,
	},
	{
		.name = "enable_sli",
		.read_u64 = enable_sli_read,
		.write_u64 = enable_sli_write
	},
	{
		.name = "sched_cfs_statistics",
		.seq_show = cpuacct_sched_cfs_show,
	},
	{
		.name = "wait_latency",
		.private = SCHED_LAT_WAIT,
		.write_u64 = sched_lat_stat_write,
		.seq_show = sched_lat_stat_show
	},
	{
		.name = "cgroup_wait_latency",
		.private = SCHED_LAT_CGROUP_WAIT,
		.write_u64 = sched_lat_stat_write,
		.seq_show = sched_lat_stat_show
	},
	{
		.name = "block_latency",
		.private = SCHED_LAT_BLOCK,
		.write_u64 = sched_lat_stat_write,
		.seq_show = sched_lat_stat_show
	},
	{
		.name = "ioblock_latency",
		.private = SCHED_LAT_IOBLOCK,
		.write_u64 = sched_lat_stat_write,
		.seq_show = sched_lat_stat_show
	},
#endif
	{ }	/* terminate */
};

/*
 * charge this task's execution time to its accounting group.
 *
 * called with rq->lock held.
 */
void cpuacct_charge(struct task_struct *tsk, u64 cputime)
{
	struct cpuacct *ca;
	int index = CPUACCT_STAT_SYSTEM;
	struct pt_regs *regs = get_irq_regs() ? : task_pt_regs(tsk);

	if (regs && user_mode(regs))
		index = CPUACCT_STAT_USER;

	rcu_read_lock();

	for (ca = task_ca(tsk); ca; ca = parent_ca(ca))
		__this_cpu_add(ca->cpuusage->usages[index], cputime);

	update_steal_high(tsk, cputime);

	rcu_read_unlock();
}

/*
 * Add user/system time to cpuacct.
 *
 * Note: it's the caller that updates the account of the root cgroup.
 */
void cpuacct_account_field(struct task_struct *tsk, int index, u64 val)
{
	struct cpuacct *ca;

	rcu_read_lock();
	for (ca = task_ca(tsk); ca != &root_cpuacct; ca = parent_ca(ca))
		__this_cpu_add(ca->cpustat->cpustat[index], val);
	rcu_read_unlock();
}

static void cpuacct_cgroup_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;

	cgroup_taskset_for_each(task, css, tset)
		if (task->pid && is_child_reaper(task_pid(task)))
			create_rich_container_reaper(task);
}

struct cgroup_subsys cpuacct_cgrp_subsys = {
	.css_alloc	= cpuacct_css_alloc,
	.css_free	= cpuacct_css_free,
#ifdef CONFIG_SCHED_SLI
	.css_offline	= cpuacct_css_offline,
#endif
	.attach		= cpuacct_cgroup_attach,
	.legacy_cftypes	= files,
	.early_init	= true,
};

#ifdef CONFIG_SCHED_SLI
static DEFINE_STATIC_KEY_FALSE(async_load_calc);

bool async_load_calc_enabled(void)
{
	return static_branch_likely(&async_load_calc);
}

static int async_load_calc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", async_load_calc_enabled());
	return 0;
}

static int async_load_calc_open(struct inode *inode, struct file *file)
{
	return single_open(file, async_load_calc_show, NULL);
}

static void async_calc_cgroup_load(void)
{
	int cnt;
	struct cpuacct *ca;

again:
	cnt = 1;
	rcu_read_lock();
	list_for_each_entry_rcu(ca, &sli_ca_list, sli_list) {
		unsigned long next_update = ca->next_load_update;

		/*
		 * Need per ca check since after break the list
		 * could have been changed, otherwise the loop
		 * will be endless.
		 */
		if (time_before(jiffies, next_update + 10))
			continue;

		cpuacct_calc_load(ca);
		ca->next_load_update = jiffies + LOAD_FREQ;

		/* Take a break for every 100 ca */
		if (cnt++ >= 100) {
			rcu_read_unlock();
			cond_resched();
			goto again;
		}
	}
	rcu_read_unlock();
}

int load_calc_func(void *unsed)
{
	unsigned long next_update = jiffies + LOAD_FREQ;

	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ/5);
		set_current_state(TASK_RUNNING);

		if (time_before(jiffies, next_update + 10))
			continue;

		async_calc_cgroup_load();
		next_update += LOAD_FREQ;
	}

	return 0;
}

static struct task_struct *load_calc_p;

static int mod_async_load_calc(bool enable)
{
	if (enable == async_load_calc_enabled())
		return 0;

	if (enable) {
		load_calc_p = kthread_create(load_calc_func, NULL, "load_calc");
		if (!load_calc_p)
			return -ENOMEM;

		wake_up_process(load_calc_p);
		static_branch_enable(&async_load_calc);
	} else {
		kthread_stop(load_calc_p);
		load_calc_p = NULL;

		static_branch_disable(&async_load_calc);
	}

	return 0;
}

static DEFINE_MUTEX(load_calc_mutex);

static ssize_t async_load_calc_write(struct file *file,
		const char __user *ubuf,
		size_t count, loff_t *ppos)
{
	char val = -1;
	int ret = 0;

	if (count < 1 || *ppos) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(&val, ubuf, 1)) {
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&load_calc_mutex);

	switch (val) {
	case '0':
		ret = mod_async_load_calc(false);
		break;
	case '1':
		ret = mod_async_load_calc(true);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&load_calc_mutex);
out:
	return ret ? ret : count;
}

static const struct proc_ops async_load_calc_opt = {
	.proc_open	= async_load_calc_open,
	.proc_read	= seq_read,
	.proc_write	= async_load_calc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static void async_load_calc_init(void)
{
	if (!proc_create("async_load_calc", 0600, NULL,
				&async_load_calc_opt)) {
		pr_err("Failed to register async_load_calc interface\n");
		return;
	}

	if (mod_async_load_calc(true))
		pr_err("Failed to enable async_load_calc\n");
}
late_initcall_sync(async_load_calc_init);
#endif

#ifdef CONFIG_PSI

static bool psi_v1_enable;
static int __init setup_psi_v1(char *str)
{
	return kstrtobool(str, &psi_v1_enable) == 0;
}
__setup("psi_v1=", setup_psi_v1);

static int __init cgroup_v1_psi_init(void)
{
	if (!psi_v1_enable) {
		static_branch_enable(&psi_v1_disabled);
		return 0;
	}

	cgroup_add_legacy_cftypes(&cpuacct_cgrp_subsys, cgroup_v1_psi_files);
	return 0;
}

late_initcall_sync(cgroup_v1_psi_init);
#endif

#ifdef CONFIG_RICH_CONTAINER

#ifndef CONFIG_RICH_CONTAINER_CG_SWITCH
/* 0 - cpu quota; 1 - cpuset.cpus; 2 - cpu.shares */
int sysctl_rich_container_cpuinfo_source;
/* when cpu.shares */
unsigned int sysctl_rich_container_cpuinfo_sharesbase = 1024;

static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

static inline struct task_group *task_tg(struct task_struct *tsk)
{
	return css_tg(task_css(tsk, cpu_cgrp_id));
}

void rich_container_get_cpus(struct task_struct *tsk, struct cpumask *pmask)
{
	struct task_group *tg;
	int i, cpus;

	/* cfs quota source */
	if (sysctl_rich_container_cpuinfo_source == 0) {
		long quota, period;

		rcu_read_lock();
		tg = task_tg(tsk);
		if (sysctl_rich_container_source == 2 && tg->parent)
			tg = tg->parent;
		quota = tg_get_cfs_quota(tg);
		period = tg_get_cfs_period(tg);
		rcu_read_unlock();

		if (quota == -1) {
			/* Fallback to use cpuset.cpus if quota not set */
			goto cpuset_source;
		} else {
			/* period can't be 0 */
			cpus = (quota + period - 1) / period;
			cpus = clamp(cpus, 1, (int)num_online_cpus());
			cpumask_clear(pmask);
			for (i = 0; i < cpus; i++)
				cpumask_set_cpu(i, pmask);
		}

		return;
	}

	/* cpu.shares source */
	if (sysctl_rich_container_cpuinfo_source == 2) {
		unsigned long shares;

		rcu_read_lock();
		tg = task_tg(tsk);
		if (sysctl_rich_container_source == 2 && tg->parent)
			tg = tg->parent;
		shares = scale_load_down(tg->shares);
		rcu_read_unlock();

		/* sysctl_rich_container_cpuinfo_sharesbase can't be 0 */
		cpus = (shares + sysctl_rich_container_cpuinfo_sharesbase - 1) /
			sysctl_rich_container_cpuinfo_sharesbase;
		cpus = clamp(cpus, 1, (int)num_online_cpus());
		cpumask_clear(pmask);
		for (i = 0; i < cpus; i++)
			cpumask_set_cpu(i, pmask);

		return;
	}

cpuset_source:
	/* cpuset.cpus source */
	rich_container_get_cpuset_cpus(pmask);
}
#endif /*CONFIG_RICH_CONTAINER_CG_SWITCH */

bool child_cpuacct(struct task_struct *tsk)
{
	struct cpuacct *ca = task_ca(tsk);

	if (ca && ca != &root_cpuacct)
		return true;

	return false;
}


bool check_rich_container(unsigned int cpu, unsigned int *index,
		bool *rich_container, unsigned int *total)
{
	struct cpumask cpuset_allowed;
	struct task_struct __maybe_unused *scenario;
	bool in_rich;
	int i, id = 0;

	rcu_read_lock();
	in_rich = in_rich_container(current, RC_CPUINFO);
	rcu_read_unlock();
	if (!in_rich)
		return false;

	*rich_container = true;

#ifdef CONFIG_RICH_CONTAINER_CG_SWITCH
	rich_container_get_cpuset_cpus(&cpuset_allowed);
#else
	read_lock(&tasklist_lock);
	scenario = rich_container_get_scenario();
	get_task_struct(scenario);
	read_unlock(&tasklist_lock);
	rich_container_get_cpus(scenario, &cpuset_allowed);
	put_task_struct(scenario);
#endif

	*total = cpumask_weight(&cpuset_allowed);
	if (cpumask_test_cpu(cpu, &cpuset_allowed)) {
		for_each_cpu(i, &cpuset_allowed) {
			if (i == cpu)
				break;
			id++;
		}
		*index = id;
		return false;
	}

	/* Hide this cpu in the container */
	return true;
}

#ifdef CONFIG_RICH_CONTAINER_CG_SWITCH
void rich_container_source(enum rich_container_source *from)
{
	struct cgroup_subsys_state *css;

	rcu_read_lock();
	css = task_css(current, cpuacct_cgrp_id);
	while (css) {
		if (test_bit(CGRP_RICH_CONTAINER_SOURCE, &css->cgroup->flags))
			break;
		css = css->parent;
	}

	if (css)
		*from = RICH_CONTAINER_CSS;
	else
		*from = RICH_CONTAINER_REAPER;
	rcu_read_unlock();
}
#else
void rich_container_source(enum rich_container_source *from)
{
	if (sysctl_rich_container_source == 1)
		*from = RICH_CONTAINER_REAPER;
	else if (sysctl_rich_container_source == 0)
		*from = RICH_CONTAINER_CURRENT;
	else
		*from = RICH_CONTAINER_PARENT_CGROUP;
}
#endif

void rich_container_get_usage(enum rich_container_source from,
		struct task_struct *reaper, int cpu,
		struct cpuacct_usage_result *res)
{
	struct cgroup_subsys_state *css;
	struct cpuacct *ca_src;
	struct task_group *tg;

	rcu_read_lock();
	/* To avoid iterating css for every cpu */
	if (likely(from == RICH_CONTAINER_REAPER)) {
		ca_src = task_ca(reaper);
		goto ok;
	} else if (from == RICH_CONTAINER_CURRENT) {
		ca_src = task_ca(current);
		goto ok;
	} else if (from == RICH_CONTAINER_PARENT_CGROUP) {
		css = task_css(current, cpuacct_cgrp_id)->parent;
		if (!css)
			ca_src = task_ca(current);
		else
			ca_src = css_ca(css);
		goto ok;
	}

	css = task_css(current, cpuacct_cgrp_id);
	while (css) {
		if (test_bit(CGRP_RICH_CONTAINER_SOURCE, &css->cgroup->flags))
			break;
		css = css->parent;
	}

	if (css)
		ca_src = css_ca(css);
	else
		ca_src = task_ca(reaper);

ok:
	tg = cgroup_tg(ca_src->css.cgroup);
	__cpuacct_get_usage_result(ca_src, cpu, tg, res);
	rcu_read_unlock();
}

unsigned long rich_container_get_running(enum rich_container_source from,
		struct task_struct *reaper, int cpu)
{
	struct cgroup_subsys_state *css;
	struct cpuacct *ca_src;
	unsigned long nr;

	rcu_read_lock();
	/* To avoid iterating css for every cpu */
	if (likely(from == RICH_CONTAINER_REAPER)) {
		ca_src = task_ca(reaper);
		goto ok;
	} else if (from == RICH_CONTAINER_CURRENT) {
		ca_src = task_ca(current);
		goto ok;
	} else if (from == RICH_CONTAINER_PARENT_CGROUP) {
		css = task_css(current, cpuacct_cgrp_id)->parent;
		if (!css)
			ca_src = task_ca(current);
		else
			ca_src = css_ca(css);
		goto ok;
	}

	css = task_css(current, cpuacct_cgrp_id);
	while (css) {
		if (test_bit(CGRP_RICH_CONTAINER_SOURCE, &css->cgroup->flags))
			break;
		css = css->parent;
	}

	if (css)
		ca_src = css_ca(css);
	else
		ca_src = task_ca(reaper);

ok:
	nr = ca_running(ca_src, cpu);
	rcu_read_unlock();

	return nr;
}

void rich_container_get_avenrun(enum rich_container_source from,
		struct task_struct *reaper, unsigned long *loads,
		unsigned long offset, int shift, bool running)
{
	struct cgroup_subsys_state *css;
	struct cpuacct *ca_src;

	rcu_read_lock();
	/* To avoid iterating css for every cpu */
	if (likely(from == RICH_CONTAINER_REAPER)) {
		ca_src = task_ca(reaper);
		goto ok;
	} else if (from == RICH_CONTAINER_CURRENT) {
		ca_src = task_ca(current);
		goto ok;
	} else if (from == RICH_CONTAINER_PARENT_CGROUP) {
		css = task_css(current, cpuacct_cgrp_id)->parent;
		if (!css)
			ca_src = task_ca(current);
		else
			ca_src = css_ca(css);
		goto ok;
	}

	css = task_css(current, cpuacct_cgrp_id);
	while (css) {
		if (test_bit(CGRP_RICH_CONTAINER_SOURCE, &css->cgroup->flags))
			break;
		css = css->parent;
	}

	if (css)
		ca_src = css_ca(css);
	else
		ca_src = task_ca(reaper);

ok:
	__get_cgroup_avenrun(ca_src, loads, offset, shift, running);
	rcu_read_unlock();
}

#endif
