// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Platform Security Processor (PSP) interface
 *
 * Copyright (C) 2016,2019 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#include <linux/kernel.h>
#include <linux/irqreturn.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/rwlock.h>

#include "sp-dev.h"
#include "psp-dev.h"
#include "sev-dev.h"
#include "tee-dev.h"
#include "vpsp.h"
#ifdef CONFIG_TDM_DEV_HYGON
#include "tdm-dev.h"
#endif

struct psp_device *psp_master;

struct psp_misc_dev *psp_misc;
int is_hygon_psp;

#define HYGON_PSP_IOC_TYPE 'H'
enum HYGON_PSP_OPCODE {
	HYGON_PSP_MUTEX_ENABLE = 1,
	HYGON_PSP_MUTEX_DISABLE,
	HYGON_VPSP_CTRL_OPT,
	HYGON_PSP_OP_PIN_USER_PAGE,
	HYGON_PSP_OP_UNPIN_USER_PAGE,
	HYGON_PSP_OPCODE_MAX_NR,
};

int psp_mutex_enabled;
extern struct mutex sev_cmd_mutex;

uint64_t atomic64_exchange(uint64_t *dst, uint64_t val)
{
	return xchg(dst, val);
}

int psp_mutex_init(struct psp_mutex *mutex)
{
	if (!mutex)
		return -1;
	mutex->locked = 0;
	return 0;
}

int psp_mutex_trylock(struct psp_mutex *mutex)
{
	if (atomic64_exchange(&mutex->locked, 1))
		return 0;
	else
		return 1;
}

int psp_mutex_lock_timeout(struct psp_mutex *mutex, uint64_t ms)
{
	int ret = 0;
	unsigned long je;

	je = jiffies + msecs_to_jiffies(ms);
	do {
		if (psp_mutex_trylock(mutex)) {
			ret = 1;
			break;
		}
	} while ((ms == 0) || time_before(jiffies, je));

	return ret;
}

int psp_mutex_unlock(struct psp_mutex *mutex)
{
	if (!mutex)
		return -1;

	atomic64_exchange(&mutex->locked, 0);
	return 0;
}

static struct psp_device *psp_alloc_struct(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	struct psp_device *psp;

	psp = devm_kzalloc(dev, sizeof(*psp), GFP_KERNEL);
	if (!psp)
		return NULL;

	psp->dev = dev;
	psp->sp = sp;

	snprintf(psp->name, sizeof(psp->name), "psp-%u", sp->ord);

	return psp;
}

static irqreturn_t psp_irq_handler(int irq, void *data)
{
	struct psp_device *psp = data;
	unsigned int status;

	/* Read the interrupt status: */
	status = ioread32(psp->io_regs + psp->vdata->intsts_reg);

	/* Clear the interrupt status by writing the same value we read. */
	iowrite32(status, psp->io_regs + psp->vdata->intsts_reg);

	/* invoke subdevice interrupt handlers */
	if (status) {
		if (psp->sev_irq_handler)
			psp->sev_irq_handler(irq, psp->sev_irq_data, status);

		if (psp->tee_irq_handler)
			psp->tee_irq_handler(irq, psp->tee_irq_data, status);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_HYGON_PSP2CPU_CMD
static DEFINE_SPINLOCK(p2c_notifier_lock);
static p2c_notifier_t p2c_notifiers[P2C_NOTIFIERS_MAX] = {NULL};
int psp_register_cmd_notifier(uint32_t cmd_id, int (*notifier)(uint32_t id, uint64_t data))
{
	int ret = -ENODEV;
	unsigned long flags;

	spin_lock_irqsave(&p2c_notifier_lock, flags);
	if (cmd_id < P2C_NOTIFIERS_MAX && !p2c_notifiers[cmd_id]) {
		p2c_notifiers[cmd_id] = notifier;
		ret = 0;
	}
	spin_unlock_irqrestore(&p2c_notifier_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(psp_register_cmd_notifier);

int psp_unregister_cmd_notifier(uint32_t cmd_id, int (*notifier)(uint32_t id, uint64_t data))
{
	int ret = -ENODEV;
	unsigned long flags;

	spin_lock_irqsave(&p2c_notifier_lock, flags);
	if (cmd_id < P2C_NOTIFIERS_MAX && p2c_notifiers[cmd_id] == notifier) {
		p2c_notifiers[cmd_id] = NULL;
		ret = 0;
	}
	spin_unlock_irqrestore(&p2c_notifier_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(psp_unregister_cmd_notifier);

#define PSP2CPU_MAX_LOOP		100
static irqreturn_t psp_irq_handler_hygon(int irq, void *data)
{
	struct psp_device *psp = data;
	struct sev_device *sev = psp->sev_irq_data;
	unsigned int status;
	int reg;
	unsigned long flags;
	int count = 0;
	uint32_t p2c_cmd;
	uint32_t p2c_lo_data;
	uint32_t p2c_hi_data;
	uint64_t p2c_data;

	/* Read the interrupt status: */
	status = ioread32(psp->io_regs + psp->vdata->intsts_reg);

	while (status && (count++ < PSP2CPU_MAX_LOOP)) {
		/* Clear the interrupt status by writing the same value we read. */
		iowrite32(status, psp->io_regs + psp->vdata->intsts_reg);

		/* Check if it is command completion: */
		if (status & SEV_CMD_COMPLETE) {
			/* Check if it is SEV command completion: */
			reg = ioread32(psp->io_regs + psp->vdata->sev->cmdresp_reg);
			if (reg & PSP_CMDRESP_RESP) {
				sev->int_rcvd = 1;
				wake_up(&sev->int_queue);
			}
		}

		if (status & PSP_X86_CMD) {
			/* Check if it is P2C command completion: */
			reg = ioread32(psp->io_regs + psp->vdata->p2c_cmdresp_reg);
			if (!(reg & PSP_CMDRESP_RESP)) {
				p2c_lo_data = ioread32(psp->io_regs +
						psp->vdata->p2c_cmdbuff_addr_lo_reg);
				p2c_hi_data = ioread32(psp->io_regs +
						psp->vdata->p2c_cmdbuff_addr_hi_reg);
				p2c_data = (((uint64_t)(p2c_hi_data) << 32) +
						((uint64_t)(p2c_lo_data)));
				p2c_cmd = (uint32_t)(reg & SEV_CMDRESP_IOC);
				if (p2c_cmd < P2C_NOTIFIERS_MAX) {
					spin_lock_irqsave(&p2c_notifier_lock, flags);
					if (p2c_notifiers[p2c_cmd])
						p2c_notifiers[p2c_cmd](p2c_cmd, p2c_data);

					spin_unlock_irqrestore(&p2c_notifier_lock, flags);
				}

				reg |= PSP_CMDRESP_RESP;
				iowrite32(reg, psp->io_regs + psp->vdata->p2c_cmdresp_reg);
			}
		}
		status = ioread32(psp->io_regs + psp->vdata->intsts_reg);
	}

	return IRQ_HANDLED;
}
#endif

static unsigned int psp_get_capability(struct psp_device *psp)
{
	unsigned int val = ioread32(psp->io_regs + psp->vdata->feature_reg);

	/*
	 * Check for a access to the registers.  If this read returns
	 * 0xffffffff, it's likely that the system is running a broken
	 * BIOS which disallows access to the device. Stop here and
	 * fail the PSP initialization (but not the load, as the CCP
	 * could get properly initialized).
	 */
	if (val == 0xffffffff) {
		dev_notice(psp->dev, "psp: unable to access the device: you might be running a broken BIOS.\n");
		return 0;
	}

	return val;
}

static int psp_check_sev_support(struct psp_device *psp,
				 unsigned int capability)
{
	/* Check if device supports SEV feature */
	if (!(capability & 1)) {
		dev_dbg(psp->dev, "psp does not support SEV\n");
		return -ENODEV;
	}

	return 0;
}

static int psp_check_tee_support(struct psp_device *psp,
				 unsigned int capability)
{
	/* Check if device supports TEE feature */
	if (!(capability & 2) ||
	    (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)) {
		dev_dbg(psp->dev, "psp does not support TEE\n");
		return -ENODEV;
	}

	return 0;
}

static int psp_check_support(struct psp_device *psp,
			     unsigned int capability)
{
	int sev_support = psp_check_sev_support(psp, capability);
	int tee_support = psp_check_tee_support(psp, capability);

	/* Return error if device neither supports SEV nor TEE */
	if (sev_support && tee_support)
		return -ENODEV;

	return 0;
}

static int psp_init(struct psp_device *psp, unsigned int capability)
{
	int ret;

	if (!psp_check_sev_support(psp, capability)) {
		ret = sev_dev_init(psp);
		if (ret)
			return ret;
	}

	if ((boot_cpu_data.x86_vendor != X86_VENDOR_HYGON) &&
	    !psp_check_tee_support(psp, capability)) {
		ret = tee_dev_init(psp);
		if (ret)
			return ret;
	}

#ifdef CONFIG_TDM_DEV_HYGON
	if (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON) {
		ret = tdm_dev_init();
		if (ret)
			return ret;
	}
#endif
	return 0;
}

static int mmap_psp(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long page;

	page = virt_to_phys((void *)psp_misc->data_pg_aligned) >> PAGE_SHIFT;

	if (remap_pfn_range(vma, vma->vm_start, page, (vma->vm_end - vma->vm_start),
				vma->vm_page_prot)) {
		printk(KERN_ERR "remap failed...\n");
		return -1;
	}
	vma->vm_flags |= (VM_DONTDUMP|VM_DONTEXPAND);
	printk(KERN_INFO "remap_pfn_rang page:[%lu] ok.\n", page);
	return 0;
}

static ssize_t read_psp(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t remaining;

	if ((*ppos + count) > PAGE_SIZE) {
		printk(KERN_ERR "%s: invalid address range, pos %llx, count %lx\n",
				__func__, *ppos, count);
		return -EFAULT;
	}

	remaining = copy_to_user(buf, (char *)psp_misc->data_pg_aligned + *ppos, count);
	if (remaining)
		return -EFAULT;

	*ppos += count;

	return count;
}

static ssize_t write_psp(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t remaining, written;

	if ((*ppos + count) > PAGE_SIZE) {
		printk(KERN_ERR "%s: invalid address range, pos %llx, count %lx\n",
				__func__, *ppos, count);
		return -EFAULT;
	}

	remaining = copy_from_user((char *)psp_misc->data_pg_aligned + *ppos, buf, count);
	written = count - remaining;
	if (!written)
		return -EFAULT;

	*ppos += written;

	return written;
}

/**
 * Try to pin a page
 *
 * @vaddr: the userspace virtual address, must be aligned to PAGE_SIZE
 */
static int psp_pin_user_page(u64 vaddr)
{
	struct page *page;
	long npinned = 0;
	int ref_count = 0;

	// check must be aligned to PAGE_SIZE
	if (vaddr & (PAGE_SIZE - 1)) {
		pr_err("vaddr %llx not aligned to 0x%lx\n", vaddr, PAGE_SIZE);
		return -EFAULT;
	}

	npinned = get_user_pages_fast(vaddr, 1, FOLL_WRITE, &page);
	if (npinned != 1) {
		pr_err("PSP: pin_user_pages_fast fail\n");
		return -ENOMEM;
	}

	ref_count = page_ref_count(page);
	pr_debug("pin user page with address %llx, page ref_count %d\n", vaddr, ref_count);
	return 0;
}

/**
 * Try to unpin a page
 *
 * @vaddr: the userspace virtual address, must be aligned to PAGE_SIZE
 */
static int psp_unpin_user_page(u64 vaddr)
{
	struct page *page;
	long npinned = 0;
	int ref_count = 0;

	// check must be aligned to PAGE_SIZE
	if (vaddr & (PAGE_SIZE - 1)) {
		pr_err("vaddr %llx not aligned to 0x%lx\n", vaddr, PAGE_SIZE);
		return -EFAULT;
	}

	// page reference count increment by 1
	npinned = get_user_pages_fast(vaddr, 1, FOLL_WRITE, &page);
	if (npinned != 1) {
		pr_err("PSP: pin_user_pages_fast fail\n");
		return -ENOMEM;
	}

	// page reference count decrement by 2
	put_page(page);
	put_page(page);

	ref_count = page_ref_count(page);
	pr_debug("unpin user page with address %llx, page ref_count %d\n", vaddr, ref_count);
	return 0;
}

static long ioctl_psp(struct file *file, unsigned int ioctl, unsigned long arg)
{
	unsigned int opcode = 0;
	struct vpsp_dev_ctrl vpsp_ctrl_op;
	int ret = -EFAULT;

	if (_IOC_TYPE(ioctl) != HYGON_PSP_IOC_TYPE) {
		printk(KERN_ERR "%s: invalid ioctl type: 0x%x\n", __func__, _IOC_TYPE(ioctl));
		return -EINVAL;
	}
	opcode = _IOC_NR(ioctl);
	switch (opcode) {
	case HYGON_PSP_MUTEX_ENABLE:
		psp_mutex_lock_timeout(&psp_misc->data_pg_aligned->mb_mutex, 0);
		/* And get the sev lock to make sure no one is using it now. */
		mutex_lock(&sev_cmd_mutex);
		psp_mutex_enabled = 1;
		mutex_unlock(&sev_cmd_mutex);
		/* Wait 10ms just in case someone is right before getting the psp lock. */
		mdelay(10);
		psp_mutex_unlock(&psp_misc->data_pg_aligned->mb_mutex);
		ret = 0;
		break;

	case HYGON_PSP_MUTEX_DISABLE:
		mutex_lock(&sev_cmd_mutex);
		/* And get the psp lock to make sure no one is using it now. */
		psp_mutex_lock_timeout(&psp_misc->data_pg_aligned->mb_mutex, 0);
		psp_mutex_enabled = 0;
		psp_mutex_unlock(&psp_misc->data_pg_aligned->mb_mutex);
		/* Wait 10ms just in case someone is right before getting the sev lock. */
		mdelay(10);
		mutex_unlock(&sev_cmd_mutex);
		ret = 0;
		break;

	case HYGON_VPSP_CTRL_OPT:
		if (copy_from_user(&vpsp_ctrl_op, (void __user *)arg,
						sizeof(struct vpsp_dev_ctrl)))
			return -EFAULT;
		ret = do_vpsp_op_ioctl(&vpsp_ctrl_op);
		if (!ret && copy_to_user((void __user *)arg, &vpsp_ctrl_op,
				sizeof(struct vpsp_dev_ctrl)))
			return -EFAULT;
		break;

	case HYGON_PSP_OP_PIN_USER_PAGE:
		ret = psp_pin_user_page((u64)arg);
		break;

	case HYGON_PSP_OP_UNPIN_USER_PAGE:
		ret = psp_unpin_user_page((u64)arg);
		break;

	default:
		printk(KERN_ERR "%s: invalid ioctl number: %d\n", __func__, opcode);
		return -EINVAL;
	}
	return ret;
}

static const struct file_operations psp_fops = {
	.owner		= THIS_MODULE,
	.mmap		= mmap_psp,
	.read		= read_psp,
	.write		= write_psp,
	.unlocked_ioctl	= ioctl_psp,
};

static int hygon_psp_additional_setup(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	int ret = 0;

	if (!psp_misc) {
		struct miscdevice *misc;

		psp_misc = devm_kzalloc(dev, sizeof(*psp_misc), GFP_KERNEL);
		if (!psp_misc)
			return -ENOMEM;
		psp_misc->data_pg_aligned = (struct psp_dev_data *)get_zeroed_page(GFP_KERNEL);
		if (!psp_misc->data_pg_aligned) {
			dev_err(dev, "alloc psp data page failed\n");
			devm_kfree(dev, psp_misc);
			psp_misc = NULL;
			return -ENOMEM;
		}
		SetPageReserved(virt_to_page(psp_misc->data_pg_aligned));
		psp_mutex_init(&psp_misc->data_pg_aligned->mb_mutex);

		*(uint32_t *)((void *)psp_misc->data_pg_aligned + 8) = 0xdeadbeef;
		misc = &psp_misc->misc;
		misc->minor = MISC_DYNAMIC_MINOR;
		misc->name = "hygon_psp_config";
		misc->fops = &psp_fops;

		ret = misc_register(misc);
		if (ret)
			return ret;
		kref_init(&psp_misc->refcount);
	} else {
		kref_get(&psp_misc->refcount);
	}

	return ret;
}

static void hygon_psp_exit(struct kref *ref)
{
	struct psp_misc_dev *misc_dev = container_of(ref, struct psp_misc_dev, refcount);

	misc_deregister(&misc_dev->misc);
	ClearPageReserved(virt_to_page(misc_dev->data_pg_aligned));
	free_page((unsigned long)misc_dev->data_pg_aligned);
	psp_misc = NULL;
}

int psp_dev_init(struct sp_device *sp)
{
	struct device *dev = sp->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct psp_device *psp;
	unsigned int capability;
	int ret;

	ret = -ENOMEM;
	psp = psp_alloc_struct(sp);
	if (!psp)
		goto e_err;

	sp->psp_data = psp;

	psp->vdata = (struct psp_vdata *)sp->dev_vdata->psp_vdata;
	if (!psp->vdata) {
		ret = -ENODEV;
		dev_err(dev, "missing driver data\n");
		goto e_err;
	}

	psp->io_regs = sp->io_map;

	capability = psp_get_capability(psp);
	if (!capability)
		goto e_disable;

	ret = psp_check_support(psp, capability);
	if (ret)
		goto e_disable;

	/* Disable and clear interrupts until ready */
	iowrite32(0, psp->io_regs + psp->vdata->inten_reg);
	iowrite32(-1, psp->io_regs + psp->vdata->intsts_reg);

	if (pdev->vendor == PCI_VENDOR_ID_HYGON) {
		is_hygon_psp = 1;
		psp_mutex_enabled = 0;
		ret = hygon_psp_additional_setup(sp);
		if (ret) {
			dev_err(dev, "psp: unable to do additional setup\n");
			goto e_err;
		}
	}

	/* Request an irq */
	if (pdev->vendor == PCI_VENDOR_ID_HYGON) {
#ifdef CONFIG_HYGON_PSP2CPU_CMD
		ret = sp_request_psp_irq(psp->sp, psp_irq_handler_hygon, psp->name, psp);
#else
		ret = sp_request_psp_irq(psp->sp, psp_irq_handler, psp->name, psp);
#endif
	} else {
		ret = sp_request_psp_irq(psp->sp, psp_irq_handler, psp->name, psp);
	}
	if (ret) {
		dev_err(dev, "psp: unable to allocate an IRQ\n");
		goto e_err;
	}

	ret = psp_init(psp, capability);
	if (ret)
		goto e_irq;

	if (sp->set_psp_master_device)
		sp->set_psp_master_device(sp);

	/* Enable interrupt */
	iowrite32(-1, psp->io_regs + psp->vdata->inten_reg);

	dev_notice(dev, "psp enabled\n");

	return 0;

e_irq:
	sp_free_psp_irq(psp->sp, psp);
e_err:
	sp->psp_data = NULL;

	dev_notice(dev, "psp initialization failed\n");

	return ret;

e_disable:
	sp->psp_data = NULL;

	return ret;
}

void psp_dev_destroy(struct sp_device *sp)
{
	struct psp_device *psp = sp->psp_data;

	if (!psp)
		return;

#ifdef CONFIG_TDM_DEV_HYGON
	if (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		tdm_dev_destroy();
#endif

	sev_dev_destroy(psp);

	tee_dev_destroy(psp);

	if (is_hygon_psp && psp_misc)
		kref_put(&psp_misc->refcount, hygon_psp_exit);

	sp_free_psp_irq(sp, psp);

	if (sp->clear_psp_master_device)
		sp->clear_psp_master_device(sp);
}

void psp_set_sev_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data)
{
	psp->sev_irq_data = data;
	psp->sev_irq_handler = handler;
}

void psp_clear_sev_irq_handler(struct psp_device *psp)
{
	psp_set_sev_irq_handler(psp, NULL, NULL);
}

void psp_set_tee_irq_handler(struct psp_device *psp, psp_irq_handler_t handler,
			     void *data)
{
	psp->tee_irq_data = data;
	psp->tee_irq_handler = handler;
}

void psp_clear_tee_irq_handler(struct psp_device *psp)
{
	psp_set_tee_irq_handler(psp, NULL, NULL);
}

struct psp_device *psp_get_master_device(void)
{
	struct sp_device *sp = sp_get_psp_master_device();

	return sp ? sp->psp_data : NULL;
}

void psp_pci_init(void)
{
	psp_master = psp_get_master_device();

	if (!psp_master)
		return;

	sev_pci_init();
}

void psp_pci_exit(void)
{
	if (!psp_master)
		return;

	sev_pci_exit();
}
