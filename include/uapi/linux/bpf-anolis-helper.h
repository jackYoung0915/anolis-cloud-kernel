/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_BPF_ANOLIS_HELPER_H__
#define _UAPI__LINUX_BPF_ANOLIS_HELPER_H__

#ifdef __KERNEL__
#include <linux/limits.h>

/* Start of Anolis BPF helper function descriptions:
 *
 * WARNING: These helpers are NOT stable and may change at any time.
 *
 * long bpf_anolis_ipv6_addr_set(struct bpf_sock_addr *ctx, struct in6_addr *addr, size_t addr_len)
 *	Description
 *		Set IPv6 address including family. The flowinfo and scope_id
 *		are set as 0. This helper is used only for
 *		*BPF_CGROUP_INET4_GETPEERNAME** to return IPv6 address.
 *		*addr_len* is the length of the input addr.
 *	Returns
 *		0 on success, or a negative error in case of failure.
 *
 * long bpf_anolis_relay_write(void *data, u64 size, u64 id)
 *	Description
 *		Copy *size* bytes from *data* into bpf relay files, which is
 *		only used by relay-bpf. *id* indicates the relay buffer id
 *		to write into, which can be queried by
 *		`cat /sys/kernel/debug/relay_ebpf`
 *	Return
 *		0 on success, or a negative error in case of failure.
 *
 * struct rb_node * bpf_anolis_rb_next(const struct rb_node *node)
 *	Description
 *		Find next rb node in a tree.
 *	Returns
 *		Pointer to the next rb_node. NULL if reach the end.
 */

#define ___ANOLIS_BPF_FUNC_MAPPER(FN)		\
	FN(anolis_ipv6_addr_set)		\
	FN(anolis_relay_write)			\
	FN(anolis_rb_next)			\

#define __BPF_ENUM_FN(x) BPF_FUNC_ ## x,
enum anolis_bpf_func_id {
	__ANOLIS_BPF_FUNC_MIN_ID = INT_MAX - 1024,
	___ANOLIS_BPF_FUNC_MAPPER(__BPF_ENUM_FN)
	__ANOLIS_BPF_FUNC_MAX_ID,
};
#undef __BPF_ENUM_FN

#endif /* __KERNEL__ */

#endif /* _UAPI__LINUX_BPF_ANOLIS_HELPER_H__ */
