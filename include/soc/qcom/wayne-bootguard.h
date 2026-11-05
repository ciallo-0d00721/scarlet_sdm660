/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __QCOM_WAYNE_BOOTGUARD_H__
#define __QCOM_WAYNE_BOOTGUARD_H__

#include <linux/types.h>

#ifdef CONFIG_QCOM_WAYNE_BOOTGUARD
bool wayne_bootguard_is_armed(void);
#else
static inline bool wayne_bootguard_is_armed(void)
{
	return false;
}
#endif

#endif
