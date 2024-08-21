/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Userspace interface for HYGON Platform Security Processor (PSP)
 * commands.
 *
 * Copyright (C) 2024 Hygon Info Technologies Ltd.
 *
 * Author: Liyang Han <hanliyang@hygon.cn>
 */

#ifndef __PSP_HYGON_USER_H__
#define __PSP_HYGON_USER_H__

#include <linux/types.h>

/**
 * struct csv_guest_user_data_attestation - ATTESTATION command parameters
 *
 * @user_data: user specified data for the attestation report
 * @mnonce: user's random nonce
 * @hash: sm3 hash of the @user_data and @mnonce
 */
struct csv_guest_user_data_attestation {
	__u8 user_data[64];			/* In */
	__u8 monce[16];				/* In */
	__u8 hash[32];				/* In */
} __packed;

#endif  /* __PSP_HYGON_USER_H__ */
