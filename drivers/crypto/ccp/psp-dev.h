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

#include "psp-ringbuf.h"
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
extern uint8_t psp_legacy_rb_supported;		// support legacy ringbuffer
extern uint8_t psp_rb_oc_supported;		// support overcommit
extern uint8_t psp_generic_rb_supported;	// support generic ringbuffer

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

struct tkm_cmdresp_head {
	uint32_t buf_size; //including this header
	uint32_t cmdresp_size; //including this header
	uint32_t cmdresp_code;
} __packed;

struct tkm_device_info {
	uint32_t api_version;
	uint32_t fw_version;
	uint32_t kek_sm4_total;
	uint32_t isk_sm2_sign_total;
	uint32_t isk_sm2_enc_total;
	uint8_t chip_id[32];
} __packed;
struct tkm_cmdresp_device_info_get {
	struct tkm_cmdresp_head head;
	struct tkm_device_info dev_info;
} __packed;

struct queue_info {
	uint32_t head;   /* In|Out */
	uint32_t tail;   /* In */
	uint32_t mask;   /* In */
	uint64_t cmdptr_address;     /* In */
	uint64_t statval_address;    /* In */
	uint8_t  reserved[36];
} __packed;	// total 64 bytes
struct psp_ringbuffer_cmd_buf {
	struct queue_info high;
	struct queue_info low;
	uint8_t reserved[128];
} __packed;	// total 256 bytes

#define PSP_RB_IS_SUPPORTED(buildid)		(buildid >= 1913 && boot_cpu_has(X86_FEATURE_SEV))
#define PSP_RB_OC_IS_SUPPORTED(buildid)		(buildid >= 2167)
#define PSP_GRB_IS_SUPPORTED(buildid)		(buildid >= 2270)
#define PSP_CMD_STATUS_RUNNING			0xffff
#define PSP_RB_OVERCOMMIT_SIZE			1024
#define TKM_DEVICE_INFO_GET			0x1001

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

/**
 * When PSP_DO_CMD_OP_NOWAIT is used with psp_do_cmd_locked,
 * psp_worker_register_notify must be called first to register async notify
 * for PSP worker bottom-half execution. Note: triggering worker bottom-half
 * always clears previous notify.
 */
void psp_worker_register_notify(work_func_t notify);

/**
 * psp generic ringbuffer implement.
 **/
uint32_t psp_ringbuffer_enqueue(struct csv_ringbuffer_queue *ringbuffer,
				uint32_t cmd, phys_addr_t phy_addr, uint16_t flags);
void psp_ringbuffer_dequeue(struct csv_ringbuffer_queue *ringbuffer,
				struct csv_cmdptr_entry *cmdptr, struct csv_statval_entry *statval,
				uint32_t num);
int psp_ringbuffer_queue_init(struct csv_ringbuffer_queue *ring_buffer);
void psp_ringbuffer_queue_free(struct csv_ringbuffer_queue *ring_buffer);
int psp_ringbuffer_get_newhead(uint32_t *hi_head, uint32_t *low_head);
void psp_ringbuffer_check_support(void);

/**
 * psp_do_ringbuffer_cmds_locked is a no-wait PSP operation.
 * Must register notify via psp_worker_register_notify before use.
 */
int psp_do_ringbuffer_cmds_locked(struct csv_ringbuffer_queue *ring_buffer, int *psp_ret);

#endif /* __PSP_DEV_H */
