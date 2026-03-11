// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Lua based LSM
 *
 * Copyright (C) 2025 The Alibaba Cloud Linux Authors.
 */

#define pr_fmt(fmt)	"lua-lsm: " fmt

#include "debug.h"
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/compiler.h>
#include <linux/rwlock.h>
#include <linux/cred.h>
#include <linux/syscalls.h>	/* for __MAP */
#include <linux/timekeeping.h>	/* for ktime_get */
#include <linux/lsm_hooks.h>
#include <linux/lua.h>
#include <linux/lualib.h>
#include <linux/lauxlib.h>
#include "lsm.h"
#include "lua_object.h"
#include "lsm_defs.h"


#ifdef DEBUG

bool lua_lsm_debug = true;

static int __init disable_debug(char *str)
{
	lua_lsm_debug = false;
	return 1;
}
__setup("lua.nodebug", disable_debug);

#endif	/* ! DEBUG */

/* Flag indicating whether initialization completed */
int lua_lsm_initialized __initdata;

/********************************* lsm hook *********************************/

struct list_head lsm_modules = LIST_HEAD_INIT(lsm_modules);
static DEFINE_MUTEX(modules_mutex);
DEFINE_SRCU(modules_ss);

struct lua_lsm_hook_stat lua_lsm_hook_stats[] = {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...)			\
		{ .name = #NAME, .nhooks = ATOMIC_INIT(0), },

	#include <linux/lsm_hook_defs.h>
	#undef LSM_HOOK
	{ NULL }
};

/* LSM weak funcs */
#define LSM_HOOK(RET, DEFAULT, NAME, ...)				\
	int __weak __prepare_ ## NAME(__VA_ARGS__)			\
	{								\
		return 0;						\
	}								\
	void __weak __postpone_ ## NAME(__VA_ARGS__)			\
	{								\
	}

#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK

/********************************** stats **********************************/

/* Used by module unregister */
static atomic_t vm_nusage = ATOMIC_INIT(0);

#ifdef CONFIG_SECURITY_LUA_LSM_STATS

static atomic_t vm_nalloc = ATOMIC_INIT(0);
static atomic_t vm_nfree = ATOMIC_INIT(0);
static atomic64_t mem_nalloc = ATOMIC64_INIT(0);
static atomic64_t mem_nrealloc = ATOMIC64_INIT(0);
static atomic64_t mem_nfree = ATOMIC64_INIT(0);
static atomic_t mem_total = ATOMIC_INIT(0);
static atomic_t mem_minimum = ATOMIC_INIT(INT_MAX);
static atomic_t mem_maximum = ATOMIC_INIT(0);

static void lvm_stats_vmalloc(void)
{
	atomic_inc(&vm_nalloc);
	atomic_inc(&vm_nusage);
}

static void lvm_stats_vmfree(void)
{
	atomic_inc(&vm_nfree);
	atomic_dec(&vm_nusage);
}

static void lvm_stats_memalloc(struct lvm_state *lvm, void *ptr,
		size_t osize, size_t nsize)
{
	lua_State *L = lvm->L;
	int minimum, maximum, nbytes;

	if (nsize == 0) {
		atomic64_inc(&mem_nfree);
		atomic64_inc(&lvm->nfree);
		atomic_sub((int)osize, &mem_total);
	} else {
		if (ptr) {
			atomic64_inc(&mem_nrealloc);
			atomic64_inc(&lvm->nrealloc);
		} else {
			atomic64_inc(&mem_nalloc);
			atomic64_inc(&lvm->nalloc);
		}

		atomic_add(nsize - osize, &mem_total);
	}

	if (L == NULL)
		return;

	minimum = atomic_read(&mem_minimum);
	maximum = atomic_read(&mem_maximum);
	/*
	 * XXX Notes: There may be a deadlock risk if internal lua_lock()
	 * is actually used. The current architecture does not use locking,
	 * so lua_gc() is fine here.
	 */
	nbytes = lua_gc(L, LUA_GCCOUNT, 0) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0);
	atomic_cmpxchg(&mem_minimum, minimum, min(minimum, nbytes));
	atomic_cmpxchg(&mem_maximum, maximum, max(maximum, nbytes));
}

void lvm_stats_show(struct seq_file *m)
{
	int nusage = atomic_read(&vm_nusage);
	int total = atomic_read(&mem_total);
	int average = nusage ? total / nusage : 0;

	seq_printf(m, "lvm.nalloc\t= %12d\n", atomic_read(&vm_nalloc));
	seq_printf(m, "lvm.nfree\t= %12d\n", atomic_read(&vm_nfree));
	seq_printf(m, "lvm.nusage\t= %12d\n", nusage);
	seq_printf(m, "lmem.nalloc\t= %12lld\n", atomic64_read(&mem_nalloc));
	seq_printf(m, "lmem.nrealloc\t= %12lld\n", atomic64_read(&mem_nrealloc));
	seq_printf(m, "lmem.nfree\t= %12lld\n", atomic64_read(&mem_nfree));
	seq_printf(m, "lmem.total\t= %12d\n", total);
	seq_printf(m, "lmem.average\t= %12d\n", average);
	seq_printf(m, "lmem.minimum\t= %12d\n", atomic_read(&mem_minimum));
	seq_printf(m, "lmem.maximum\t= %12d\n", atomic_read(&mem_maximum));
}

int lsm_funcs_show(struct seq_file *m, void *v)
{
	struct lua_lsm_hook_stat *stat;
	int i = 1;

	seq_printf(m, "stats for lua-lsm (ns)\n");
	seq_printf(m, "%3s %-28s %4s %12s %15s %10s %12s\n",
		"num", "name", "nlsm", "count", "total", "average", "maxtime");
	seq_printf(m, "%s\n", TABLINE);

	for (stat = lua_lsm_hook_stats; stat->name; stat++) {
		int n = atomic_read(&stat->count);
		s64 total = atomic64_read(&stat->time);
		s64 maxtime = atomic64_read(&stat->maxtime);
		seq_printf(m, "%3d %-28s %4d %12d %15llu %10llu %12llu\n",
			i++, stat->name, atomic_read(&stat->nhooks),
			n, total, n ? total / n : 0, maxtime);
	}
	return 0;
}

#else

static inline void lvm_stats_vmalloc(void)
{
	atomic_inc(&vm_nusage);
}

static inline void lvm_stats_vmfree(void)
{
	atomic_dec(&vm_nusage);
}

static inline void lvm_stats_memalloc(struct lvm_state *lvm, void *ptr,
		size_t osize, size_t nsize)
{
}

#endif

/********************************** lvm **********************************/

static DEFINE_PER_CPU(struct lvm_state *, irq_lvms);

static int lua_state_alloc(struct lvm_state *lvm);
static void lua_state_free(struct lvm_state *lvm);

#define LVM_POOL_MAX	8

struct lvm_pool_cpu {
	struct lvm_state *head;
	unsigned int count;
	raw_spinlock_t lock;
};

static DEFINE_PER_CPU(struct lvm_pool_cpu, lvm_pools);

static void lvm_pool_init_cpu(int cpu)
{
	struct lvm_pool_cpu *pool = &per_cpu(lvm_pools, cpu);

	pool->head = NULL;
	pool->count = 0;
	raw_spin_lock_init(&pool->lock);
}

static struct lvm_state *lvm_pool_get(void)
{
	struct lvm_pool_cpu *pool;
	struct lvm_state *lvm = NULL;
	unsigned long flags;
	int cpu;

	cpu = get_cpu();
	pool = &per_cpu(lvm_pools, cpu);
	raw_spin_lock_irqsave(&pool->lock, flags);
	if (pool->head) {
		lvm = pool->head;
		pool->head = lvm->next;
		pool->count--;
	}
	raw_spin_unlock_irqrestore(&pool->lock, flags);
	put_cpu();

	if (lvm)
		lvm->next = NULL;

	return lvm;
}

static void lvm_pool_put(struct lvm_state *lvm)
{
	struct lvm_pool_cpu *pool;
	unsigned long flags;
	int cpu;

	if (!lvm)
		return;

	cpu = get_cpu();
	pool = &per_cpu(lvm_pools, cpu);
	raw_spin_lock_irqsave(&pool->lock, flags);
	if (pool->count < LVM_POOL_MAX) {
		lvm->next = pool->head;
		pool->head = lvm;
		pool->count++;
		lvm = NULL;
	}
	raw_spin_unlock_irqrestore(&pool->lock, flags);
	put_cpu();

	if (lvm) {
		lua_state_free(lvm);
		kfree(lvm);
	}
}

static void lvm_vm_reset(struct lvm_state *lvm)
{
	lua_State *L = lvm->L;
	int modules_idx;
	int keys_idx;
	int loaded_idx;
	int nkeys;
	int i;

	if (!L)
		return;

	lua_settop(L, 0);
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, CURR_ENV);

	/* Collect keys from _MODULES then clear _MODULES/_LOADED entries. */
	lua_getfield(L, LUA_REGISTRYINDEX, "_MODULES");
	if (lua_istable(L, -1)) {
		modules_idx = lua_gettop(L);
		lua_newtable(L);
		keys_idx = lua_gettop(L);
		nkeys = 0;

		lua_pushnil(L);
		while (lua_next(L, modules_idx) != 0) {
			lua_pushvalue(L, -2);
			lua_rawseti(L, keys_idx, ++nkeys);
			lua_pop(L, 1);
		}

		lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
		loaded_idx = lua_gettop(L);
		for (i = 1; i <= nkeys; i++) {
			lua_rawgeti(L, keys_idx, i);
			if (!lua_isnil(L, -1)) {
				lua_pushvalue(L, -1);
				lua_pushnil(L);
				lua_rawset(L, modules_idx);

				if (lua_istable(L, loaded_idx)) {
					lua_pushvalue(L, -1);
					lua_pushnil(L);
					lua_rawset(L, loaded_idx);
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_gc(L, LUA_GCCOLLECT, 0);
}

static lua_State *
lvm_get_from_task(const struct task_struct *task, bool exclusive)
{
	struct lua_lsm_task *llt = lua_lsm_task(task);
	struct lvm_state *lvm = llt->lvm;
	int n = refcount_acquire(&lvm->refcount);
	if (exclusive && n != 1) {
		refcount_release(&lvm->refcount);
		return NULL;
	}
	WARN_ON(n != 1);
	KASSERT(n == 1, ("<%s> Lua VM is reused, refcount = %d\n",
		task->comm, n));
	return lvm->L;
}

static void lvm_put_to_task(const struct task_struct *task, lua_State *L)
{
	struct lua_lsm_task *llt = lua_lsm_task(task);
	struct lvm_state *lvm = llt->lvm;
	int n = refcount_release(&lvm->refcount);
	KASSERT(n == 0, ("<%s> Lua VM is reused, refcount = %d\n",
		task->comm, n));
	WARN_ON(L != lvm->L);
}

lua_State *lvm_get(void)
{
	BUG_ON(in_nmi() || in_hardirq());

	if (in_task())
		return lvm_get_from_task(current, false);
	else
		return get_cpu_var(irq_lvms)->L;
}

void lvm_put(lua_State *L)
{
	if (in_task())
		lvm_put_to_task(current, L);
	else
		put_cpu_var(irq_lvms);
}

/********************************** sandbox **********************************/

static int lua_shared_index(lua_State *L)
{
	const char *name = luaL_checkstring(L, 2);
	struct lua_lsm_module_shdict *shdict, *shtmp;
	struct lua_lsm_module *module;
	int found = 0;

	__log_info_ratelimited("READ shared table, [%s] %s\n",
		luaL_typename(L, 2), lua_tostring(L, 2) ?: "(null)");

	lua_pushlightuserdata(L, MODULE_KEY);
	lua_gettable(L, LUA_ENVIRONINDEX);	/* env.MODULE_KEY */
	if (!lua_islightuserdata(L, -1)) {
		__log_err("NO fenv module\n");
		return 0;
	}
	module = lua_touserdata(L, -1);

	rcu_read_lock();
	list_for_each_entry_rcu(shdict, &module->shdicts, list) {
		if (strcmp(shdict->name, name) == 0) {
			found = 1;
			break;
		}
	}
	rcu_read_unlock();

	if (!found) {
		unsigned long flags;
		size_t l = strlen(name);
		shdict = kmalloc(struct_size(shdict, name, l + 1),
				lua_lsm_gfp());
		if (shdict == NULL) {
			__log_err("No memory\n");
			return 0;
		}
		kvcache_dict_init(&shdict->dict);
		memcpy(shdict->name, name, l);
		shdict->name[l] = '\0';

		spin_lock_irqsave(&module->shdict_lock, flags);
		list_for_each_entry(shtmp, &module->shdicts, list) {
			if (strcmp(shtmp->name, name) == 0) {
				found = 1;
				break;
			}
		}
		if (!found) {
			atomic_inc(&module->shdict_count);
			list_add_tail_rcu(&shdict->list, &module->shdicts);
		}
		spin_unlock_irqrestore(&module->shdict_lock, flags);

		if (found) {
			kvcache_dict_free(&shdict->dict);
			kfree(shdict);

			shdict = shtmp;
		}
	}

	/* shared[name] = shdict */
	lua_pushvalue(L, 2);
	*newshdict(L) = &shdict->dict;
	lua_rawset(L, 1);

	lua_settop(L, 2);
	lua_rawget(L, 1);
	return 1;
}

static int lua_shared_newindex(lua_State *L)
{
	__log_err("Invalid operation: WRITE shared table, [%s] %s - [%s] %s\n",
		luaL_typename(L, 2), lua_tostring(L, 2) ?: "(null)",
		luaL_typename(L, 3), lua_tostring(L, 3) ?: "(null)");
	return 0;
}

static int lua_module_fenv_newindex(lua_State *L)
{
	/* args: t, k, v */
	__log_warn("Warning: set global variable, [%s] %s - [%s] %s\n",
		luaL_typename(L, 2), lua_tostring(L, 2) ?: "(null)",
		luaL_typename(L, 3), lua_tostring(L, 3) ?: "(null)");

	/* XXX: t[k] = v, Warning it, but still perform assignment */
	lua_rawset(L, 1);
	return 0;
}

static int module_load(lua_State *L, struct lua_lsm_module *module)
{
	int err;

	lua_newtable(L);			/* env */
	lua_pushvalue(L, -1);
	lua_replace(L, LUA_ENVIRONINDEX);

	/* Must be after lua_replace(LUA_ENVIRONINDEX) to ensure correct env */
	lua_pushlightuserdata(L, MODULE_KEY);
	lua_pushlightuserdata(L, module);
	lua_settable(L, -3);			/* env.MODULE_KEY = module */
	*newtask_nomain(L) = current;
	lua_setfield(L, -2, "current");		/* env.current = current */

	lua_newtable(L);			/* shared table */
	lua_createtable(L, 0, 2);		/* shared metatable */
	lua_pushcfunction(L, lua_shared_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, lua_shared_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_setmetatable(L, -2);		/* setmetatable(shared, mt) */
	lua_setfield(L, -2, "shared");		/* env.shared = shared */

	/* setmetatable(env, { __index = _G, __newindex = func }) */
	lua_createtable(L, 0, 2);		/* metatable */
	lua_pushvalue(L, LUA_GLOBALSINDEX);
	lua_setfield(L, -2, "__index");		/* metatable.__index = _G */
	lua_pushcfunction(L, lua_module_fenv_newindex);
	lua_setfield(L, -2, "__newindex");	/* metatable.__newindex = func */
	lua_setmetatable(L, -2);		/* setmetatable(env, metatable) */

	lua_pushcfunction(L, lua_traceback);
	err = luaL_loadbuffer_wrap(L, module->chunk,
			module->chunk_len, module->name);
	if (err) {
		lua_pop(L, 2);
		return err;
	}

	/* stack: [env, traceback, modfunc] */
	lua_pushvalue(L, -3);
	lua_setfenv(L, -2);			/* setfenv(modfunc, env) */

	err = lua_pcall_wrap(L, 0, 1, -2);
	if (err) {
		lua_pop(L, 2);
		return err;
	}

	/* stack: [env, traceback, _M] */
	if (!lua_istable(L, -1)) {
		__log_err("module <%s> is not a table: top = %d [%s]\n",
			module->name, lua_gettop(L), luaL_typename(L, -1));
		lua_pop(L, 3);
		return -EBADF;
	}

	lua_remove(L, -2);			/* remove traceback */
	lua_remove(L, -2);			/* remove env */
	/* NO error, only _M is returned */
	return 0;
}

static int lua_modules_index(lua_State *L)
{
	const char *key = luaL_checkstring(L, 2);
	struct lua_lsm_module *module;
	int err;

	/* module queries are always run with a read lock */
	list_for_each_entry_srcu(module, &lsm_modules, list,
				srcu_read_lock_held(&modules_ss)) {
		if (strcmp(module->name, key) != 0)
			continue;

		err = module_load(L, module);
		if (err) {
			__log_err("load: %s, err = %d, top = %d\n",
				key, err, lua_gettop(L));
			return 0;
		}

		lua_insert(L, -2);
		lua_pushvalue(L, -2);
		/* stack: [table, thunk, key, thunk] */
		lua_rawset(L, 1);		/* table[key] = thunk */

		atomic_inc(&module->nloaded);
		return 1;			/* return the thunk */
	}

	__log_err("'%s' NOT found, top = %d\n", key, lua_gettop(L));
	return 0;
}

/*
 * When LuaVM is destroyed, iterate over the modules loaded in the VM
 * and update the load count in the module.
 */
static void lua_modules_free(struct task_struct *task, lua_State *L)
{
	struct lua_lsm_module *module;

	lua_getfield(L, LUA_REGISTRYINDEX, "_MODULES");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return;
	}

	list_for_each_entry_srcu(module, &lsm_modules, list,
				srcu_read_lock_held(&modules_ss)) {
		lua_pushstring(L, module->name);
		lua_rawget(L, -2);
		if (lua_istable(L, -1)) {
			atomic_dec(&module->nloaded);
			__log_info("<%s>: %d-%d freed module <%s>, nloaded = %d\n",
					task->comm, task_tgid_nr(task), task_pid_nr(task),
					module->name, atomic_read(&module->nloaded));
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

/********************************** Lua VM **********************************/

static const luaL_Reg builtinlibs[] = {
	{ "kernel",	luaopen_kernel		},
	{ "fs",		luaopen_fs		},
	{ "net",	luaopen_net		},
	{ "errno",	luaopen_errno		},
	{ "capability",	luaopen_capability	},
	{ "signal",	luaopen_signal		},
	{ NULL, NULL }
};

static void lualibs_openall(lua_State *L)
{
	const luaL_Reg *lib;

	/* TODO: loaded if needed */
	for (lib = builtinlibs; lib->func; lib++) {
		luaL_requiref(L, lib->name, lib->func, 0);
		lua_pop(L, 1);
	}
}

static int ll_require(lua_State *L)
{
	const char *modname = luaL_checkstring(L, 1);
	luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);
	lua_getfield(L, -1, modname);		/* _LOADED[modname] */
	return 1;
}

static void *lvm_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	lvm_stats_memalloc(ud, ptr, osize, nsize);

	if (nsize == 0) {
		kfree(ptr);
		return NULL;
	} else {
		return krealloc(ptr, nsize, lua_lsm_gfp());
	}
}

static int lvm_panic(lua_State *L)
{
	(void)L;	/* to avoid warnings */
	pr_err("PANIC: unprotected error in call to Lua API (%s), top = %d\n",
		lua_tostring(L, -1), lua_gettop(L));
	return 0;
}

static int lvm_pmain(lua_State *L)
{
	luaL_openlibs(L);

	/* open builtin libraries */
	lualibs_openall(L);

	/* shared dict init */
	shdict_init(L);

	lua_gc(L, LUA_GCRESTART, 0);

	/* _G._G = nil, remove global variable _G */
	lua_pushnil(L);
	lua_setfield(L, LUA_GLOBALSINDEX, "_G");

	lua_pushcfunction(L, ll_require);
	lua_setglobal(L, "require");

	/* build _MODULES table with metatable */
	lua_newtable(L);			/* _MODULES table */
	lua_createtable(L, 0, 1);		/* metatable */
	lua_pushcfunction(L, lua_modules_index); /* TODO: pass it as args */
	lua_setfield(L, -2, "__index");		/* metatable.__index = func */
	/* setmetatable(_MODULES, metatable) */
	lua_setmetatable(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, "_MODULES");

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

static int lua_state_alloc(struct lvm_state *lvm)
{
	lua_State *L;
	int status;

	L = lua_newstate(lvm_alloc, lvm);
	if (L == NULL)
		return -ENOMEM;

	lvm->L = L;
	lua_atpanic(L, lvm_panic);
	lua_gc(L, LUA_GCSTOP, 0);

	lua_pushcfunction(L, lvm_pmain);
	status = lua_pcall(L, 0, 1, 0);
	if (status != 0) {
		__log_err("pcall: status = %d, top = %d, %s\n",
			status, lua_gettop(L), lua_tostring(L, -1));
		status = -ENOEXEC;
	} else if (!lua_toboolean(L, -1) && lua_gettop(L) != 1) {
		__log_err("lvm_pmain: top = %d, stack[top] = [%s]\n",
			lua_gettop(L), luaL_typename(L, -1));
		status = -EFAULT;
	} else {
		lua_pop(L, 1);		/* pop boolean result */
		status = 0;
	}

	if (status != 0) {
		lua_close(L);
		lvm->L = NULL;
		return status;
	}

	lvm_stats_vmalloc();
	return 0;
}

static void lua_state_free(struct lvm_state *lvm)
{
	if (lvm->L) {
		lvm_stats_vmfree();
		lua_close(lvm->L);
		lvm->L = NULL;
	}
}

