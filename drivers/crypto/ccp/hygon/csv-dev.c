// SPDX-License-Identifier: GPL-2.0-only
/*
 * HYGON CSV interface
 *
 * Copyright (C) 2024 Hygon Info Technologies Ltd.
 *
 * Author: Liyang Han <hanliyang@hygon.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/psp-sev.h>
#include <linux/psp-csv.h>

#include "csv-dev.h"

/*
 * Hygon CSV build info:
 *    Hygon CSV build info is 32-bit in length other than 8-bit as that
 *    in AMD SEV.
 */
u32 hygon_csv_build;

/*
 * csv_update_api_version used to update the api version of HYGON CSV
 * firmwareat driver side.
 * Currently, we only need to update @hygon_csv_build.
 */
void csv_update_api_version(struct sev_user_data_status *status)
{
	if (status) {
		hygon_csv_build = (status->flags >> 9) |
				   ((u32)status->build << 23);
	}
}

int csv_get_extension_info(void *buf, size_t *size)
{
	/* If @hygon_csv_build is 0, this means CSV firmware doesn't exist or
	 * the psp device doesn't exist.
	 */
	if (hygon_csv_build == 0)
		return -ENODEV;

	/* The caller must provide valid @buf and the @buf must >= 4 bytes in
	 * size.
	 */
	if (!buf || !size || *size < sizeof(uint32_t)) {
		if (size)
			*size = sizeof(uint32_t);

		return -EINVAL;
	}

	/* Since firmware with build id 2200, support:
	 *   a. issue LAUNCH_ENCRYPT_DATA command more than once for a
	 *      CSV3 guest.
	 *   b. inject secret to a CSV3 guest.
	 */
	if (csv_version_greater_or_equal(2200)) {
		*(uint32_t *)buf |= CSV_EXT_CSV3_MULT_LUP_DATA;
		*(uint32_t *)buf |= CSV_EXT_CSV3_INJ_SECRET;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(csv_get_extension_info);
