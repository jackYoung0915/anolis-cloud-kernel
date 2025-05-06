/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _VKERNEL_UTILS_H
#define _VKERNEL_UTILS_H

int vk_kallsyms_init(void);
void vk_kallsyms_uninit(void);

unsigned long lookup_name(const char *name);

#endif
