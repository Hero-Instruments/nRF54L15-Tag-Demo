/*
 * Copyright (c) 2026 onceLabs
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * OTA / DFU housekeeping.
 *
 * The transport is the SMP-over-Bluetooth service configured in prj.conf, and
 * the manual slot commands (`mcuboot`, `mcumgr image ...`) come from Zephyr's
 * MCUBOOT_SHELL / MCUMGR_GRP_IMG. This module only adds the two things those
 * don't cover:
 *
 *  - Self-confirmation. MCUboot (swap-using-move) boots a freshly uploaded
 *    image as a *test* image and reverts to the previous one on the next reset
 *    unless the image confirms itself. The tag has no screen and may be far
 *    from a host, so it confirms on its own after APP_DFU_CONFIRM_DELAY_S of
 *    uptime. Anything that resets the device before then (fault, watchdog,
 *    power loss) leaves the image unconfirmed and MCUboot rolls back.
 *
 *  - Quiescing Channel Sounding during an upload: writing the image into RRAM
 *    stalls the CPU in bursts, which upsets CS subevent timing.
 */
#include "dfu.h"

#include <zephyr/app_version.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#include <zephyr/storage/flash_map.h>

#if IS_ENABLED(CONFIG_APP_DFU_PAUSE_CS)
#include "cs_shared.h"
#endif

LOG_MODULE_REGISTER(app_dfu, CONFIG_LOG_DEFAULT_LEVEL);

static bool upload_in_progress;

static void confirm_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	err = boot_write_img_confirmed();
	if (err) {
		LOG_ERR("failed to confirm running image (%d) — MCUboot will "
			"revert on the next reset", err);
		return;
	}

	LOG_INF("running image confirmed; update is now permanent");
}

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_work_handler);

static enum mgmt_cb_return img_event_cb(uint32_t event, enum mgmt_cb_return prev_status,
					int32_t *rc, uint16_t *group, bool *abort_more,
					void *data, size_t data_size)
{
	ARG_UNUSED(prev_status);
	ARG_UNUSED(rc);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);

	switch (event) {
	case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
		upload_in_progress = true;
		LOG_INF("image upload started");
#if IS_ENABLED(CONFIG_APP_DFU_PAUSE_CS)
		if (cs_is_running()) {
			LOG_INF("stopping Channel Sounding for the duration of the upload");
			(void)cs_stop();
		}
#endif
		break;

	case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
		/* Either the upload was abandoned, or it finished and the host
		 * is about to reset us into the new image — nothing to restart
		 * in the latter case, and the role is re-chosen by button in
		 * the former.
		 */
		upload_in_progress = false;
		LOG_INF("image upload stopped");
		break;

	case MGMT_EVT_OP_IMG_MGMT_DFU_PENDING:
		LOG_INF("image marked for test — reset to swap it in");
		break;

	case MGMT_EVT_OP_IMG_MGMT_DFU_CONFIRMED:
		/* A host confirmed the image before our timer got to it. */
		(void)k_work_cancel_delayable(&confirm_work);
		LOG_INF("image confirmed by host");
		break;

	default:
		break;
	}

	return MGMT_CB_OK;
}

static struct mgmt_callback img_callback = {
	.callback = img_event_cb,
	.event_id = MGMT_EVT_OP_IMG_MGMT_ALL,
};

static void log_running_image(void)
{
	struct mcuboot_img_header header;
	int err;

	err = boot_read_bank_header(boot_fetch_active_slot(), &header, sizeof(header));
	if (err) {
		LOG_WRN("could not read the running image header (%d)", err);
		LOG_INF("app version " APP_VERSION_STRING);
		return;
	}

	LOG_INF("running image %u.%u.%u+%u (%u bytes), app version " APP_VERSION_STRING,
		header.h.v1.sem_ver.major, header.h.v1.sem_ver.minor,
		header.h.v1.sem_ver.revision, header.h.v1.sem_ver.build_num,
		header.h.v1.image_size);
}

int dfu_init(void)
{
	log_running_image();

	mgmt_callback_register(&img_callback);

	if (boot_is_img_confirmed()) {
		return 0;
	}

	if (CONFIG_APP_DFU_CONFIRM_DELAY_S == 0) {
		confirm_work_handler(NULL);
		return 0;
	}

	LOG_INF("running an unconfirmed (test) image; confirming in %d s if we stay up",
		CONFIG_APP_DFU_CONFIRM_DELAY_S);
	k_work_schedule(&confirm_work, K_SECONDS(CONFIG_APP_DFU_CONFIRM_DELAY_S));

	return 0;
}

bool dfu_in_progress(void)
{
	return upload_in_progress;
}
