# Corne TP Freeze — Root Cause Analysis (2026-04-18)

## Summary

Both halves froze (no key input, mouse still moved briefly, bongo cat
animation stopped, peripheral half BLE-disconnected ~5 s later) during a
multi-hour soak test of branch `feat/input-split-coalesce` (head
`13d2b003`) with diagnostics enabled.

**This is not a stack overflow or a hard fault.** It is a **system
work-queue deadlock on the central** triggered while processing a
momentary-layer activation that resulted in a display redraw against
the nice!view (Sharp memory LCD, `ls0xx` driver).

The peripheral half was healthy throughout. It only "died" because the
central stopped responding to BLE pings, so the link timed out (reason
`0x08`, supervision timeout).

---

## Captured artefacts

- Raw logs and forensic extract:
  [logs/freeze-capture-20260418-172601/](../logs/freeze-capture-20260418-172601)
  - [port101.log](../logs/freeze-capture-20260418-172601/port101.log) — **central (LEFT, with nice!view)** full session
  - [port2101.log](../logs/freeze-capture-20260418-172601/port2101.log) — **peripheral (RIGHT, trackpoint)** full session

  > Side mapping verified from `boards/shields/corne_tp/Kconfig.defconfig` (`SHIELD_CORNE_TP_LEFT` ⇒ `ZMK_SPLIT_ROLE_CENTRAL=y`) and from the log content itself: only `port101.log` carries `split_central_notify_func` and `ls0xx:` lines; `port2101.log` carries `split_peripheral_listener`.
  - [analysis.txt](../logs/freeze-capture-20260418-172601/analysis.txt) — 27 k-line extract (windows around the freeze, all non-`<dbg>` lines, last 3 thread-analyzer dumps per half)
  - [extract.sh](../logs/freeze-capture-20260418-172601/extract.sh) — repeatable extraction script

---

## Timeline (board-clock = central; host-clock for cross-reference)

| Board time     | Half       | Event                                                                                  |
| -------------- | ---------- | -------------------------------------------------------------------------------------- |
| `02:17:37.005` | central    | Last `Thread analyze:` dump (the cadence is 30 s)                                      |
| `02:17:57.302` | peripheral | (cross-noted) `02:17:27` peripheral first sees `bt_conn: not connected`                |
| `02:18:10.759` | central    | `split_central_notify_func: [NOTIFICATION] data 0x2001af2a length 16` (coalesced batch) |
| `02:18:10.760,040` | central | `position_state_changed_listener: 39 bubble (no undecided hold_tap active)`            |
| `02:18:10.760,131` | central | `zmk_keymap_apply_position_state: layer_id: 0 position: 39, binding name: momentary_layer` |
| `02:18:10.760,192` | central | `mo_keymap_binding_pressed: position 39 layer 4`                                       |
| `02:18:10.760,223` | central | `set_layer_state: layer_changed: layer 4 state 1 locked 0`                             |
| `02:18:10.760,284` | central | `<wrn> ls0xx: Unsupported` ← **last log line from anything driven by sys_workq or display thread** |
| `02:18:11+`        | central | Mouse / `peripheral_input_event_notify_cb` / `apply_config` keep streaming (these run on the BT RX thread, not sys_workq) |
| `02:18:11+`        | central | `Thread analyze:` at expected `02:18:07` does **not** appear → sys_workq is dead       |
| `02:18:16`         | central | Mouse logging eventually stops too                                                     |
| `02:17:27.559`     | peripheral | `<wrn> bt_conn: conn 0x20004490: not connected` × 2                                  |
| `02:17:27.560`     | peripheral | `<dbg> zmk: disconnected: Disconnected from DA:7B:4B:81:FA:2B (random) (reason 0x08)` |
| `02:17:35`–`02:24` | peripheral | Continues to scan, debounce, send key events. Every send fails: `send_position_state_callback: Error notifying -128` (= `-ENOTCONN`) |

The peripheral keeps scanning until power off.

---

## Evidence the freeze is a sys_workq deadlock, not a crash

1. **No fault trace.** No `BUS FAULT`, `MPU FAULT`, `Stack overflow`,
   `assertion failed`, `panic`, `oops`, etc. anywhere on either half.
2. **No reboot.** Central's board uptime continues monotonically; the
   `02:18:11` ‹dbg› messages still bear the same boot-relative
   timestamp continuum.
3. **Other threads keep running on central.** PS/2 trackpoint ISR,
   `peripheral_input_event_notify_cb` (BT RX), HID `apply_config` /
   `scale_val` / `zmk_hid_mouse_movement_set` continue for ~5 seconds
   after sys_workq dies. These run on the BT RX thread / interrupt
   contexts, **not** on sys_workq.
4. **Log thread is alive.** Logs are still being drained — proving the
   logging back-end and USB CDC ACM are fine. The reason no further
   `<dbg>` messages appear from sys_workq or the display dedicated
   thread is that they are blocked, not because logs are dropped.
5. **`Thread analyze:` stops firing exactly on the 30 s cadence.** It
   is scheduled via `k_work_schedule` on sys_workq. Its silence is the
   single cleanest indicator that sys_workq is deadlocked.
6. **Peripheral is unrelated.** Peripheral's last `Thread analyze`
   block at `02:24:11` shows healthy stacks, idle thread at 97 % CPU,
   and continued kscan event flow. Its only problem is the BLE link
   has dropped to the (frozen) central.

### Suspected sys_workq thread

Last central `Thread analyze:` at `02:17:37`:

```
0x20008a70 : STACK: unused 2360 usage 1736 / 4096 (42 %); CPU: 0 %   ← matches CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
0x20002428 : STACK: unused 5040 usage 3152 / 8192 (38 %); CPU: 0 %   ← matches CONFIG_ZMK_DISPLAY_DEDICATED_THREAD_STACK_SIZE=8192
0x20002618 : STACK: unused  120 usage  648 /  768 (84 %); CPU: 3 %   ← high HWM, busy; identity unknown without THREAD_NAME
0x20008858 : STACK: unused  320 usage   64 /  384 (16 %); CPU: 94 %  ← idle thread (healthy)
ISR0       : STACK: unused 1216 usage  832 / 2048 (40 %)
```

Without `CONFIG_THREAD_NAME=y` we can't put names on the addresses, but
sizes pin them down: the 4096-byte stack is sys_workq, the 8192-byte
stack is the ZMK dedicated display work-queue thread, and `0x20002618`
(0.768 KB stack at 84 % HWM) is the unidentified hot thread we have
been watching all session.

---

## Why `ls0xx: Unsupported` is the last log line

The warning text is emitted by upstream Zephyr at
[`drivers/display/ls0xx.c` lines 78-88](https://github.com/dgct/zephyr/tree/main/drivers/display/ls0xx.c#L78-L88):

```c
static int ls0xx_blanking_off(const struct device *dev)
{
#if DT_INST_NODE_HAS_PROP(0, disp_en_gpios)
    return gpio_pin_set_dt(&config->disp_en_gpio, 1);
#else
    LOG_WRN("Unsupported");
    return -ENOTSUP;
#endif
}
```

The nice!view shield does **not** wire `disp-en-gpios` for its
LS013B7DH05 panel, so this warning fires every time something calls
`display_blanking_off()`. It has been firing harmlessly all session.

What is special about *this* invocation: it is the immediate
predecessor of the freeze. ZMK calls `display_blanking_off()` on the
activity-wake path because we have

```kconfig
CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y
```

So the sequence on the central was:

1. The keyboard had been idle long enough for ZMK to have called
   `display_blanking_on()` (ls0xx logged "Unsupported", returned
   `-ENOTSUP`, no real effect because nice!view stays on).
2. A (split-forwarded) keypress at position 39 arrived.
3. ZMK activity-state changed from idle → active → call
   `display_blanking_off()` (ls0xx logged "Unsupported" again).
4. The very same path then needs to redraw the display because the
   active layer changed (`set_layer_state: layer_changed: layer 4`).
5. The redraw enters LVGL → `lvgl_flush_cb_mono` → `display_write` →
   `ls0xx_write` → `ls0xx_update_display` → `k_sem_take(&ls0xx_bus_sem)`
   → `spi_write_dt(SPI_LOCK_ON | SPI_HOLD_ON_CS)` per line × 128/240.
6. **Something in that path never returns.**

The `<wrn>` line is the last thing logged because everything *after*
that is inside the deadlocked thread; nothing in that thread will ever
emit another log message.

---

## Strongly suspected mechanism

The `ls0xx_update_display` function has a long-standing
"always-release" pattern that is unsafe on the time-out branch:

```c
/* drivers/display/ls0xx.c, ls0xx_update_display */
if (k_sem_take(&ls0xx_bus_sem, K_MSEC(LS0XX_MAX_BUS_WAIT_MSEC)) == 0) {
    err = ls0xx_cmd(dev, write_cmd, sizeof(write_cmd));
    for (...) { err |= spi_write_dt(&config->bus, &line_set); }
    err |= ls0xx_cmd(dev, write_cmd, sizeof(write_cmd));
} else {
    LOG_ERR("memory display semaphore not available - refresh data");
    err = -EBUSY;
}
k_sleep(K_TICKS(LS0XX_BUS_RETURN_DELAY_TICKS));
k_sem_give(&ls0xx_bus_sem);   /* given even when never taken */
spi_release_dt(&config->bus); /* released even when SPI_LOCK_ON never asserted */
```

The Zephyr SPI bus is configured with `SPI_LOCK_ON | SPI_HOLD_ON_CS`
(see [`ls0xx_config`](https://github.com/dgct/zephyr/tree/main/drivers/display/ls0xx.c#L344-L355)).
A mismatched `spi_release_dt()` corrupts the SPI core's lock-owner
tracking. The user's `dgct/zephyr` fork (`fba7337f`, manifest comment
*"v4.1.0+zmk-fixes + ls0xx VCOM sem/SPI release fix (display
freeze)"*) is explicitly aware of this class of bug. Crucially, **the
`ls0xx.c` shipped in `dgct/zephyr` is byte-for-byte identical to
upstream on every relevant line** (`ls0xx_clear`, `ls0xx_update_display`,
the VCOM thread, and `ls0xx_config`'s `SPI_LOCK_ON | SPI_HOLD_ON_CS`
flags) — the unconditional `k_sem_give` + `spi_release_dt` pattern is
still present in all three call sites in the fork. So one of the
following is true:

- the prior fix lives elsewhere (SPI core, nrfx SPIM driver, LVGL
  flush callback) and does not cover this path; or
- the prior fix was intentionally narrower (e.g. an EXTCOMIN VCOM
  toggle race) and the new freeze is a different pathology that just
  shares the same warning line; or
- a regression has reintroduced a stuck-state path, most plausibly via
  the new `feat/input-split-coalesce` work that has changed the
  timing of layer events. Note the **`length 16` coalesced
  notification** immediately preceding the freeze; the prior
  single-event flow used `length 8`.

The coalesced batch causes two `position_state_changed` events to be
delivered back-to-back inside one BT RX dispatch. If the display
redraw triggered by the **first** event is still mid-flight when the
**second** event arrives and tries to re-enter the same code path,
the unsafe release/give pattern can flip state in a way that wedges
the thread permanently.

---

## What is *not* the cause (ruled out by evidence)

| Ruled out                              | Why                                                                                       |
| -------------------------------------- | ----------------------------------------------------------------------------------------- |
| Stack overflow on any monitored thread | All HWMs in last `Thread analyze` are well under 100 %; no canary fault                   |
| MPU / Bus / hard fault                 | No fault trace, no reboot, no `<err>` messages                                            |
| Sleep / power management               | `CONFIG_ZMK_SLEEP=y` with 15 min idle; freeze occurred ~ 9 minutes after a clean keypress |
| Peripheral firmware bug                | Peripheral keeps running normally after the freeze                                        |
| BLE supervision timeout being primary  | Disconnect happened **after** central stopped responding, not before                      |
| USB CDC log throughput                 | Logs continue to flow from non-deadlocked threads                                         |
| `feat/input-split-coalesce` *itself*   | New code is upstream of the deadlock; mostly likely just changes the **timing** that hits it |

---

## Reproducer (likely)

Hammer position 39 (right-thumb middle on the **peripheral** half — `&mo 4` in the default layer; the press is debounced on the right, BLE-forwarded to the left/central, and processed there)
repeatedly while the display is in `BLANK_ON_IDLE`-induced sleep, with
keypresses delivered as coalesced split notifications (length ≥ 16).
With the current build the freeze hit after roughly 2 hours of light
use; without coalescing it is not believed to occur (this needs
confirmation by toggling
`CONFIG_ZMK_SPLIT_BLE_PERIPHERAL_INPUT_COALESCE` off).

---

## Recommended next steps

In rough priority order — each step should produce evidence that
either confirms or refutes the hypothesis above before moving on.

### 1. Make the next freeze identify itself

Add to [config/corne_tp.conf](../config/corne_tp.conf):

```kconfig
# Quiet the dbg firehose so the next freeze isn't buried in mouse logs
CONFIG_LOG_DEFAULT_LEVEL=3       # WRN/ERR only

# Name threads so addresses become readable
CONFIG_THREAD_NAME=y

# Catch silent deadlocks — Zephyr will __ASSERT_NO_MSG and print the
# stuck thread name when sys_workq misses its tick
CONFIG_KERNEL_COHERENCE=n
CONFIG_ASSERT=y
CONFIG_ASSERT_VERBOSE=y

# Surface BLE-stack reasons (peripheral disconnect 0x08 detail, etc.)
CONFIG_BT_LOG_LEVEL_DBG=y
```

### 2. Confirm display is the trigger

Smoke-test build with the display disabled on **central only** for a
day. If no freeze, we have culprit confirmation:

```yaml
# build.yaml — temporarily for the right (central) shield
include:
  - board: nice_nano
    shield: corne_tp_left nice_view_adapter nice_view
    snippet: zmk-usb-logging
  - board: nice_nano
    shield: corne_tp_right          # nothing on the central
    snippet: zmk-usb-logging
```

(left already has the display, right does not — keep the existing
asymmetry; this just keeps it. To actually take the display out of the
picture you have to disable it on the **left/central** half: drop the
`nice_view_adapter nice_view` shields from the left build line.)

Or, with display present, disable blanking-on-idle to take the
activity-wake path out of the picture:

```kconfig
# CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=n
```

### 3. Confirm coalescing is the timing trigger

Without changing the display, build with split-coalesce off:

```kconfig
# CONFIG_ZMK_SPLIT_BLE_PERIPHERAL_INPUT_COALESCE=n   (or whatever the
# branch's symbol is)
```

If the freeze stops, the deadlock is timing-sensitive to back-to-back
layer events, which is a strong signal to fix the ls0xx re-entry path.

### 4. Fix the ls0xx release/give mismatch upstream

Patch `drivers/display/ls0xx.c` `ls0xx_update_display()` so the
`k_sem_give(&ls0xx_bus_sem)` and `spi_release_dt(&config->bus)`
calls are inside the success branch. Carry it locally in
`dgct/zephyr` until upstream accepts. Same fix in `ls0xx_clear()`.

---

## Notes for the next session

- `0x20002618` is still the unidentified high-HWM thread on central
  (it was `0x20002050` on peripheral with the same profile in older
  logs). Step 1 will name it.
- The fix used by `dgct/zephyr` for the prior "display freeze" should
  be located via `git log --oneline drivers/display/ls0xx.c` and
  re-checked against the current freeze; if step 4 is needed it is a
  regression, otherwise the fix needs to extend to the new path.
