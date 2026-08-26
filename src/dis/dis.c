/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Device Information Service (DIS, 0x180A) — per-unit serial number.
 *
 * The service is Zephyr's (subsys/bluetooth/services/dis.c), registered
 * statically via BT_GATT_SERVICE_DEFINE at boot. Manufacturer, model and
 * firmware revision are all compile-time strings from prj.conf, so there is
 * nothing to do for them here. Only the serial number needs runtime work,
 * which is what CONFIG_BT_DIS_SETTINGS is for: it turns the DIS strings into
 * RAM buffers reachable through the "bt/dis" settings handler.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>

#include "dis.h"

LOG_MODULE_REGISTER(app_dis, CONFIG_LOG_DEFAULT_LEVEL);

/* 6-byte address rendered as hex, plus the NUL. */
#define SERIAL_LEN sizeof("001122334455")

int dis_init(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);
	char serial[SERIAL_LEN];
	const uint8_t *a;
	int err;

	bt_id_get(addrs, &count);
	if (count == 0) {
		LOG_WRN("no Bluetooth identity; DIS serial left at its default");
		return -ENODEV;
	}

	/* bt_addr_le_t stores the address little-endian; print it MSB-first so
	 * it reads the same way a scanner displays the tag's address.
	 */
	a = addrs[0].a.val;
	snprintk(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
		 a[5], a[4], a[3], a[2], a[1], a[0]);

	/* Length excludes the NUL: the "bt/dis" handler reads at most
	 * CONFIG_BT_DIS_STR_MAX bytes and terminates the buffer itself.
	 */
	err = settings_runtime_set("bt/dis/serial", serial, strlen(serial));
	if (err) {
		LOG_ERR("could not set the DIS serial number (%d)", err);
		return err;
	}

	LOG_INF("DIS ready (serial %s)", serial);
	return 0;
}
