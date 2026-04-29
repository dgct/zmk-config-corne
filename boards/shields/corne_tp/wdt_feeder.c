/*
 * Bug 6 watchdog + reset-reason logger.
 *
 * Catches freezes that CONFIG_RESET_ON_FATAL_ERROR cannot:
 *   - System workqueue starvation / livelock
 *   - BLE host stack stall with CPU still alive
 *   - Any IRQ-off deadlock (HW WDT runs on dedicated clock)
 *
 * Mechanism:
 *   - task_wdt channel fed every 500ms from system workqueue
 *   - HW WDT fallback fires if task_wdt itself stops being serviced
 *   - On boot, log the reset reason from HWINFO so we know if a freeze
 *     happened (RESET_REASON: WATCHDOG) vs. a clean USB power cycle
 *
 * Wake grace (automotive ECU / Linux touch_softlockup_watchdog pattern):
 *   On IDLE→ACTIVE transition, immediately feed the watchdog and reset
 *   the periodic feed timer. This absorbs the burst of synchronous work
 *   (conn-param negotiation, HFXO ramp, display redraw, settings save)
 *   that can starve the syswq for several hundred milliseconds during
 *   wake. The watchdog remains armed — a genuine freeze still trips it.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/logging/log.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

/* Hardcode INF so logs always show regardless of CONFIG_ZMK_LOG_LEVEL
 * visibility from this compilation unit. */
LOG_MODULE_REGISTER(wdt_feeder, LOG_LEVEL_INF);

/* Feed twice per second; declare a freeze if the syswq misses ~16 feed
 * windows.  Bumped from 1000/5000 to 500/8000 (P1 Apr 2026) — the old
 * 4 s grace window was tight enough that a settings-save + LFS commit
 * coinciding with a HoG retry burst could false-positive.
 */
#define WDT_FEED_INTERVAL_MS  500
#define WDT_TASK_TIMEOUT_MS   8000

static int wdt_channel = -1;

static void wdt_feed_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wdt_feed_work, wdt_feed_work_cb);

static void wdt_feed_work_cb(struct k_work *work) {
    if (wdt_channel >= 0) {
        task_wdt_feed(wdt_channel);
    }
    k_work_schedule(&wdt_feed_work, K_MSEC(WDT_FEED_INTERVAL_MS));
}

static const char *reset_cause_str(uint32_t cause) {
    if (cause == 0) {
        return "NONE";
    }
    if (cause & RESET_WATCHDOG) {
        return "WATCHDOG (FREEZE DETECTED)";
    }
    if (cause & RESET_SOFTWARE) {
        return "SOFTWARE (fault or sys_reboot)";
    }
    if (cause & RESET_BROWNOUT) {
        return "BROWNOUT";
    }
    if (cause & RESET_POR) {
        return "POWER-ON";
    }
    if (cause & RESET_PIN) {
        return "PIN (reset button / nice!nano enumeration)";
    }
    if (cause & RESET_DEBUG) {
        return "DEBUG";
    }
    return "OTHER";
}

static int wdt_feeder_init(void) {
    uint32_t cause = 0;
    int err = hwinfo_get_reset_cause(&cause);
    if (err == 0) {
        LOG_WRN("RESET REASON: 0x%08x (%s)", cause, reset_cause_str(cause));
        hwinfo_clear_reset_cause();
    } else {
        LOG_WRN("RESET REASON: hwinfo_get_reset_cause failed (%d)", err);
    }

    wdt_channel = task_wdt_add(WDT_TASK_TIMEOUT_MS, NULL, NULL);
    if (wdt_channel < 0) {
        LOG_ERR("task_wdt_add failed: %d", wdt_channel);
        return wdt_channel;
    }

#if DT_HAS_CHOSEN(zephyr_task_wdt)
    LOG_INF("Watchdog armed: task=%dms, feed=%dms, channel=%d, HW fallback=YES (%s)",
            WDT_TASK_TIMEOUT_MS, WDT_FEED_INTERVAL_MS, wdt_channel,
            DT_NODE_FULL_NAME(DT_CHOSEN(zephyr_task_wdt)));
#else
    LOG_ERR("Watchdog armed: task=%dms, feed=%dms, channel=%d, HW fallback=NO "
            "(zephyr,task-wdt chosen missing - software-only mode!)",
            WDT_TASK_TIMEOUT_MS, WDT_FEED_INTERVAL_MS, wdt_channel);
#endif

    k_work_schedule(&wdt_feed_work, K_MSEC(WDT_FEED_INTERVAL_MS));
    return 0;
}

SYS_INIT(wdt_feeder_init, APPLICATION, 90);

/*
 * Wake grace: feed the watchdog immediately on IDLE→ACTIVE transition.
 *
 * During wake, the syswq is flooded with synchronous work:
 *   - bt_conn_le_param_update() (deep-idle unwrap: multi-CI negotiation)
 *   - HFXO re-lock (~360µs blocking)
 *   - constlat re-enable
 *   - display redraw (SPI transfers)
 *   - settings_save (LFS flash erase: ~85ms/page, partial IRQ masking)
 *
 * Compound worst case: ~500-800ms syswq stall. The periodic 500ms feed
 * work item can miss 1-2 windows. With 8s timeout that's still safe in
 * theory (16 windows), but if the wake burst *starts* near a feed
 * deadline, the effective margin shrinks. Feeding here resets the clock
 * at the beginning of the burst — same pattern as:
 *   - Automotive ISO 26262 "startup supervision" grace period
 *   - Linux touch_softlockup_watchdog() before known-long ops
 *   - IEC 61131 PLC mode-transition watchdog widening
 */
static int wdt_activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state == ZMK_ACTIVITY_ACTIVE && wdt_channel >= 0) {
        /* Feed immediately — resets the 8s deadline */
        task_wdt_feed(wdt_channel);
        /* Reschedule the periodic feeder from now, so it doesn't fire
         * redundantly or miss its next window during the burst */
        k_work_reschedule(&wdt_feed_work, K_MSEC(WDT_FEED_INTERVAL_MS));
        LOG_DBG("Wake grace: watchdog fed on IDLE->ACTIVE");
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(wdt_activity, wdt_activity_listener);
ZMK_SUBSCRIPTION(wdt_activity, zmk_activity_state_changed);
