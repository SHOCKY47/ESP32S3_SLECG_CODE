"""BLE GATT transport via bleak."""

from __future__ import annotations

import asyncio
import logging
import threading
from dataclasses import dataclass
from typing import Callable

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

from slecg_host.protocol.constants import (
    BLE_CHAR_CMD_RX_UUID,
    BLE_CHAR_CMD_TX_UUID,
    BLE_DEVICE_NAME,
    BLE_SVC_UUID,
)

from .base import DeviceInfo, Transport

logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class BleDeviceInfo(DeviceInfo):
    address: str = ""


class BleTransport(Transport):
    def __init__(self) -> None:
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._client: BleakClient | None = None
        self._callback: Callable[[bytes], None] | None = None
        self._connected = False
        self._scanned: list[BleDeviceInfo] = []
        self._notify_count = 0

    def list_devices(self) -> list[DeviceInfo]:
        return list(self._scanned)

    def scan(self, timeout: float = 5.0) -> list[BleDeviceInfo]:
        logger.info("BLE scan timeout=%.1fs", timeout)
        return self._run_coro(self._scan_async(timeout))

    def open(self, device_id: str) -> None:
        logger.info("BLE 连接: %s", device_id)
        self.close()
        self._run_coro(self._connect_async(device_id))
        logger.info("BLE 已连接并开启 Notify: %s", device_id)

    def close(self) -> None:
        if self._loop and self._loop.is_running():
            try:
                self._run_coro(self._disconnect_async())
            except Exception as exc:  # noqa: BLE001
                logger.warning("BLE disconnect 异常: %s", exc)
            try:
                self._loop.call_soon_threadsafe(self._loop.stop)
            except Exception:  # noqa: BLE001
                pass
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self._thread = None
        self._loop = None
        self._client = None
        self._connected = False
        logger.info("BLE 已关闭 (notify 累计 %d)", self._notify_count)

    def is_open(self) -> bool:
        return self._connected

    def write(self, data: bytes) -> None:
        if not self._connected:
            raise RuntimeError("BLE not connected")
        logger.info("BLE 写入 %d 字节: %s", len(data), data[:16].hex(" "))
        self._run_coro(self._write_async(data))

    def set_data_callback(self, callback: Callable[[bytes], None] | None) -> None:
        self._callback = callback

    def _ensure_loop(self) -> asyncio.AbstractEventLoop:
        if self._loop is None or not self._loop.is_running():
            ready = threading.Event()

            def run_loop() -> None:
                self._loop = asyncio.new_event_loop()
                asyncio.set_event_loop(self._loop)
                ready.set()
                self._loop.run_forever()

            self._thread = threading.Thread(target=run_loop, daemon=True, name="ble-loop")
            self._thread.start()
            if not ready.wait(timeout=2.0):
                raise RuntimeError("BLE 事件循环启动超时")
        return self._loop  # type: ignore[return-value]

    def _run_coro(self, coro):  # noqa: ANN001, ANN201
        loop = self._ensure_loop()
        future = asyncio.run_coroutine_threadsafe(coro, loop)
        return future.result(timeout=30.0)

    async def _scan_async(self, timeout: float) -> list[BleDeviceInfo]:
        discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
        found: list[BleDeviceInfo] = []
        items = discovered.values() if isinstance(discovered, dict) else ((d, None) for d in discovered)
        for dev, adv in items:
            if self._is_slecg_device(dev, adv):
                name = (adv.local_name if adv and adv.local_name else None) or dev.name or BLE_DEVICE_NAME
                label = f"{name} ({dev.address})"
                info = BleDeviceInfo(id=dev.address, label=label, address=dev.address)
                found.append(info)
        self._scanned = found
        logger.info("BLE 扫描命中 %d 台 SLECG 设备", len(found))
        return found

    def _is_slecg_device(self, dev: BLEDevice, adv=None) -> bool:  # noqa: ANN001
        name = (adv.local_name if adv and adv.local_name else dev.name or "").upper()
        if BLE_DEVICE_NAME.upper() in name or name == BLE_DEVICE_NAME.upper():
            return True
        uuids: set[str] = set()
        if adv and adv.service_uuids:
            uuids.update(u.lower() for u in adv.service_uuids)
        meta = getattr(dev, "metadata", None) or {}
        uuids.update(u.lower() for u in (meta.get("uuids") or []))
        return BLE_SVC_UUID.lower() in uuids

    async def _connect_async(self, address: str) -> None:
        self._client = BleakClient(address)
        await self._client.connect()
        logger.info("GATT connected, 开启 Notify %s", BLE_CHAR_CMD_TX_UUID)
        await self._client.start_notify(BLE_CHAR_CMD_TX_UUID, self._on_notify)
        self._connected = True
        self._notify_count = 0

    def _on_notify(self, _handle: int, data: bytearray) -> None:
        self._notify_count += 1
        if self._notify_count <= 3 or self._notify_count % 20 == 0:
            logger.info(
                "BLE Notify #%d: %d B head=%s",
                self._notify_count,
                len(data),
                bytes(data[:8]).hex(" "),
            )
        if self._callback:
            self._callback(bytes(data))

    async def _write_async(self, data: bytes) -> None:
        assert self._client is not None
        await self._client.write_gatt_char(BLE_CHAR_CMD_RX_UUID, data, response=False)

    async def _disconnect_async(self) -> None:
        if self._client and self._client.is_connected:
            try:
                await self._client.stop_notify(BLE_CHAR_CMD_TX_UUID)
            except Exception:  # noqa: BLE001
                pass
            await self._client.disconnect()
        self._connected = False
