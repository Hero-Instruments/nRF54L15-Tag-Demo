/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Connectable BLE advertising — role-independent single advertiser.
 *
 * Advertises whenever the tag can accept a connection (single-connection model,
 * BT_MAX_CONN=1):
 *   - disconnected + idle       -> ad_idle      (ESS only)      : phone services
 *   - disconnected + reflector  -> ad_reflector (RANGING + ESS) : CS initiator or phone
 *   - initiator (running)       -> off (device is central, needs its one slot)
 *   - connected                 -> off (slot in use); resumes on disconnect
 *
 * Two rules keep the software state honest about the radio — do not drop either,
 * they are what make a reconnect after a disconnect work at all:
 *
 * 1. A BT_LE_ADV_OPT_CONN advertiser is stopped *by the controller* the instant it
 *    creates a connection, and the application must start it again (see the
 *    BT_LE_ADV_OPT_CONN docs in zephyr/bluetooth/bluetooth.h). So `connected` marks
 *    `current = ADV_OFF` for a peripheral link; otherwise `current` goes stale and
 *    the want==current short-circuit below turns every later refresh into a no-op.
 *
 * 2. adv_refresh() only *queues* the reconcile. Running it on the system workqueue
 *    means it observes ble_core's conn pointers after every BT_CONN_CB_DEFINE
 *    callback has returned — those run in linker-sorted (alphabetical) order, so
 *    reconciling inline here would read `ble_peripheral_conn()` before ble_core has
 *    cleared it. It also keeps bt_le_adv_start() off the BT RX thread, which the
 *    DFU mgmt callback reaches via cs_stop().
 *
 * Restarting from `disconnected` can still fail with -ENOMEM when both conn slots
 * are busy (initiator role: outbound CS link + inbound phone link), because the
 * conn object is not back in the pool yet. `recycled` is the guaranteed-safe retry
 * point; a failed start leaves `current = ADV_OFF` so that retry takes effect.
 */
#include "adv.h"
#include "ble_core.h"
#include "cs_shared.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/services/ras.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_adv, CONFIG_LOG_DEFAULT_LEVEL);

/* Idle payload: Environmental Sensing UUID + name (phone-facing services). */
static const struct bt_data ad_idle[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Reflector payload: additionally advertise the Ranging Service UUID so a CS
 * initiator (RAS mode, scans by UUID) can find it. Harmless for IPT (name scan).
 */
static const struct bt_data ad_reflector[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_RANGING_SERVICE_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_ESS_VAL)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

enum adv_variant {
	ADV_OFF,
	ADV_IDLE,
	ADV_REFLECTOR,
};

static K_MUTEX_DEFINE(adv_mtx);
static enum adv_variant current = ADV_OFF;

static enum adv_variant desired_variant(void)
{
	if (ble_peripheral_conn()) {
		return ADV_OFF; /* peripheral slot in use (screen/phone/SMP host) */
	}
	if (cs_is_running() && cs_get_role() == CS_ROLE_REFLECTOR) {
		return ADV_REFLECTOR; /* advertise Ranging UUID for the initiator */
	}
	/* Idle or initiator: advertise (ESS + name) so a screen/host can connect.
	 * As initiator we keep the separate outbound central link to the reflector.
	 */
	return ADV_IDLE;
}

static void adv_reconcile(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&adv_mtx, K_FOREVER);

	enum adv_variant want = desired_variant();

	if (want == current) {
		k_mutex_unlock(&adv_mtx);
		return;
	}

	if (current != ADV_OFF) {
		int err = bt_le_adv_stop();

		if (err && err != -EALREADY) {
			/* Leave `current` alone: we do not know what the radio is
			 * doing, and starting a second advertiser would fail anyway.
			 */
			LOG_ERR("adv stop failed (%d)", err);
			k_mutex_unlock(&adv_mtx);
			return;
		}
		current = ADV_OFF;
	}

	if (want != ADV_OFF) {
		const struct bt_data *ad =
			(want == ADV_REFLECTOR) ? ad_reflector : ad_idle;
		size_t len = (want == ADV_REFLECTOR) ? ARRAY_SIZE(ad_reflector)
						     : ARRAY_SIZE(ad_idle);
		int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, len, NULL, 0);

		if (err == -ENOMEM) {
			/* No free conn object yet — expected when restarting from
			 * `disconnected`. `current` stays ADV_OFF so the `recycled`
			 * callback retries this and succeeds.
			 */
			LOG_DBG("adv start deferred: no free conn object");
		} else if (err) {
			LOG_ERR("adv start failed (%d)", err);
		} else {
			current = want;
			LOG_INF("advertising: %s",
				want == ADV_REFLECTOR ? "reflector" : "idle");
		}
	} else {
		LOG_INF("advertising: off");
	}

	k_mutex_unlock(&adv_mtx);
}

static K_WORK_DEFINE(adv_work, adv_reconcile);

void adv_refresh(void)
{
	/* A no-op if already queued, so the several triggers below coalesce. */
	k_work_submit(&adv_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info;

	/* On success as peripheral the controller has already stopped the
	 * advertiser (see rule 1 in the file header) — mirror that here, or every
	 * later refresh short-circuits on a stale `current` and never restarts it.
	 */
	if (!err && !bt_conn_get_info(conn, &info) &&
	    info.role == BT_CONN_ROLE_PERIPHERAL) {
		k_mutex_lock(&adv_mtx, K_FOREVER);
		current = ADV_OFF;
		k_mutex_unlock(&adv_mtx);
	}

	adv_refresh(); /* slot now in use (or connect failed) -> reconcile */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);
	adv_refresh(); /* slot free again -> resume per current role */
}

static void recycled(void)
{
	/* A conn object is back in the pool: the safe point to (re)start the
	 * advertiser, and the retry for an -ENOMEM start above.
	 */
	adv_refresh();
}

BT_CONN_CB_DEFINE(adv_conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

int adv_init(void)
{
	adv_refresh();
	return 0;
}
