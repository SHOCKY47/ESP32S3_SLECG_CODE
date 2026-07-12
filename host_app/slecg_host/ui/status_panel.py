"""DEVICE_STATUS display panel."""

from __future__ import annotations

from PyQt6.QtWidgets import QGroupBox, QHBoxLayout, QLabel

from slecg_host.protocol.constants import (
    SLECG_STATUS_BIT_ACQUIRING,
    SLECG_STATUS_BIT_ADS_READY,
    SLECG_STATUS_BIT_BATTERY_LOW,
    SLECG_STATUS_BIT_BLE_CONNECTED,
)
from slecg_host.protocol.payloads import DeviceStatus
from slecg_host.transport.base import TransportMode


class StatusPanel(QGroupBox):
    def __init__(self, parent=None) -> None:  # noqa: ANN001
        super().__init__("设备状态", parent)

        self._acq = QLabel("● 采集")
        self._ble = QLabel("● BLE")
        self._ads = QLabel("● ADS")
        self._bat = QLabel("● 电池")
        self._detail = QLabel("—")
        self._error = QLabel("")
        self._error.setStyleSheet("color: red;")

        for lbl in (self._acq, self._ble, self._ads, self._bat):
            lbl.setMinimumWidth(80)

        row = QHBoxLayout()
        row.addWidget(self._acq)
        row.addWidget(self._ble)
        row.addWidget(self._ads)
        row.addWidget(self._bat)
        row.addWidget(self._detail)
        row.addStretch()
        row.addWidget(self._error)

        self.setLayout(row)
        self.clear_status()

    def set_transport_mode(self, mode: TransportMode) -> None:
        visible = mode == TransportMode.BLE
        self._ble.setVisible(visible)
        self._ads.setVisible(visible)
        self._bat.setVisible(visible)
        if not visible:
            self._detail.setText("串口模式 — 无 DEVICE_STATUS 上报")

    def clear_status(self) -> None:
        self._set_indicator(self._acq, False)
        self._set_indicator(self._ble, False)
        self._set_indicator(self._ads, False)
        self._set_indicator(self._bat, False, low_is_bad=True)
        self._detail.setText("—")
        self._error.setText("")

    def update_status(self, status: DeviceStatus) -> None:
        self._set_indicator(self._acq, bool(status.state & SLECG_STATUS_BIT_ACQUIRING))
        self._set_indicator(self._ble, bool(status.state & SLECG_STATUS_BIT_BLE_CONNECTED))
        self._set_indicator(self._ads, bool(status.state & SLECG_STATUS_BIT_ADS_READY))
        self._set_indicator(
            self._bat,
            not bool(status.state & SLECG_STATUS_BIT_BATTERY_LOW),
            low_is_bad=True,
        )
        uptime_s = status.uptime_ms / 1000.0
        self._detail.setText(
            f"FW {status.fw_version_str} | {status.sample_rate_hz} Hz | "
            f"ecg_seq={status.ecg_seq} | uptime={uptime_s:.0f}s"
        )
        if status.error_code:
            self._error.setText(f"错误码: {status.error_code}")
        else:
            self._error.setText("")

    def show_ack(self, orig_type: int, result: int) -> None:
        self._error.setStyleSheet("color: green;")
        self._error.setText(f"ACK: type=0x{orig_type:02X} result={result}")

    def show_nack(self, orig_type: int, error: int) -> None:
        self._error.setStyleSheet("color: red;")
        self._error.setText(f"NACK: type=0x{orig_type:02X} error={error}")

    def _set_indicator(self, label: QLabel, ok: bool, *, low_is_bad: bool = False) -> None:
        if low_is_bad:
            color = "#cc0000" if not ok else "#00aa00"
        else:
            color = "#00aa00" if ok else "#aaaaaa"
        label.setStyleSheet(f"color: {color}; font-weight: bold;")
