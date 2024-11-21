// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Alibaba Group Holding Limited.
 * Author: Tianchen Ding <dtcccc@linux.alibaba.com>
 */

#define _GNU_SOURCE
#include <sys/prctl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PR_GET_IDENTITY 1000
#define PR_SET_IDENTITY 1001

int main(int argc, char *argv[])
{
	int id_flags = 0, res;
	pid_t pid;

	if (argc == 1) {
		printf("usage: {pid} to get, or {pid flags} to set\n");
		return 1;
	}

	pid = atoi(argv[1]);
	if (argc == 2) {
		res = prctl(PR_GET_IDENTITY, pid, &id_flags, 0, 0);
		if (res)
			return -errno;

		printf("%d\n", id_flags);
		return 0;
	}

	id_flags = atoi(argv[2]);
	res = prctl(PR_SET_IDENTITY, pid, id_flags, 0, 0);
	if (res)
		return -errno;

	return 0;
}
