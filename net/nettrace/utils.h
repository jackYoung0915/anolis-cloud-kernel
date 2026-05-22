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

#ifndef NETDUMP_UTILS_H
#define NETDUMP_UTILS_H

#include <linux/string.h>
#include <linux/fs.h>

#define MAX_FILE_NAME 256

#define str_append(dest, fmt, args...)    \
	sprintf(dest + strlen(dest), fmt, ##args)

extern int file_append(struct file *file, void *data, unsigned int size);

extern struct file *file_create(const char *path);

extern void file_close(struct file *file);

static inline int streq(char *a, char *b)
{
	return strcmp(a, b) == 0;
}

extern int access_path(const char *path);

#endif //NETDUMP_UTILS_H
