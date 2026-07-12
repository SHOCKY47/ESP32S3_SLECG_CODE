"""串口传输 — 115200 8N1 被动接收。"""

from __future__ import annotations

import logging
import threading
import time
from typing import Callable

import serial
from serial.tools import list_ports

from slecg_host.protocol.constants import SERIAL_BAUD_RATE

from .base import DeviceInfo, Transport

logger = logging.getLogger(__name__)

# CH340 等 USB-UART 打开端口时常脉冲 DTR/RTS 复位 ESP32，需等待启动完成
_BOOT_SETTLE_S = 1.5


class SerialTransport(Transport):
    def __init__(self) -> None:
        self._port: serial.Serial | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._callback: Callable[[bytes], None] | None = None
        self._lost_callback: Callable[[str], None] | None = None
        self._bytes_received = 0
        self._chunks = 0

    def list_devices(self) -> list[DeviceInfo]:
        devices: list[DeviceInfo] = []
        for port in list_ports.comports():
            label = port.device
            if port.description:
                label = f"{port.device} — {port.description}"
            devices.append(DeviceInfo(id=port.device, label=label))
        logger.info("枚举串口 %d 个: %s", len(devices), [d.id for d in devices])
        return devices

    def set_lost_callback(self, callback: Callable[[str], None] | None) -> None:
        self._lost_callback = callback

    def open(self, device_id: str) -> None:
        self.close()
        logger.info("准备打开串口 %s @ %d 8N1", device_id, SERIAL_BAUD_RATE)
        t0 = time.monotonic()
        try:
            kwargs: dict = {
                "port": device_id,
                "baudrate": SERIAL_BAUD_RATE,
                "bytesize": serial.EIGHTBITS,
                "parity": serial.PARITY_NONE,
                "stopbits": serial.STOPBITS_ONE,
                "timeout": 0.1,
                "write_timeout": 1.0,
                "dsrdtr": False,
                "rtscts": False,
            }
            try:
                self._port = serial.Serial(**kwargs, exclusive=True)
            except TypeError:
                self._port = serial.Serial(**kwargs)
        except serial.SerialException as exc:
            elapsed_ms = (time.monotonic() - t0) * 1000
            logger.error("串口打开失败 %s (%.0f ms): %s", device_id, elapsed_ms, exc)
            raise RuntimeError(
                f"无法打开 {device_id}：{exc}\n\n"
                "常见原因：\n"
                "1. idf_monitor / 串口助手仍占用该端口（请先退出）\n"
                "2. 端口号已变化，请点「刷新列表」后重选\n"
                "3. macOS 权限或 USB 线不稳定"
            ) from exc

        # 强制拉低 DTR/RTS，避免 CH340 自动复位电路持续复位 ESP32
        try:
            self._port.dtr = False
            self._port.rts = False
        except Exception as exc:  # noqa: BLE001
            logger.warning("设置 DTR/RTS=False 失败: %s", exc)

        logger.info(
            "串口硬件已打开: %s (耗时 %.0f ms) — 等待固件启动 %.1fs…",
            device_id,
            (time.monotonic() - t0) * 1000,
            _BOOT_SETTLE_S,
        )
        time.sleep(_BOOT_SETTLE_S)

        # 丢弃复位启动日志（ESP-ROM / bootloader / ESP_LOG）
        discarded_total = 0
        try:
            while self._port.in_waiting:
                junk = self._port.read(self._port.in_waiting)
                discarded_total += len(junk)
                time.sleep(0.05)
            # 再读一小段，吃掉尾巴
            extra = self._port.read(4096)
            discarded_total += len(extra)
        except Exception as exc:  # noqa: BLE001
            logger.warning("清空启动日志失败: %s", exc)

        if discarded_total:
            logger.info("已丢弃启动阶段输入 %d 字节（文本日志，非 ECG）", discarded_total)
        else:
            logger.info("启动等待结束，输入缓冲为空")

        self._bytes_received = 0
        self._chunks = 0
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, daemon=True, name="serial-rx")
        self._thread.start()
        logger.info("串口读线程已启动: %s", device_id)

    def close(self) -> None:
        was_open = self._port is not None and self._port.is_open
        port_name = self._port.port if self._port else None
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=1.5)
        self._thread = None
        if self._port and self._port.is_open:
            self._port.close()
        if was_open:
            logger.info(
                "串口已关闭: %s（累计接收 %d 字节 / %d 块）",
                port_name,
                self._bytes_received,
                self._chunks,
            )
        self._port = None

    def is_open(self) -> bool:
        return self._port is not None and self._port.is_open

    def write(self, data: bytes) -> None:
        raise NotImplementedError("UART 模式不支持上位机下行指令")

    def set_data_callback(self, callback: Callable[[bytes], None] | None) -> None:
        self._callback = callback

    def _notify_lost(self, reason: str) -> None:
        logger.warning("串口连接中断: %s", reason)
        if self._lost_callback:
            self._lost_callback(reason)

    @staticmethod
    def _preview(chunk: bytes) -> str:
        """同时给出 hex 与可打印 ASCII，便于区分日志/二进制。"""
        hex_part = chunk[:24].hex(" ")
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk[:48])
        return f"hex=[{hex_part}] ascii=[{ascii_part}]"

    def _read_loop(self) -> None:
        logger.debug("串口读循环进入")
        while not self._stop.is_set() and self._port and self._port.is_open:
            try:
                chunk = self._port.read(512)
                if not chunk:
                    continue
                self._bytes_received += len(chunk)
                self._chunks += 1
                if self._chunks <= 5 or self._chunks % 50 == 0:
                    logger.info(
                        "串口RX #%d: +%d B, 累计 %d B, %s",
                        self._chunks,
                        len(chunk),
                        self._bytes_received,
                        self._preview(chunk),
                    )
                if self._callback:
                    self._callback(chunk)
            except serial.SerialException as exc:
                self._notify_lost(str(exc))
                break
            except Exception as exc:  # noqa: BLE001
                logger.exception("串口读循环异常: %s", exc)
                self._notify_lost(str(exc))
                break
        logger.debug("串口读循环退出")
