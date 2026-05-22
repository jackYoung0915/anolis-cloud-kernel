// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nettrace support.
 * Author:
 * 	Menglong Dong
 * Migrator:
 * 	xu xin
 *
 * Copyright (C) 2025 ZTE Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NETDUMP_CORE_H
#define NETDUMP_CORE_H

#include "kprobe.h"
#include "parser.h"

/* Internal status of nettrace */
enum nettrace_status {
	/* nettrace is initializing related tracepoints and its dump files */
	NT_INIT,
	/* nettrace is ready and tracing */
	NT_RUNNING,
	/* somebody is rmmoving the nettrace.ko */
	NT_EXITING
};

extern enum nettrace_status nt_status;

#endif //NETDUMP_CORE_H
