// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 3snic Technologies Co., Ltd */

#include "sss_linux_kernel.h"

#ifndef HAVE_TIMER_SETUP
void initialize_timer(const void *adapter_hdl, struct timer_list *timer)
{
	if (!adapter_hdl || !timer)
		return;

	init_timer(timer);
}
#endif
