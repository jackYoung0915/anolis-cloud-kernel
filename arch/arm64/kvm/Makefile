# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y += -I $(srctree)/$(src)
ccflags-y += -I $(srctree)/$(src)/hyp/include

KVM=../../../virt/kvm
IRQCHIP=../../../drivers/irqchip

obj-$(CONFIG_KVM) += kvm.o

kvm-y := $(KVM)/kvm_main.o $(KVM)/coalesced_mmio.o $(KVM)/eventfd.o \
	 $(KVM)/vfio.o $(KVM)/irqchip.o \
	 arm.o mmu.o mmio.o psci.o perf.o hypercalls.o pvtime.o \
	 inject_fault.o regmap.o va_layout.o handle_exit.o \
	 guest.o debug.o reset.o sys_regs.o \
	 vgic-sys-reg-v3.o fpsimd.o pmu.o \
	 aarch32.o arch_timer.o \
	 vgic/vgic.o vgic/vgic-init.o \
	 vgic/vgic-irqfd.o vgic/vgic-v2.o \
	 vgic/vgic-v3.o vgic/vgic-v4.o \
	 vgic/vgic-mmio.o vgic/vgic-mmio-v2.o \
	 vgic/vgic-mmio-v3.o vgic/vgic-kvm-device.o \
	 vgic/vgic-its.o vgic/vgic-debug.o

kvm-$(CONFIG_KVM_ARM_PMU)  += pmu-emul.o
kvm-$(CONFIG_VIRT_PLAT_DEV)  += vgic/shadow_dev.o

ifndef CONFIG_KVM_ARM_HOST_VHE_ONLY
obj-$(CONFIG_KVM) += hyp/
else
kvm-y += $(IRQCHIP)/irq-gic-v4.o
kvm-y += $(addprefix hyp/, fpsimd.o hyp-entry.o entry.o vgic-v2-cpuif-proxy.o aarch32.o pgtable.o smccc_wa.o vgic-v3-sr.o) $(addprefix hyp/vhe/, debug-sr.o sysreg-sr.o timer-sr.o tlb.o)

extra-y := hyp.lds switch.tmp.o

$(obj)/hyp.lds: $(src)/hyp/hyp.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)

$(obj)/switch.tmp.o: $(src)/hyp/vhe/switch.c FORCE
	$(call if_changed_rule,cc_o_c)

LDFLAGS_switch.o := -r -T
$(obj)/switch.o: $(obj)/hyp.lds $(obj)/switch.tmp.o FORCE
	$(call if_changed,ld)

kvm-y += switch.o
endif
