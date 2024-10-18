#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

yum install -y iperf3 netperf iproute-tc nmap-ncat readline

cd ../../../../samples/bpf
make -j8

ulimit -l unlimited

./sampleip

./sockex1
./sockex2
./sockex3
./sock_example
./spintest
./syscall_tp
./tc_l2_redirect.sh
./test_cgrp2_sock.sh
./test_cgrp2_tc.sh
./test_cls_bpf.sh
./test_map_in_map
timeout 10 ./tracex1
./tracex2
timeout 10 ./tracex3
timeout 10 ./tracex4
timeout 10 ./tracex5
./tracex6

ip link add veth0t type veth peer name veth1t
ip link set veth0t up
ip link set veth1t up
timeout 10 ./xdp_redirect_cpu --dev veth0t --progname xdp_cpu_map0 \
--cpu 0 --cpu 1 --cpu 2
timeout 10 ./xdp_redirect_cpu --dev veth0t --progname xdp_cpu_map1_touch_data \
--cpu 0 --cpu 2 --cpu 3
timeout 10 ./xdp_redirect_map `cat /sys/class/net/veth0t/ifindex` \
`cat /sys/class/net/veth1t/ifindex`
ip link delete veth0t

./trace_output

cd ../../tools/testing/selftest/bpf/
