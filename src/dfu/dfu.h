/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * OTA / DFU housekeeping around MCUboot image updates delivered over SMP.
 */
#ifndef APP_DFU_H_
#define APP_DFU_H_

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_APP_DFU)

/**
 * @brief Log the running image and arm the self-confirm timer.
 *
 * Reports the running version/slot, and — if MCUboot booted this image as a
 * test image — schedules the confirmation that makes it permanent after
 * APP_DFU_CONFIRM_DELAY_S of healthy uptime. Also registers the MCUmgr image
 * hooks that quiesce Channel Sounding during an upload.
 *
 * Call early in main(), before the rest of the modules start.
 *
 * @return 0 on success, negative errno otherwise.
 */
int dfu_init(void);

/** @brief True while a host is uploading a firmware image over SMP. */
bool dfu_in_progress(void);

#else

static inline int dfu_init(void)
{
	return 0;
}

static inline bool dfu_in_progress(void)
{
	return false;
}

#endif /* CONFIG_APP_DFU */

#endif /* APP_DFU_H_ */
