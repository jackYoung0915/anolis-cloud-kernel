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
	echo 0 > $GI_PATH
	echo $gi_bak > $GI_PATH
	echo $cs_bak > $CS_PATH
	exit $1
}

ksoftirqd_pid=$(pgrep -n "ksoftirqd/")
echo 0 > $CS_PATH

# should get 0 when gi disabled
echo 0 > $GI_PATH
identity=$($SCRIPT_PATH/task_identity_test $ksoftirqd_pid)
if [ $? -ne 0 ]
then
	echo "get identity for pid=$ksoftirqd_pid failed!"
	cleanup 1
fi
if [ $identity -ne 0 ]
then
	echo "expected identity=0, actual=$identity"
	cleanup 1
fi

# should set fail when gi disabled
$SCRIPT_PATH/task_identity_test $ksoftirqd_pid 22
if [ $? -eq 0 ]
then
	echo "set identity for pid=$ksoftirqd_pid unexpected success!"
	cleanup 1
fi

# should set success when gi enabled
echo 1 > $GI_PATH
$SCRIPT_PATH/task_identity_test $ksoftirqd_pid 22
if [ $? -ne 0 ]
then
	echo "set identity for pid=$ksoftirqd_pid failed!"
	cleanup 1
fi

# should get 22 when set succeeded
identity=$($SCRIPT_PATH/task_identity_test $ksoftirqd_pid)
if [ $? -ne 0 ]
then
	echo "get identity for pid=$ksoftirqd_pid failed!"
	cleanup 1
fi
if [ $identity -ne 22 ]
then
	echo "expected identity=22, actual=$identity"
	cleanup 1
fi

# should get 0 when gi disabled
echo 0 > $GI_PATH
identity=$($SCRIPT_PATH/task_identity_test $ksoftirqd_pid)
if [ $? -ne 0 ]
then
	echo "get identity for pid=$ksoftirqd_pid failed!"
	cleanup 1
fi
if [ $identity -ne 0 ]
then
	echo "expected identity=0, actual=$identity"
	cleanup 1
fi

echo "task_identity_test PASSED"
cleanup 0
