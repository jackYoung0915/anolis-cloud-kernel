.. SPDX-License-Identifier: GPL-2.0

================================================================
Linux Base Virtual Function Driver for WangXun(R) 10G Ethernet
================================================================

WangXun 10 Gigabit Virtual Function Linux driver.
Copyright (c) 2015 - 2024 Beijing WangXun Technology Co., Ltd.


Contents
========

- Identifying Your Adapter
- Known Issues
- Support


Identifying Your Adapter
========================
The driver is compatible with WangXun Sapphire Dual ports Ethernet Adapters.

Known Issues/Troubleshooting
============================

SR-IOV requires the correct platform and OS support.

The guest OS loading this driver must support MSI-X interrupts.

This driver is only supported as a loadable module at this time. Intel is not
supplying patches against the kernel source to allow for static linking of the
drivers.

VLANs: There is a limit of a total of 64 shared VLANs to 1 or more VFs.


Support
=======
If you got any problem, contact Wangxun support team via support@trustnetic.com
and Cc: netdev.
