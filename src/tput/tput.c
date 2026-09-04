/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Custom BLE Throughput Service (128-bit vendor UUID).
 *
 * Measures transfer rate over the four GATT paths that behave differently on
 * air, all on a single Data characteristic:
 *
 *   host -> tag : Write Without Response  (unacknowledged)
 *   host -> tag : Write                   (acknowledged per packet)
 *   tag  -> host: Notification            (unacknowledged)
 *   tag  -> host: Indication              (confirmed per packet)
 *
 * Inbound writes are counted as they arrive — Zephyr sets
 * BT_GATT_WRITE_FLAG_CMD for write-without-response, so one callback serves
 * both directions and keeps them in separate counters. Outbound runs are driven
 * from the Control characteristic and pump a filler buffer until a limit is hit.
 *
 * Payloads are raw filler with no header: a run measures the transport ceiling
 * with zero protocol overhead. Packet loss is derived by comparing the two ends'
 * counters, not from packet contents.
 *
 * NCS's own bt_throughput service is not reused: it declares a single
 * READ | WRITE_WITHOUT_RESP characteristic, which covers one of the four paths.
 */
#include "tput.h"
#include "ble_core.h"

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_tput, CONFIG_LOG_DEFAULT_LEVEL);

/* 128-bit vendor UUIDs, same base as the Motion Service (f0de1b0x). */
#define BT_UUID_TAG_TPUT_SVC_VAL \
	BT_UUID_128_ENCODE(0xf0de1c00, 0x9b0f, 0x4a3e, 0x8b1a, 0x2f9c0d5e7a10)
#define BT_UUID_TAG_TPUT_DATA_VAL \
	BT_UUID_128_ENCODE(0xf0de1c01, 0x9b0f, 0x4a3e, 0x8b1a, 0x2f9c0d5e7a10)
#define BT_UUID_TAG_TPUT_CTRL_VAL \
	BT_UUID_128_ENCODE(0xf0de1c02, 0x9b0f, 0x4a3e, 0x8b1a, 0x2f9c0d5e7a10)

#define BT_UUID_TAG_TPUT_SVC  BT_UUID_DECLARE_128(BT_UUID_TAG_TPUT_SVC_VAL)
#define BT_UUID_TAG_TPUT_DATA BT_UUID_DECLARE_128(BT_UUID_TAG_TPUT_DATA_VAL)
#define BT_UUID_TAG_TPUT_CTRL BT_UUID_DECLARE_128(BT_UUID_TAG_TPUT_CTRL_VAL)

/* Value-attribute index of the Data characteristic within tput_svc. */
#define ATTR_DATA_VAL 2

/* ATT overhead on a notify/indicate PDU: opcode + handle. */
#define ATT_NOTIFY_OVERHEAD 3
#define TPUT_MAX_PAYLOAD    (CONFIG_BT_L2CAP_TX_MTU - ATT_NOTIFY_OVERHEAD)

/* ATT MTU before any exchange has happened. */
#define ATT_DEFAULT_MTU 23

/* Control characteristic wire sizes. */
#define TPUT_CTRL_CMD_LEN   10
#define TPUT_CTRL_STATS_LEN 44

/* op */
enum tput_op {
	TPUT_OP_STOP        = 0,
	TPUT_OP_START_TX    = 1,
	TPUT_OP_RESET_STATS = 2,
};

/* method */
enum tput_method {
	TPUT_METHOD_NOTIFY   = 0,
	TPUT_METHOD_INDICATE = 1,
};

/* limit_kind */
enum tput_limit_kind {
	TPUT_LIMIT_NONE     = 0, /* free-run until STOP */
	TPUT_LIMIT_DURATION = 1, /* limit = milliseconds */
	TPUT_LIMIT_BYTES    = 2, /* limit = bytes */
};

/* state */
enum tput_state {
	TPUT_STATE_IDLE    = 0,
	TPUT_STATE_RUNNING = 1,
};

struct tput_stats {
	uint32_t tx_packets;
	uint32_t tx_bytes;
	uint32_t tx_errors;
	uint32_t tx_elapsed_ms;

	uint32_t rx_cmd_packets; /* write without response */
	uint32_t rx_cmd_bytes;
	uint32_t rx_req_packets; /* write with response */
	uint32_t rx_req_bytes;
	uint32_t rx_elapsed_ms;
};

static struct tput_stats stats;

/* RX window: first write to most recent write, so rx_bytes/rx_elapsed is a
 * real rate rather than "bytes since boot".
 */
static int64_t rx_first_ms;
static bool rx_started;

/* Run parameters, published by the control write and read by the sender. */
static uint8_t run_method = TPUT_METHOD_NOTIFY;
static uint8_t run_limit_kind = TPUT_LIMIT_NONE;
static uint32_t run_limit;
static uint16_t run_payload_len;
static uint16_t req_payload_len; /* as asked for; 0 = auto */

static atomic_t running = ATOMIC_INIT(0);
static bool data_subscribed;
static uint16_t data_ccc_value;

/* Idle sender parks here; START_TX gives it. */
static K_SEM_DEFINE(run_sem, 0, 1);
/* TX buffer credits, given back from the notify-complete callback. */
static K_SEM_DEFINE(tx_credits, 0, CONFIG_BT_ATT_TX_COUNT);
/* One indication may be outstanding per connection. */
static K_SEM_DEFINE(indicate_done, 0, 1);

/* Filler payload; contents are irrelevant, only the length matters. */
static uint8_t filler[TPUT_MAX_PAYLOAD];

static void data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);

/* Data: one handler for both write paths. Contents are discarded; only the
 * length and the acknowledged/unacknowledged distinction are recorded.
 */
static ssize_t data_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset,
			  uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(buf);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Prepared writes would be counted twice (once on prepare, once on
	 * execute) and are not part of any throughput path we measure.
	 */
	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	if (!rx_started) {
		rx_first_ms = k_uptime_get();
		rx_started = true;
	}

	if (flags & BT_GATT_WRITE_FLAG_CMD) {
		stats.rx_cmd_packets++;
		stats.rx_cmd_bytes += len;
	} else {
		stats.rx_req_packets++;
		stats.rx_req_bytes += len;
	}

	stats.rx_elapsed_ms = (uint32_t)(k_uptime_get() - rx_first_ms);

	return len;
}

static uint16_t current_mtu(void)
{
	struct bt_conn *conn = ble_peripheral_conn();

	return conn ? bt_gatt_get_mtu(conn) : ATT_DEFAULT_MTU;
}

static uint16_t max_payload(void)
{
	uint16_t mtu = current_mtu();

	if (mtu <= ATT_NOTIFY_OVERHEAD) {
		return 1;
	}

	return MIN((uint16_t)(mtu - ATT_NOTIFY_OVERHEAD), TPUT_MAX_PAYLOAD);
}

/* Control: current run state and counters. */
static ssize_t ctrl_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	uint8_t out[TPUT_CTRL_STATS_LEN];

	out[0] = atomic_get(&running) ? TPUT_STATE_RUNNING : TPUT_STATE_IDLE;
	out[1] = run_method;
	sys_put_le16(run_payload_len, &out[2]);

	sys_put_le32(stats.tx_packets, &out[4]);
	sys_put_le32(stats.tx_bytes, &out[8]);
	sys_put_le32(stats.tx_errors, &out[12]);
	sys_put_le32(stats.tx_elapsed_ms, &out[16]);

	sys_put_le32(stats.rx_cmd_packets, &out[20]);
	sys_put_le32(stats.rx_cmd_bytes, &out[24]);
	sys_put_le32(stats.rx_req_packets, &out[28]);
	sys_put_le32(stats.rx_req_bytes, &out[32]);
	sys_put_le32(stats.rx_elapsed_ms, &out[36]);

	sys_put_le16(current_mtu(), &out[40]);
	sys_put_le16(max_payload(), &out[42]);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, out,
				 sizeof(out));
}

static void reset_stats(void)
{
	memset(&stats, 0, sizeof(stats));
	rx_started = false;
	rx_first_ms = 0;
}

#if IS_ENABLED(CONFIG_APP_TPUT_REQUEST_FAST_INTERVAL)
static void request_fast_interval(void)
{
	struct bt_conn *conn = ble_peripheral_conn();

	if (!conn) {
		return;
	}

	struct bt_le_conn_param param = {
		.interval_min = CONFIG_APP_TPUT_CONN_INTERVAL_MIN,
		.interval_max = CONFIG_APP_TPUT_CONN_INTERVAL_MAX,
		.latency = 0,
		.timeout = 400, /* 4 s supervision timeout */
	};

	int err = bt_conn_le_param_update(conn, &param);

	if (err) {
		LOG_WRN("conn param update request failed (%d)", err);
	}
}
#else
static inline void request_fast_interval(void)
{
}
#endif

static int start_run(void)
{
	uint16_t cap = max_payload();

	if (!ble_peripheral_conn()) {
		LOG_WRN("start: no peripheral connection");
		return -ENOTCONN;
	}

	if (!data_subscribed) {
		LOG_WRN("start: nothing subscribed to the data characteristic");
		return -EAGAIN;
	}

	/* A client subscribed for notifications cannot receive indications and
	 * vice versa — starting anyway would just spin on -EINVAL.
	 */
	if (run_method == TPUT_METHOD_NOTIFY &&
	    !(data_ccc_value & BT_GATT_CCC_NOTIFY)) {
		LOG_WRN("start: notify requested but client subscribed 0x%04x",
			data_ccc_value);
		return -EAGAIN;
	}
	if (run_method == TPUT_METHOD_INDICATE &&
	    !(data_ccc_value & BT_GATT_CCC_INDICATE)) {
		LOG_WRN("start: indicate requested but client subscribed 0x%04x",
			data_ccc_value);
		return -EAGAIN;
	}

	run_payload_len = req_payload_len ? MIN(req_payload_len, cap) : cap;

	stats.tx_packets = 0;
	stats.tx_bytes = 0;
	stats.tx_errors = 0;
	stats.tx_elapsed_ms = 0;

	/* Recharge credits: a previous run may have ended with sends still in
	 * flight, whose completions land after we stopped counting.
	 */
	k_sem_reset(&tx_credits);
	for (int i = 0; i < CONFIG_BT_ATT_TX_COUNT; i++) {
		k_sem_give(&tx_credits);
	}
	k_sem_reset(&indicate_done);

	request_fast_interval();

	atomic_set(&running, 1);
	k_sem_give(&run_sem);

	LOG_INF("run start: %s payload=%u limit_kind=%u limit=%u",
		run_method == TPUT_METHOD_INDICATE ? "indicate" : "notify",
		run_payload_len, run_limit_kind, run_limit);

	return 0;
}

/* Control: [op][method][limit_kind][rsv][payload_len:2][limit:4] */
static ssize_t ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset,
			  uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != TPUT_CTRL_CMD_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	const uint8_t *p = buf;
	uint8_t op = p[0];
	uint8_t method = p[1];
	uint8_t limit_kind = p[2];
	uint16_t payload_len = sys_get_le16(&p[4]);
	uint32_t limit = sys_get_le32(&p[6]);

	switch (op) {
	case TPUT_OP_STOP:
		atomic_set(&running, 0);
		LOG_INF("run stop requested");
		return len;

	case TPUT_OP_RESET_STATS:
		if (atomic_get(&running)) {
			LOG_WRN("reset rejected: run in progress");
			return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
		}
		reset_stats();
		LOG_INF("stats reset");
		return len;

	case TPUT_OP_START_TX:
		break;

	default:
		LOG_WRN("unknown op %u", op);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	if (method > TPUT_METHOD_INDICATE ||
	    limit_kind > TPUT_LIMIT_BYTES) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (payload_len > TPUT_MAX_PAYLOAD) {
		LOG_WRN("payload_len %u exceeds max %u", payload_len,
			TPUT_MAX_PAYLOAD);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (limit_kind != TPUT_LIMIT_NONE && limit == 0) {
		LOG_WRN("limit_kind %u needs a non-zero limit", limit_kind);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	if (atomic_get(&running)) {
		LOG_WRN("start rejected: already running");
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	run_method = method;
	run_limit_kind = limit_kind;
	run_limit = limit;
	req_payload_len = payload_len;

	if (start_run() != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	return len;
}

BT_GATT_SERVICE_DEFINE(tput_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_TAG_TPUT_SVC),

	BT_GATT_CHARACTERISTIC(BT_UUID_TAG_TPUT_DATA,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP |
				       BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_NOTIFY |
				       BT_GATT_CHRC_INDICATE,
			       BT_GATT_PERM_WRITE, NULL, data_write, NULL),
	BT_GATT_CCC(data_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(BT_UUID_TAG_TPUT_CTRL,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       ctrl_read, ctrl_write, NULL),
);

static void data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	data_ccc_value = value;
	data_subscribed = (value & (BT_GATT_CCC_NOTIFY | BT_GATT_CCC_INDICATE)) != 0;

	LOG_DBG("data CCC = 0x%04x", value);

	/* Unsubscribing mid-run ends it rather than spinning on -EINVAL. */
	if (!data_subscribed && atomic_get(&running)) {
		atomic_set(&running, 0);
		LOG_INF("run stopped: client unsubscribed");
	}
}

/* Called when a notification's buffer has been handed to the controller. */
static void notify_sent(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);

	k_sem_give(&tx_credits);
}

static void indicate_confirmed(struct bt_conn *conn,
			       struct bt_gatt_indicate_params *params,
			       uint8_t err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (err) {
		stats.tx_errors++;
	}

	k_sem_give(&indicate_done);
}

/* Send one notification, blocking until a TX buffer credit is free. Returns 0
 * when the packet was accepted by the stack.
 */
static int send_notify(void)
{
	struct bt_gatt_notify_params params = {
		.attr = &tput_svc.attrs[ATTR_DATA_VAL],
		.data = filler,
		.len = run_payload_len,
		.func = notify_sent,
	};
	struct bt_conn *conn = ble_peripheral_conn();
	int err;

	if (!conn) {
		return -ENOTCONN;
	}

	/* Wait for a free buffer rather than sleeping a fixed interval on
	 * -ENOMEM: an arbitrary sleep would throttle the rate we are measuring.
	 */
	if (k_sem_take(&tx_credits, K_MSEC(500)) != 0) {
		return -EAGAIN;
	}

	err = bt_gatt_notify_cb(conn, &params);
	if (err) {
		/* No completion callback will run, so return the credit. */
		k_sem_give(&tx_credits);
	}

	return err;
}

/* Send one indication and wait for the peer's confirmation. Only one may be
 * outstanding per connection, so this is inherently serialised.
 */
static int send_indicate(void)
{
	/* Must outlive the call: the stack keeps a reference until confirmed. */
	static struct bt_gatt_indicate_params params;
	struct bt_conn *conn = ble_peripheral_conn();
	int err;

	if (!conn) {
		return -ENOTCONN;
	}

	params.attr = &tput_svc.attrs[ATTR_DATA_VAL];
	params.func = indicate_confirmed;
	params.destroy = NULL;
	params.data = filler;
	params.len = run_payload_len;

	err = bt_gatt_indicate(conn, &params);
	if (err) {
		return err;
	}

	if (k_sem_take(&indicate_done, K_MSEC(5000)) != 0) {
		LOG_WRN("indication confirmation timed out");
		return -ETIMEDOUT;
	}

	return 0;
}

static bool limit_reached(int64_t start_ms)
{
	switch (run_limit_kind) {
	case TPUT_LIMIT_DURATION:
		return (k_uptime_get() - start_ms) >= (int64_t)run_limit;
	case TPUT_LIMIT_BYTES:
		return stats.tx_bytes >= run_limit;
	default:
		return false;
	}
}

static void tput_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		int64_t start_ms;

		k_sem_take(&run_sem, K_FOREVER);
		start_ms = k_uptime_get();

		while (atomic_get(&running)) {
			int err;

			if (!ble_peripheral_conn() || !data_subscribed) {
				LOG_INF("run stopped: link or subscription gone");
				break;
			}

			err = (run_method == TPUT_METHOD_INDICATE)
				      ? send_indicate()
				      : send_notify();

			if (err == 0) {
				stats.tx_packets++;
				stats.tx_bytes += run_payload_len;
			} else if (err == -ENOTCONN) {
				break;
			} else {
				stats.tx_errors++;
			}

			stats.tx_elapsed_ms =
				(uint32_t)(k_uptime_get() - start_ms);

			if (limit_reached(start_ms)) {
				break;
			}
		}

		stats.tx_elapsed_ms = (uint32_t)(k_uptime_get() - start_ms);
		atomic_set(&running, 0);

		LOG_INF("run done: %u packets, %u bytes, %u errors in %u ms",
			stats.tx_packets, stats.tx_bytes, stats.tx_errors,
			stats.tx_elapsed_ms);
	}
}

K_THREAD_DEFINE(tput_tid, CONFIG_APP_TPUT_THREAD_STACK_SIZE, tput_thread, NULL,
		NULL, NULL, CONFIG_APP_TPUT_THREAD_PRIO, 0, 0);

int tput_init(void)
{
	for (size_t i = 0; i < sizeof(filler); i++) {
		filler[i] = (uint8_t)i;
	}

	LOG_INF("Throughput Service ready (max payload %u bytes)",
		TPUT_MAX_PAYLOAD);
	return 0;
}
