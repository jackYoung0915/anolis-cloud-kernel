/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021 Arm Ltd. */

#include <asm/mpam.h>

#include <linux/arm_mpam.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>

DEFINE_STATIC_KEY_FALSE(arm64_mpam_has_hcr);
EXPORT_SYMBOL_GPL(arm64_mpam_has_hcr);
DEFINE_PER_CPU(u64, arm64_mpam_default);
DEFINE_PER_CPU(u64, arm64_mpam_current);

u64 mpam_sysreg_offset;

static int __init vpartid_offset_setup(char *str)
{
	int vpartid_offset = 0;

	get_option(&str, &vpartid_offset);
	mpam_sysreg_offset |= FIELD_PREP(MPAM_SYSREG_PARTID_D, vpartid_offset);
	mpam_sysreg_offset |= FIELD_PREP(MPAM_SYSREG_PARTID_I, vpartid_offset);
	return 1;
}
__setup("mpam_vpartid_offset=", vpartid_offset_setup);
