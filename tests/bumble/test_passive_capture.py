"""Phase-1 passive capture tests.

Operator drives the keyboard manually for each scenario. The harness
records HOG notifies and asserts shape/diff against a golden JSONL.

Run interactively:

    uv run pytest tests/bumble/test_passive_capture.py -v -s

Refresh goldens (after firmware behavior intentionally changes):

    uv run pytest tests/bumble/test_passive_capture.py -v -s --update-golden

Tests are SKIPPED if BUMBLE_HCI / ZMK_BLE_ADDR / a USB BT controller
are missing — keeps `pytest` green in pure-CI / no-hardware environments.
"""

from __future__ import annotations

import asyncio
from pathlib import Path

import pytest

from conftest import KeyboardCapture, drain

pytestmark = pytest.mark.asyncio


def _diff(actual: list, golden: list) -> str | None:
    if len(actual) != len(golden):
        return f"notify count: actual={len(actual)} golden={len(golden)}"
    for i, (a, g) in enumerate(zip(actual, golden)):
        if a.handle != g.handle or a.data != g.data:
            return (f"notify[{i}] differs: "
                    f"actual=(h={a.handle:#x} d={a.data.hex()}) "
                    f"golden=(h={g.handle:#x} d={g.data.hex()})")
    return None


async def _record_scenario(prompt: str, capture: KeyboardCapture,
                           hold_seconds: float) -> None:
    print(f"\n>>> {prompt}\n>>> recording for {hold_seconds:.1f}s ...")
    await drain(hold_seconds)
    print(f">>> captured {len(capture.notifies)} notifies")


@pytest.mark.parametrize("scenario,hold_s", [
    ("idle_quiet",   3.0),
    ("type_hello",   5.0),
    ("mouse_burst",  4.0),
])
async def test_passive_capture(scenario, hold_s, keyboard_capture,
                               golden_dir: Path, update_golden: bool):
    """Capture HOG notifies for a manually-driven scenario; diff vs golden."""
    prompts = {
        "idle_quiet":  "Do nothing. Verify no spurious notifies.",
        "type_hello":  "Type 'hello' once.",
        "mouse_burst": "Move trackpoint in a small circle for 3s.",
    }
    await _record_scenario(prompts[scenario], keyboard_capture, hold_s)

    golden_path = golden_dir / f"{scenario}.jsonl"

    if update_golden or not golden_path.exists():
        keyboard_capture.dump_jsonl(golden_path)
        if not golden_path.exists():
            pytest.skip(f"golden written: {golden_path.name}")
        return

    expected = KeyboardCapture.load_jsonl(golden_path)
    diff = _diff(keyboard_capture.notifies, expected)
    assert diff is None, diff


async def test_idle_no_notifies(keyboard_capture):
    """Sanity: a freshly-connected idle keyboard sends no HOG notifies."""
    await drain(2.0)
    assert keyboard_capture.notifies == [], (
        f"expected no idle notifies, got {len(keyboard_capture.notifies)}: "
        + ", ".join(f"h={n.handle:#x} d={n.data.hex()}"
                    for n in keyboard_capture.notifies[:5]))
