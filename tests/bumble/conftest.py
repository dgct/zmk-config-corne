"""Bumble fixtures for ZMK passive-capture tests.

Phase 1: connect as a Bumble central to the keyboard, subscribe to
HOG report characteristics, yield a `KeyboardCapture` that records
all incoming notifies.

Hardware required: USB BT controller (Bumble cannot drive macOS
CoreBluetooth). Set BUMBLE_HCI=usb:0 and ZMK_BLE_ADDR.
"""

from __future__ import annotations

import asyncio
import json
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import AsyncIterator

import pytest
import pytest_asyncio

# Bumble imports are deferred so collection works without bumble installed.
# Tests are skipped at runtime if env vars or hardware are missing.

GOLDEN_DIR = Path(__file__).parent / "golden"
HCI = os.environ.get("BUMBLE_HCI")
TARGET_ADDR = os.environ.get("ZMK_BLE_ADDR")
PAIR_DIR = Path(os.environ.get("BUMBLE_PAIR_DIR", Path.home() / ".bumble" / "zmk-tests"))


def _require_hardware():
    if not HCI or not TARGET_ADDR:
        pytest.skip("Set BUMBLE_HCI and ZMK_BLE_ADDR to run Bumble tests")


@dataclass
class CapturedNotify:
    ts_ms: float
    handle: int
    data: bytes

    def to_jsonable(self) -> dict:
        return {"ts_ms": round(self.ts_ms, 3), "handle": self.handle,
                "data": self.data.hex()}

    @classmethod
    def from_jsonable(cls, d: dict) -> "CapturedNotify":
        return cls(ts_ms=d["ts_ms"], handle=d["handle"],
                   data=bytes.fromhex(d["data"]))


@dataclass
class KeyboardCapture:
    notifies: list[CapturedNotify] = field(default_factory=list)
    _start_ms: float = field(default_factory=lambda: time.monotonic() * 1000)

    def record(self, handle: int, data: bytes) -> None:
        self.notifies.append(CapturedNotify(
            ts_ms=time.monotonic() * 1000 - self._start_ms,
            handle=handle, data=bytes(data)))

    def dump_jsonl(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as f:
            for n in self.notifies:
                f.write(json.dumps(n.to_jsonable()) + "\n")

    @classmethod
    def load_jsonl(cls, path: Path) -> list[CapturedNotify]:
        with path.open() as f:
            return [CapturedNotify.from_jsonable(json.loads(line))
                    for line in f if line.strip()]


@pytest_asyncio.fixture
async def keyboard_capture() -> AsyncIterator[KeyboardCapture]:
    """Connect to ZMK keyboard, subscribe to HOG notifies, yield a recorder.

    Skipped if hardware/env are missing.
    """
    _require_hardware()
    pytest.importorskip("bumble")

    from bumble.device import Device  # noqa: WPS433
    from bumble.transport import open_transport_or_link  # noqa: WPS433
    from bumble.hci import Address  # noqa: WPS433
    from bumble.gatt_client import CharacteristicProxy  # noqa: WPS433

    PAIR_DIR.mkdir(parents=True, exist_ok=True)
    capture = KeyboardCapture()

    async with await open_transport_or_link(HCI) as (hci_source, hci_sink):
        device = Device.with_hci("zmk-tests", "F0:F1:F2:F3:F4:F5",
                                 hci_source, hci_sink)
        device.keystore_path = str(PAIR_DIR / "keys.json")
        await device.power_on()

        connection = await device.connect(Address(TARGET_ADDR))
        try:
            await connection.pair()
            await connection.discover_services()

            # HOG = 0x1812. Subscribe to every notifiable characteristic
            # in that service so we capture keyboard, consumer, and mouse.
            HOG_UUID = 0x1812
            services = [s for s in connection.gatt_client.services
                        if int(s.uuid) == HOG_UUID]
            for service in services:
                await connection.gatt_client.discover_characteristics(service=service)
                for ch in service.characteristics:
                    if not ch.properties & CharacteristicProxy.PROPERTY_NOTIFY:
                        continue
                    h = ch.handle

                    def make_cb(handle: int):
                        return lambda data: capture.record(handle, data)

                    await ch.subscribe(make_cb(h))

            yield capture
        finally:
            try:
                await connection.disconnect()
            except Exception:
                pass


@pytest.fixture
def golden_dir() -> Path:
    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    return GOLDEN_DIR


def pytest_addoption(parser):
    parser.addoption("--update-golden", action="store_true",
                     help="Overwrite golden traces with current capture.")


@pytest.fixture
def update_golden(request) -> bool:
    return bool(request.config.getoption("--update-golden"))


async def drain(seconds: float) -> None:
    """Yield to the event loop for `seconds` to let notifies arrive."""
    await asyncio.sleep(seconds)
