# SPDX-License-Identifier: GPL-2.0+

obj-y += ummu-core/
obj-$(CONFIG_UB_UMMU) += ummu.o
ummu-y := ummu_main.o \
	  queue.o \
	  interrupt.o \
	  page_table.o \
	  cfg_table.o \
	  flush.o  \
	  ummu_impl.o \
	  ummu_cfg_v1.o \
	  attribute.o \
	  qos.o \
	  logic_ummu/logic_ummu.o \
	  iommu.o
