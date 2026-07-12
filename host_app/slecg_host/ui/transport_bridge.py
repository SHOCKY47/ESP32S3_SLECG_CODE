"""线程安全桥：后台线程通过 Qt 信号回到主线程更新 UI。"""

from __future__ import annotations

from PyQt6.QtCore import QObject, pyqtSignal


class TransportBridge(QObject):
    """跨线程 UI 事件桥（必须在主线程创建并 connect）。"""

    data_received = pyqtSignal(bytes)
    connect_succeeded = pyqtSignal(str)
    connect_failed = pyqtSignal(str)
    devices_updated = pyqtSignal(object)  # list[tuple[str, str]]
    refresh_failed = pyqtSignal(str)
    transport_lost = pyqtSignal(str)

    def emit_data(self, data: bytes) -> None:
        self.data_received.emit(data)

    def emit_connect_succeeded(self, device_id: str) -> None:
        self.connect_succeeded.emit(device_id)

    def emit_connect_failed(self, message: str) -> None:
        self.connect_failed.emit(message)

    def emit_devices_updated(self, items: list[tuple[str, str]], ble: bool) -> None:
        self.devices_updated.emit((items, ble))

    def emit_refresh_failed(self, message: str) -> None:
        self.refresh_failed.emit(message)

    def emit_transport_lost(self, reason: str) -> None:
        self.transport_lost.emit(reason)
