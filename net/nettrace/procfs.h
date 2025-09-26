/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Nettrace support.
 *
 * Copyright (C) 2022 ZTE Corporation. All rights reserved.
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

#ifndef NETDUMP_PROCFS_H
#define NETDUMP_PROCFS_H

extern int __net_init ntrace_proc_init(void);

extern void __net_exit ntrace_proc_exit(void);

#endif //NETDUMP_PROCFS_H
