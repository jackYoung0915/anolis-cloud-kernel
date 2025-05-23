/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Copyright (C), 2020, Linkdata Technologies Co., Ltd.
 *
 * @file: sxe_version.h
 * @author: Linkdata
 * @date: 2025.02.16
 * @brief:
 * @note:
 */
#ifndef __SXE_VER_H__
#define __SXE_VER_H__

#define SXE_VERSION                "1.5.0.30"
#define SXE_COMMIT_ID              "cd8fdce"
#define SXE_BRANCH                 "develop/rc/sagitta-1.5.0_B030-Anolis"
#define SXE_BUILD_TIME             "2025-05-13 20:33:58"

#define SXE_DRV_NAME                   "sxe"
#define SXEVF_DRV_NAME                 "sxevf"
#define SXE_DRV_LICENSE                "GPL v2"
#define SXE_DRV_AUTHOR                 "sxe"
#define SXEVF_DRV_AUTHOR               "sxevf"
#define SXE_DRV_DESCRIPTION            "sxe driver"
#define SXEVF_DRV_DESCRIPTION          "sxevf driver"

#define SXE_FW_NAME                     "soc"
#define SXE_FW_ARCH                     "arm32"

#ifndef PS3_CFG_RELEASE
#define PS3_SXE_FW_BUILD_MODE             "debug"
#else
#define PS3_SXE_FW_BUILD_MODE             "release"
#endif

#endif
