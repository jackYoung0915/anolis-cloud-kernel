// Auto update: 2026-03-12 11:06:16 - ZxRYadoC
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig
 */
#include <linux/iomap.h>

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "trace.h"
