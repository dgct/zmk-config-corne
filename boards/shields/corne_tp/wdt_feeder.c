/*
 * Bug 6 watchdog + reset-reason logger.
 *
 * Catches freezes that CONFIG_RESET_ON_FATAL_ERROR cannot:
 *   - System workqueue starvation / livelock
 *   - BLE host stack stall with CPU still alive
 *   - Any IRQ-off deadlock (HW WDT runs on dedicated clock)
 *
 * Mechanism:
 *   - task_wdt channel fed every 1 s from system workqueue
 *   - HW WDT fallback fires if task_wdt itself stops being serviced
 *   - On boot, log the reset reason from HWINFO so we know if a freeze
 *     happened (RESET_REASON: WATCHDOG) vs. a clean USB power cycle
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/logging/log.h>

/* Hardcode INF so logs always show regardless of CONFIG_ZMK_LOG_LEVEL
 * visibility from this compilation unit. */
LOG_MODULE_REGISTER(wdt_feeder, LOG_LEVEL_INF);

#define WDT_FEED_INTERVAL_MS  1000
#define WDT_TASK_TIMEOUT_MS   5000

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
