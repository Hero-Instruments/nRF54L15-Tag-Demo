/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Bluetooth SIG Device Information Service (DIS, 0x180A) — per-unit serial.
 */
#ifndef APP_DIS_H_
#define APP_DIS_H_

#include <zephyr/kernel.h>

#if IS_ENABLED(CONFIG_APP_DIS)

/**
 * @brief Program the DIS Serial Number characteristic.
 *
 * The GATT service itself is Zephyr's (CONFIG_BT_DIS) and is registered
 * statically at boot, with manufacturer, model and firmware revision baked in
 * from Kconfig. This fills in the one value that is only knowable at runtime:
 * the serial number, derived from the Bluetooth identity address.
 *
 * Must be called after ble_core_init(), since bt_id_get() requires that
 * bt_enable() has run.
 *
 * @return 0 on success, negative errno otherwise.
 */
int dis_init(void);

#else

static inline int dis_init(void)
{
	return 0;
}

#endif /* CONFIG_APP_DIS */

#endif /* APP_DIS_H_ */
