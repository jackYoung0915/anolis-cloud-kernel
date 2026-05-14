/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HYGON_NB_H
#define _ASM_X86_HYGON_NB_H

#include <linux/pci.h>

struct hygon_northbridge {
	struct pci_dev *root;
	struct pci_dev *misc;
	struct pci_dev *link;
};

struct hygon_northbridge_info {
	u16 num;
	struct hygon_northbridge *nb;
};

int hygon_smn_read(u16 node, u32 address, u32 *value);
int hygon_smn_write(u16 node, u32 address, u32 value);

#ifdef CONFIG_HYGON_NB

int northbridge_init_hygon(void);
u16 hygon_nb_num(void);
struct hygon_northbridge *node_to_hygon_nb(int node);

#else

#define northbridge_init_hygon(x)	0
#define hygon_nb_num(x)	0
static inline struct hygon_northbridge *node_to_hygon_nb(int node)
{
	return NULL;
}
#endif

#endif
