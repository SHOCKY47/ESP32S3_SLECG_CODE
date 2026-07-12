"""BLE control panel — START/STOP/REQ_STATUS."""

from __future__ import annotations

from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import (
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
)

from slecg_host.protocol.constants import SLECG_SAMPLE_RATE_HZ
from slecg_host.transport.base import TransportMode


class ControlPanel(QGroupBox):
    start_requested = pyqtSignal()
    stop_requested = pyqtSignal()
    req_status_requested = pyqtSignal()
    record_toggled = pyqtSignal(bool)

    def __init__(self, parent=None) -> None:  # noqa: ANN001
        super().__init__("控制", parent)

        self._start_btn = QPushButton("开始采集")
        self._stop_btn = QPushButton("停止采集")
        self._status_btn = QPushButton("请求状态")
        self._record_btn = QPushButton("开始录制 CSV")
        self._record_btn.setCheckable(True)

        self._stats = QLabel(f"丢包: 0 | 采样率: {SLECG_SAMPLE_RATE_HZ} Hz | 导联: —")
        self._uart_hint = QLabel("串口模式：请使用设备按键启停采集")
        self._uart_hint.setStyleSheet("color: #888;")

        btn_row = QHBoxLayout()
        btn_row.addWidget(self._start_btn)
        btn_row.addWidget(self._stop_btn)
        btn_row.addWidget(self._status_btn)
        btn_row.addWidget(self._record_btn)
        btn_row.addStretch()

        layout = QVBoxLayout(self)
        layout.addLayout(btn_row)
        layout.addWidget(self._stats)
        layout.addWidget(self._uart_hint)

        self._start_btn.clicked.connect(self.start_requested.emit)
        self._stop_btn.clicked.connect(self.stop_requested.emit)
        self._status_btn.clicked.connect(self.req_status_requested.emit)
        self._record_btn.toggled.connect(self.record_toggled.emit)

        self.set_transport_mode(TransportMode.SERIAL)

    def set_transport_mode(self, mode: TransportMode) -> None:
        is_ble = mode == TransportMode.BLE
        self._start_btn.setVisible(is_ble)
        self._stop_btn.setVisible(is_ble)
        self._status_btn.setVisible(is_ble)
        self._uart_hint.setVisible(not is_ble)

    def set_ble_enabled(self, enabled: bool) -> None:
        self._start_btn.setEnabled(enabled)
        self._stop_btn.setEnabled(enabled)
        self._status_btn.setEnabled(enabled)

    def update_stats(self, dropped: int, loff: int) -> None:
        lead = "OK" if loff == 0 else f"脱落 (0x{loff:02X})"
        self._stats.setText(f"丢包: {dropped} | 采样率: {SLECG_SAMPLE_RATE_HZ} Hz | 导联: {lead}")

    def set_recording(self, active: bool, path: str = "") -> None:
        self._record_btn.blockSignals(True)
        self._record_btn.setChecked(active)
        self._record_btn.setText("停止录制" if active else "开始录制 CSV")
        self._record_btn.blockSignals(False)
        if active and path:
            self._stats.setToolTip(f"录制中: {path}")
