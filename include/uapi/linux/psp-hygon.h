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


/* The CSV RTMR version in the kernel */
#define CSV_RTMR_VERSION_MAX	1U
#define CSV_RTMR_VERSION_MIN	1U

/* The size of CSV RTMR register. */
#define CSV_RTMR_REG_SIZE	32 /* SM3 */

/* The size of the CSV RTMR extend data. */
#define CSV_RTMR_EXTEND_LEN	CSV_RTMR_REG_SIZE

/**
 * The number of the CSV RTMR registers.
 *
 * For version 1, the number of RTMR registers for a guest is 5. The mapping
 * from TPM2 PCR index to RTMR index is shown as follows:
 *
 *   +---------------+--------------------------------------+---------+
 *   | TPM PCR Index | Event Log Measurement Register Index |  RTMR   |
 *   |---------------|--------------------------------------|-------- |
 *   |    0          |    0                                 | RTMR[0] |
 *   |    1,7        |    1                                 | RTMR[1] |
 *   |    2~6        |    2                                 | RTMR[2] |
 *   |    8~15       |    3                                 | RTMR[3] |
 *   |    16,23      |    4                                 | RTMR[4] |
 *   +---------------+--------------------------------------+---------+
 */
#define CSV_RTMR_REG_NUM	5

/* The maximum index of the CSV RTMR registers. */
#define CSV_RTMR_REG_INDEX_MAX	(CSV_RTMR_REG_NUM - 1)

#endif  /* __PSP_HYGON_USER_H__ */
