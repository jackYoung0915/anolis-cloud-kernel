// SPDX-License-Identifier: GPL-2.0
/*
 * Group Balancer
 *
 * Group Balancer sched domains define and build
 * Copyright (C) 2024 Alibaba Group, Inc., Cruz Zhao <CruzZhao@linux.alibaba.com>
 */
#include "sched.h"
#include <linux/log2.h>

struct group_balancer_sched_domain {
	struct group_balancer_sched_domain		*parent;
	struct list_head				child;
	struct list_head				sibling;
	struct list_head				topology_level_sibling;
	struct list_head				size_level_sibling;
	unsigned long					gb_flags;
	char						*topology_name;
	unsigned int					span_weight;
	unsigned int					nr_children;
	unsigned long					span[];
};

/* The topology that group balancer cares about. */
enum GROUP_BALANCER_TOPOLOGY {
	GROUP_BALANCER_ROOT,
	GROUP_BALANCER_SOCKET,
#ifdef CONFIG_NUMA
	GROUP_BALANCER_NUMA,
#endif
	GROUP_BALANCER_DIE,
	GROUP_BALANCER_LLC,
#ifdef CONFIG_SCHED_MC
	GROUP_BALANCER_MC,
#endif
#ifdef CONFIG_SCHED_CLUSTER
	GROUP_BALANCER_CLUSTER,
#endif
#ifdef CONFIG_SCHED_SMT
	GROUP_BALANCER_SMT,
#endif
	NR_GROUP_BALANCER_TOPOLOGY,
};

enum GROUP_BALANCER_TOPOLOGY_FLAGS {
	GROUP_BALANCER_ROOT_FLAG	= BIT(GROUP_BALANCER_ROOT),
	GROUP_BALANCER_SOCKET_FLAG	= BIT(GROUP_BALANCER_SOCKET),
#ifdef CONFIG_NUMA
	GROUP_BALANCER_NUMA_FLAG	= BIT(GROUP_BALANCER_NUMA),
#endif
	GROUP_BALANCER_DIE_FLAG		= BIT(GROUP_BALANCER_DIE),
	GROUP_BALANCER_LLC_FLAG		= BIT(GROUP_BALANCER_LLC),
#ifdef CONFIG_SCHED_MC
	GROUP_BALANCER_MC_FLAG		= BIT(GROUP_BALANCER_MC),
#endif
#ifdef CONFIG_SCHED_CLUSTER
	GROUP_BALANCER_CLUSTER_FLAG	= BIT(GROUP_BALANCER_CLUSTER),
#endif
#ifdef CONFIG_SCHED_SMT
	GROUP_BALANCER_SMT_FLAG		= BIT(GROUP_BALANCER_SMT),
#endif
};

struct group_balancer_topology_level {
	sched_domain_mask_f	mask;
	sched_domain_flags_f	sd_flags;
	unsigned long		gb_flags;
	char			*topology_name;
	struct list_head	domains;
	bool			skip;
};

struct group_balancer_size_level {
	int			size;
	/* Use list temporarily, we will change to use rb_tree later.*/
	struct list_head	domains;
};

LIST_HEAD(group_balancer_sched_domains);

DEFINE_RWLOCK(group_balancer_sched_domain_lock);

struct cpumask root_cpumask;

const struct cpumask *cpu_llc_mask(int cpu)
{
	struct sched_domain *llc = rcu_dereference(per_cpu(sd_llc, cpu));

	return (const struct cpumask *)to_cpumask(llc->span);
}

const struct cpumask *cpu_die_mask(int cpu)
{
	return topology_die_cpumask(cpu);
}

const struct cpumask *cpu_core_mask(int cpu)
{
	return topology_core_cpumask(cpu);
}

const struct cpumask *cpu_root_mask(int cpu)
{
	return (const struct cpumask *)&root_cpumask;
}

#define GB_SD_INIT(type) \
	.gb_flags = GROUP_BALANCER_##type##_FLAG, \
	.topology_name = #type
/*
 * Group Balancer build group_balancer_sched_domains after kernel init,
 * so the following cpumask can be got safely.
 *
 * smt mask:		cpu_smt_mask
 * cluster mask:	cpu_clustergroup_mask
 * mc mask:		cpu_coregroup_mask
 * llc mask:		cpu_llc_mask
 * die mask:		cpu_die_mask
 * numa mask:		cpu_cpu_mask
 * socket mask:		cpu_core_mask
 * all mask:		cpu_root_mask
 */
static struct group_balancer_topology_level default_topology[] = {
	{ cpu_root_mask, GB_SD_INIT(ROOT) },
	{ cpu_core_mask, GB_SD_INIT(SOCKET) },
#ifdef CONFIG_NUMA
	{ cpu_cpu_mask, GB_SD_INIT(NUMA) },
#endif
	{ cpu_die_mask, GB_SD_INIT(DIE) },
	{ cpu_llc_mask, GB_SD_INIT(LLC) },
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, cpu_core_flags, GB_SD_INIT(MC) },
#endif
#ifdef CONFIG_SCHED_CLUSTER
	{ cpu_clustergroup_mask, cpu_cluster_flags, GB_SD_INIT(CLUSTER) },
#endif
#ifdef CONFIG_SCHED_SMT
	{ cpu_smt_mask, cpu_smt_flags, GB_SD_INIT(SMT) },
#endif
	{ NULL, },
};

#define for_each_gb_topology_level(tl)			\
	for (tl = default_topology; tl->mask; tl++)

#define for_each_topology_level_sibling(pos, gb_tl)	\
	list_for_each_entry(pos, &gb_tl->domains, topology_level_sibling)

#define for_each_topology_level_sibling_safe(pos, n, gb_tl)	\
	list_for_each_entry_safe(pos, n, &gb_tl->domains, topology_level_sibling)

/* NR_CPUS is 1024 now, we set log(1024) + 1 = 11 levels. */
#define NR_SIZE_LEVELS 11
struct group_balancer_size_level default_size[NR_SIZE_LEVELS];

#define for_each_gb_size_level(sl, i)			\
	for (sl = default_size, i = 0; i < NR_SIZE_LEVELS; sl++, i++)

#define for_each_gb_sd_child(pos, gb_sd)			\
	list_for_each_entry(pos, &gb_sd->child, sibling)

#define for_each_gb_sd_child_safe(pos, n, gb_sd)			\
	list_for_each_entry_safe(pos, n, &gb_sd->child, sibling)

#define group_balancer_sched_domain_first_child(gb_sd)		\
	list_first_entry(&gb_sd->child, struct group_balancer_sched_domain, sibling)

struct group_balancer_sched_domain *group_balancer_root_domain;

static inline struct cpumask *gb_sd_span(struct group_balancer_sched_domain *gb_sd)
{
	return to_cpumask(gb_sd->span);
}

static unsigned int get_size_level(struct group_balancer_sched_domain *gb_sd)
{
	int size_level = ilog2(gb_sd->span_weight);

	/* Prevent out-of-bound array access. */
	if (unlikely(size_level < 0))
		size_level = 0;
	else if (unlikely(size_level >= NR_SIZE_LEVELS))
		size_level = NR_SIZE_LEVELS - 1;

	return (unsigned int)size_level;
}

static void add_to_size_level(struct group_balancer_sched_domain *gb_sd)
{
	unsigned int size_level = get_size_level(gb_sd);
	struct group_balancer_size_level *gb_sl;

	gb_sl = &default_size[size_level];

	list_add_tail(&gb_sd->size_level_sibling, &gb_sl->domains);
}

static inline struct group_balancer_sched_domain
*alloc_init_group_balancer_sched_domain(void)
{
	struct group_balancer_sched_domain *new;

	new = kzalloc(sizeof(struct group_balancer_sched_domain) + cpumask_size(), GFP_KERNEL);
	if (!new)
		return NULL;
	INIT_LIST_HEAD(&new->child);
	INIT_LIST_HEAD(&new->sibling);
	INIT_LIST_HEAD(&new->topology_level_sibling);
	INIT_LIST_HEAD(&new->size_level_sibling);

	return new;
}

static void add_to_tree(struct group_balancer_sched_domain *gb_sd,
			struct group_balancer_sched_domain *parent)
{
	int cpu;
	struct rq *rq;

	if (parent) {
		list_add_tail(&gb_sd->sibling, &parent->child);
		gb_sd->parent = parent;
		parent->nr_children++;
	}
	gb_sd->span_weight = cpumask_weight(gb_sd_span(gb_sd));
	add_to_size_level(gb_sd);

	if (!gb_sd->nr_children) {
		for_each_cpu(cpu, gb_sd_span(gb_sd)) {
			rq = cpu_rq(cpu);
			rq->gb_sd = gb_sd;
		}
	}
}

static void free_group_balancer_sched_domain(struct group_balancer_sched_domain *gb_sd)
{
	int cpu;
	struct rq *rq;

	if (gb_sd->parent)
		gb_sd->parent->nr_children--;

	list_del(&gb_sd->sibling);
	list_del(&gb_sd->topology_level_sibling);
	list_del(&gb_sd->size_level_sibling);

	if (!gb_sd->nr_children) {
		for_each_cpu(cpu, gb_sd_span(gb_sd)) {
			rq = cpu_rq(cpu);
			rq->gb_sd = gb_sd->parent;
		}
	}

	kfree(gb_sd);
}

/* free group balancer sched domain tree from the leaf nodes. */
static void free_group_balancer_sched_domains(void)
{
	struct group_balancer_sched_domain *parent, *child, *ancestor, *n;

	parent = group_balancer_root_domain;
down:
	for_each_gb_sd_child_safe(child, n, parent) {
		parent = child;
		goto down;
up:
		continue;
	}

	ancestor = parent->parent;
	/* root domain should always be in memory. */
	if (parent != group_balancer_root_domain && !parent->nr_children) {
		n = list_next_entry(parent, sibling);
		free_group_balancer_sched_domain(parent);
	}

	child = n;
	parent = ancestor;
	if (parent)
		goto up;
}

static void move_group_balancer_sched_domain(struct group_balancer_sched_domain *child,
					     struct group_balancer_sched_domain *new_parent,
					     bool *is_first_child)
{
	if (*is_first_child) {
		*is_first_child = false;
		new_parent->topology_name = child->topology_name;
		new_parent->gb_flags = child->gb_flags;
	}
	cpumask_or(gb_sd_span(new_parent), gb_sd_span(child), gb_sd_span(new_parent));
	list_del(&child->sibling);
	child->parent->nr_children--;
	list_add_tail(&child->sibling, &new_parent->child);
	new_parent->nr_children++;
	child->parent = new_parent;
}

static int bi_divide_group_balancer_sched_domain(struct group_balancer_sched_domain *gb_sd)
{
	unsigned int weight = gb_sd->span_weight;
	unsigned int half = (weight + 1) / 2;
	unsigned int logn = ilog2(half);
	/*
	 * Find the power of 2 closest to half, and use this number
	 * to split weight into two parts, left and right, and keep
	 * left always the smaller one.
	 */
	unsigned int left = (half - (1 << logn) < (1 << (logn + 1)) - half) ?
			    1 << logn : weight - (1 << (logn + 1));
	bool is_first_child = true;
	struct group_balancer_sched_domain *child, *n;
	struct group_balancer_sched_domain *left_middle, *right_middle;

	/*
	 * If a domain has more than two children, we add a middle level.
	 * For example, if a domain spans 48 cpus and 24 children, we add
	 * a middle level first, which contains two children who span 16
	 * and 32 cpus. And we will divide the new children in the next
	 * loop.
	 *
	 * As for the size of middle level dividing, we choose powers of
	 * two instead of half of span_weight, to make the division of
	 * lower levels simpler.
	 */
	if (gb_sd->nr_children > 2) {
		left_middle = alloc_init_group_balancer_sched_domain();
		if (!left_middle) {
			free_group_balancer_sched_domains();
			return -ENOMEM;
		}

		right_middle = alloc_init_group_balancer_sched_domain();
		if (!right_middle) {
			free_group_balancer_sched_domain(left_middle);
			free_group_balancer_sched_domains();
			return -ENOMEM;
		}

		for_each_gb_sd_child_safe(child, n, gb_sd) {
			/*
			 * Consider the following case, a domain spans 6
			 * cpus and 3 chidlren(each child spans 2 cpus),
			 * we just need to add right middle which spans 4
			 * cpus.
			 */
			move_group_balancer_sched_domain(child, left_middle,
							 &is_first_child);
			if (cpumask_weight(gb_sd_span(left_middle)) >= left)
				break;
		}

		/*
		 * As left is always the smaller one, it is possible that
		 * left has only one child, if so, we delete the child.
		 */
		if (left_middle->nr_children == 1) {
			child = group_balancer_sched_domain_first_child(left_middle);
			free_group_balancer_sched_domain(child);
		}

		is_first_child = true;
		for_each_gb_sd_child_safe(child, n, gb_sd)
			move_group_balancer_sched_domain(child, right_middle,
							 &is_first_child);

		add_to_tree(left_middle, gb_sd);
		add_to_tree(right_middle, gb_sd);
	}

	return 0;
}

/* DFS to bi-divide group balancer sched domains. */
static int bi_divide_group_balancer_sched_domains(void)
{
	struct group_balancer_sched_domain *parent, *child;
	int ret = 0;

	/*
	 * Traverse all the domains from the group_balancer_sched_domains list,
	 * and add the new domains to the tail of the list, to ensure that all
	 * the domains will be traversed.
	 */
	parent = group_balancer_root_domain;
down:
	ret = bi_divide_group_balancer_sched_domain(parent);
	if (ret)
		goto out;
	for_each_gb_sd_child(child, parent) {
		parent = child;
		goto down;
up:
		continue;
	}
	if (parent == group_balancer_root_domain)
		goto out;

	child = parent;
	parent = parent->parent;
	if (parent)
		goto up;
out:
	return ret;
}

static int build_group_balancer_root_domain(void)
{
	struct group_balancer_sched_domain *root;

	root = alloc_init_group_balancer_sched_domain();
	if (!root) {
		pr_err("Group Balancer: Failed to alloc group_balancer root domain.\n");
		return -ENOMEM;
	}
	cpumask_copy(gb_sd_span(root), &root_cpumask);
	list_add_tail(&root->topology_level_sibling, &default_topology[0].domains);
	add_to_tree(root, NULL);
	group_balancer_root_domain = root;

	return 0;
}

/* BFS to build group balancer sched domain tree. */
static int build_group_balancer_sched_domains(void)
{
	int cpu;
	int ret;
	cpumask_var_t trial_cpumask, child_cpumask;
	struct group_balancer_topology_level *gb_tl, *next_gb_tl;
	struct group_balancer_sched_domain *parent, *n;

	/*
	 * The group balancer sched domain is a tree.
	 * If the root was not built on boot, build the root node first.
	 */
	if (unlikely(!group_balancer_root_domain)) {
		ret = build_group_balancer_root_domain();
		if (ret)
			goto err_out;
	}

	if (!zalloc_cpumask_var(&trial_cpumask, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_out;
	}
	if (!zalloc_cpumask_var(&child_cpumask, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto err_free_trial_cpumask;
	}

	/* Build the tree by level. */
	for_each_gb_topology_level(gb_tl) {
		if (gb_tl->skip)
			continue;
		next_gb_tl = gb_tl + 1;
		while (next_gb_tl->skip && next_gb_tl->mask)
			next_gb_tl++;
		if (!next_gb_tl->mask)
			break;
		/* Build children from parent level. */
		for_each_topology_level_sibling_safe(parent, n, gb_tl) {
			/*
			 * If the cpumasks of the adjacent topology levels are the same,
			 * we move the domain to the next level, to make the loop
			 * continue.
			 */
			cpu = cpumask_first(gb_sd_span(parent));
			cpumask_and(child_cpumask, &root_cpumask, next_gb_tl->mask(cpu));
			if (cpumask_equal(gb_sd_span(parent), child_cpumask)) {
				list_del(&parent->topology_level_sibling);
				list_add_tail(&parent->topology_level_sibling,
					 &next_gb_tl->domains);
				parent->gb_flags &= next_gb_tl->gb_flags;
				continue;
			}
			cpumask_copy(trial_cpumask, gb_sd_span(parent));
			for_each_cpu(cpu, trial_cpumask) {
				struct group_balancer_sched_domain *child;

				cpumask_and(child_cpumask, &root_cpumask, next_gb_tl->mask(cpu));
				cpumask_andnot(trial_cpumask, trial_cpumask, child_cpumask);
				child = alloc_init_group_balancer_sched_domain();
				if (!child) {
					ret = -ENOMEM;
					goto err_free_domains;
				}
				cpumask_copy(gb_sd_span(child), child_cpumask);
				child->topology_name = next_gb_tl->topology_name;
				list_add_tail(&child->topology_level_sibling, &next_gb_tl->domains);
				child->gb_flags &= next_gb_tl->gb_flags;
				add_to_tree(child, parent);
			}
		}
	}

	free_cpumask_var(child_cpumask);
	free_cpumask_var(trial_cpumask);
	return bi_divide_group_balancer_sched_domains();

err_free_domains:
	free_group_balancer_sched_domains();
	free_cpumask_var(child_cpumask);
err_free_trial_cpumask:
	free_cpumask_var(trial_cpumask);
err_out:
	return ret;
}

void sched_init_group_balancer_levels(void)
{
	struct group_balancer_topology_level *tl;
	struct group_balancer_size_level *sl;
	int i;

	for_each_gb_topology_level(tl)
		INIT_LIST_HEAD(&tl->domains);

	for_each_gb_size_level(sl, i) {
		sl->size = 1<<i;
		INIT_LIST_HEAD(&sl->domains);
	}
}

/*
 * Here are some cases that some topologies are not reported correctly,
 * e.g., on some virtual machines, DIE cpumask is incorrect, which only
 * includes one cpu.
 * To avoid building incorrect group balancer sched domains due to this
 * kind of incorrect topology, we check whether the topology is correct,
 * and if not, we mark it should be skipped.
 */
static void validate_topology_levels(void)
{
	struct group_balancer_topology_level *gb_tl, *next_gb_tl;
	int i;

	for (i = 1; i < NR_GROUP_BALANCER_TOPOLOGY - 1; i++) {
		gb_tl = &default_topology[i];
		next_gb_tl = &default_topology[i + 1];
		if (!next_gb_tl->mask)
			break;
		if (!cpumask_subset(next_gb_tl->mask(0), gb_tl->mask(0)) ||
		    (cpumask_weight(gb_tl->mask(0)) <= 1))
			gb_tl->skip = true;
	}
}

void sched_init_group_balancer_sched_domains(void)
{
	int ret;

	cpus_read_lock();
	write_lock(&group_balancer_sched_domain_lock);
	ret = build_group_balancer_sched_domains();
	if (ret)
		pr_err("Group Balancer: Failed to build group balancer sched domains: %d\n", ret);
	else
		pr_info("Group Balancer: Build group balancer sched domains successfully.\n");
	write_unlock(&group_balancer_sched_domain_lock);
	cpus_read_unlock();
}

void sched_clear_group_balancer_sched_domains(void)
{
	cpus_read_lock();
	write_lock(&group_balancer_sched_domain_lock);
	free_group_balancer_sched_domains();
	pr_info("Group Balancer: Free group balancer sched domains.\n");
	write_unlock(&group_balancer_sched_domain_lock);
	cpus_read_unlock();
}

static int __init group_balancer_init(void)
{
	cpumask_copy(&root_cpumask, cpu_online_mask);
	sched_init_group_balancer_levels();
	validate_topology_levels();
	return build_group_balancer_root_domain();
}

late_initcall(group_balancer_init);
