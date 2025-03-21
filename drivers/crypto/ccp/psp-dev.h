/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Platform Security Processor (PSP) interface driver
 *
 * Copyright (C) 2017-2019 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#ifndef __PSP_DEV_H__
#define __PSP_DEV_H__

#include <linux/device.h>
#include <linux/list.h>
#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>

#include "sp-dev.h"

#define PSP_CMDRESP_RESP		BIT(31)
#define PSP_CMDRESP_ERR_MASK		0xffff

#define PSP_RBCTL_X86_WRITES		BIT(31)
#define PSP_RBCTL_RBMODE_ACT		BIT(30)
#define PSP_RBCTL_CLR_INTSTAT		BIT(29)
#define PSP_RBTAIL_QHI_TAIL_SHIFT	16
#define PSP_RBTAIL_QHI_TAIL_MASK	0x7FF0000
#define PSP_RBTAIL_QLO_TAIL_MASK	0x7FF

#define PSP_RBHEAD_QHI_HEAD_SHIFT	16
#define PSP_RBHEAD_QHI_HEAD_MASK	0x7FF0000
#define PSP_RBHEAD_QLO_HEAD_MASK	0x7FF

#define PSP_RBHEAD_QPAUSE_INT_STAT	BIT(30)

#define MAX_PSP_NAME_LEN		16

#ifdef CONFIG_HYGON_PSP2CPU_CMD
#define PSP_X86_CMD			BIT(2)
#define P2C_NOTIFIERS_MAX		16
#endif

#define PSP_CMD_RING_BUFFER		0x304

#define PSP_MUTEX_TIMEOUT 600000
struct psp_mutex {
	uint64_t locked;
};
struct psp_dev_data {
	struct psp_mutex mb_mutex;
};
struct psp_misc_dev {
	struct kref refcount;
	struct psp_dev_data *data_pg_aligned;
	struct miscdevice dev_misc;
	struct miscdevice resource2_misc;
};

extern struct psp_device *psp_master;

typedef void (*psp_irq_handler_t)(int, void *, unsigned int);

struct psp_device {
	struct list_head entry;

	struct psp_vdata *vdata;
	char name[MAX_PSP_NAME_LEN];

	struct device *dev;
	struct sp_device *sp;

	void __iomem *io_regs;

	psp_irq_handler_t sev_irq_handler;
	void *sev_irq_data;

	psp_irq_handler_t tee_irq_handler;
	void *tee_irq_data;

	void *sev_data;
	void *tee_data;
};

#define PSP_DO_CMD_OP_PHYADDR	BIT(0)   // Input data as physical address
#define PSP_DO_CMD_OP_NOWAIT	BIT(1)   // No need to wait ioc
int psp_do_cmd_locked(int cmd, void *data, int *psp_ret, uint32_t op);

void psp_set_sev_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data);
void psp_clear_sev_irq_handler(struct psp_device *psp);

void psp_set_tee_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data);
void psp_clear_tee_irq_handler(struct psp_device *psp);

struct psp_device *psp_get_master_device(void);

int psp_mutex_lock_timeout(struct psp_mutex *mutex, uint64_t ms);

int psp_mutex_trylock(struct psp_mutex *mutex);

int psp_mutex_unlock(struct psp_mutex *mutex);

#endif /* __PSP_DEV_H */
