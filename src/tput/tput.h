/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Custom BLE Throughput Service: measure transfer rate over all four GATT
 * transfer paths (write-without-response, write, notify, indicate).
 */
#ifndef APP_TPUT_H_
#define APP_TPUT_H_

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_APP_TPUT)

/**
 * @brief Initialise the Throughput Service.
 *
 * The GATT service is registered statically at boot. Inbound writes are counted
 * as they arrive; outbound (notify/indicate) runs are started by writing the
 * Control characteristic.
 *
 * @return 0 on success.
 */
int tput_init(void);

#else

static inline int tput_init(void)
{
	return 0;
}

#endif /* CONFIG_APP_TPUT */

#endif /* APP_TPUT_H_ */
