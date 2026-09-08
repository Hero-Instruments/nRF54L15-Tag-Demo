/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Connectable BLE advertising — role-independent single advertiser.
 *
 * Advertises whenever the tag can accept an inbound connection. With
 * CONFIG_BT_MAX_CONN=2 the second slot is the outbound CS link, so the
 * peripheral slot alone decides whether we advertise:
 *   - disconnected + idle       -> ad_idle      (ESS only)      : phone services
 *   - disconnected + reflector  -> ad_reflector (RANGING + ESS) : CS initiator or phone
 *   - peripheral connected      -> off (slot in use); resumes on disconnect
 *
 * Three rules keep this from wedging with the radio silent. The previous
 * version trusted a software mirror (`current`) of the radio state and skipped
 * the reconcile whenever it already matched; any desync, or any single failed
 * bt_le_adv_start(), was therefore permanent. Do not reintroduce that.
 *
 * 1. We do NOT track whether the controller consumed the advertiser. A
 *    BT_LE_ADV_OPT_CONN advertiser is stopped by the controller the instant it
 *    creates a connection (see the BT_LE_ADV_OPT_CONN docs in
 *    zephyr/bluetooth/bluetooth.h) — but that also happens for a connection
 *    that then *fails*, and Zephyr calls `disconnected` only for links that
 *    actually reached BT_CONN_DISCONNECT_COMPLETE. So every `connected`
 *    callback, success or failure, either role, just marks the state dirty and
 *    the next reconcile re-derives the radio from scratch.
 *
 * 2. No error path may leave `current` stranded. A failed stop still clears
 *    `current` and falls through to the start; a failed start always arms the
 *    retry below.
 *
 * 3. adv_reconcile() re-arms itself every second while we want to advertise but
 *    are not. That is the backstop that makes "never advertises again"
 *    structurally impossible — -ENOMEM from a not-yet-recycled conn object,
 *    -EAGAIN, a controller status error, or a cause nobody has found yet all
 *    recover on their own.
 *
 * adv_refresh() only *queues* the reconcile, and that matters: notify_disconnected()
 * itself runs on the system workqueue (via conn->deferred_work), so adv_work is
 * FIFO-behind it and observes ble_core's conn pointers already cleared. Running
 * it inline would read ble_peripheral_conn() before ble_core cleared it, since
 * conn callbacks run in linker-sorted order and adv_conn_cbs sorts first. It
 * also keeps bt_le_adv_start() off the BT RX thread, which the DFU mgmt callback
 * reaches via cs_stop().
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

/* Retry cadence while we want to advertise but are not. */
#define ADV_RETRY_INTERVAL K_SECONDS(1)
/* After the first failure, log only every Nth retry so RTT stays readable. */
#define ADV_RETRY_LOG_EVERY 30

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

static const char *variant_str(enum adv_variant v)
{
	switch (v) {
	case ADV_REFLECTOR:
		return "reflector";
	case ADV_IDLE:
		return "idle";
	default:
		return "off";
	}
}

/* Touched only by adv_reconcile(), which runs solely on the system workqueue
 * and is therefore serialised against itself. No lock needed.
 */
static enum adv_variant current = ADV_OFF;
static uint32_t retry_count;

/* Set from the BT RX thread (`connected`), consumed by the reconcile. Starts
 * clear: at boot the radio state genuinely is known (nothing started yet), and
 * a dirty first pass would only make the host log "No valid legacy adv to stop".
 */
static atomic_t adv_dirty = ATOMIC_INIT(0);

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

static void adv_reconcile(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(adv_work, adv_reconcile);

static void adv_reconcile(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Claim the dirty flag: a `connected` event may have consumed the
	 * advertiser without us being told, so re-derive rather than trust
	 * `current` (rule 1).
	 */
	bool dirty = atomic_cas(&adv_dirty, 1, 0);
	enum adv_variant want = desired_variant();

	/* Event-driven passes only: the once-a-second retry loop below would
	 * otherwise flood RTT. This one line names the cause of a stuck
	 * advertiser (`log disable inf app_adv` once that is settled).
	 */
	if (retry_count == 0) {
		LOG_INF("reconcile: want=%s current=%s dirty=%d peri=%p cs=%d/%s",
			variant_str(want), variant_str(current), (int)dirty,
			(void *)ble_peripheral_conn(), (int)cs_is_running(),
			cs_role_str(cs_get_role()));
	}

	if (want == current && !dirty) {
		return;
	}

	if (current != ADV_OFF || dirty) {
		int err = bt_le_adv_stop();

		/* -EALREADY is the normal answer on the dirty path (the
		 * controller already stopped it). Anything else is logged but
		 * must not abort: `current` still goes OFF and we still try to
		 * start, because bailing here is what makes silence permanent
		 * (rule 2). A genuinely-failed stop surfaces as -EALREADY from
		 * the start below, which the retry then covers.
		 */
		if (err && err != -EALREADY) {
			LOG_WRN("adv stop failed (%d)", err);
		}
		current = ADV_OFF;
	}

	if (want != ADV_OFF) {
		const struct bt_data *ad =
			(want == ADV_REFLECTOR) ? ad_reflector : ad_idle;
		size_t len = (want == ADV_REFLECTOR) ? ARRAY_SIZE(ad_reflector)
						     : ARRAY_SIZE(ad_idle);
		int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, len, NULL, 0);

		if (!err) {
			current = want;
			retry_count = 0;
			LOG_INF("advertising: %s", variant_str(want));
		} else if (err == -ENOMEM) {
			/* No free conn object yet — expected when restarting
			 * from `disconnected`. `recycled` normally beats the
			 * retry to it; either way we come back.
			 */
			LOG_DBG("adv start deferred: no free conn object");
		} else if (retry_count == 0 ||
			   (retry_count % ADV_RETRY_LOG_EVERY) == 0) {
			LOG_WRN("adv start failed (%d), retrying", err);
		}
	} else if (current == ADV_OFF) {
		retry_count = 0;
		LOG_INF("advertising: off");
	}

	/* Rule 3: while we want to advertise and are not, keep trying. */
	if (want != ADV_OFF && current == ADV_OFF) {
		retry_count++;
		k_work_reschedule(&adv_work, ADV_RETRY_INTERVAL);
	}
}

void adv_refresh(void)
{
	/* Coalesces with an already-queued refresh, and pre-empts a pending
	 * retry so a real event is acted on immediately.
	 */
	k_work_reschedule(&adv_work, K_NO_WAIT);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(err);

	/* Any connection event may have consumed the advertiser — including one
	 * that failed, for which no `disconnected` will follow. Don't try to
	 * work out whether it did; just force the next reconcile to re-derive
	 * the radio state (rule 1).
	 */
	atomic_set(&adv_dirty, 1);
	adv_refresh();
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
	 * advertiser, and the fast path out of an -ENOMEM start above.
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
