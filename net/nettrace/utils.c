// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/processor.h>
#include <linux/fs.h>


int file_append(struct file *file, void *data, unsigned int size)
{
	loff_t pos = file->f_pos;
	int n = 1;

	while (size > 0 && n > 0) {
		n = __kernel_write(file, data, size, &pos);
		size -= n;
	}
	file->f_pos = pos;
	return 0;
}

struct file *file_create(const char *path)
{
	struct file *file = NULL;

	file = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(file))
		return NULL;

	return file;
}

int access_path(const char *path)
{
	struct file *file = NULL;

	file = filp_open(path, O_DIRECTORY, 0644);
	if (IS_ERR(file))
		return PTR_ERR(file);
	filp_close(file, NULL);
	return 0;
}

void file_close(struct file *file)
{
	filp_close(file, NULL);
}
