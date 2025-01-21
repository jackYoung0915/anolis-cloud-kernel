#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Copyright (c) 2024 Alibaba Group Holding Limited.
# Author: Tianchen Ding <dtcccc@linux.alibaba.com>

GI_PATH=/proc/sys/kernel/sched_group_identity_enabled
gi_bak=$(cat $GI_PATH)
CS_PATH=/proc/sys/kernel/sched_core
cs_bak=$(cat $CS_PATH)
SCRIPT_PATH=$(dirname "$0")

cleanup() {
	echo 0 > $CS_PATH
	echo $gi_bak > $GI_PATH
	echo $cs_bak > $CS_PATH
	exit $1
}

echo 0 > $GI_PATH
echo 1 > $CS_PATH

$SCRIPT_PATH/cs_prctl_test

cleanup $?
