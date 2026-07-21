"""DEVICE_STATUS display panel."""

from __future__ import annotations

from PyQt6.QtWidgets import QGroupBox, QHBoxLayout, QLabel

from slecg_host.protocol.constants import (
    SLECG_STATUS_BIT_ACQUIRING,
    SLECG_STATUS_BIT_ADS_READY,
    SLECG_STATUS_BIT_BLE_CONNECTED,
)
from slecg_host.protocol.payloads import DeviceStatus
from slecg_host.transport.base import TransportMode


class StatusPanel(QGroupBox):
    def __init__(self, parent=None) -> None:  # noqa: ANN001
        super().__init__("设备状态", parent)
        self._language = "zh"
        self._theme = "dark"
        self._mode = TransportMode.SERIAL
        self._last_status: DeviceStatus | None = None

        self._acq = QLabel("● 采集")
        self._ble = QLabel("● BLE")
        self._ads = QLabel("● ADS")
        self._detail = QLabel("—")
        self._error = QLabel("")
        self._error.setStyleSheet("color: red;")

        for lbl in (self._acq, self._ble, self._ads):
            lbl.setMinimumWidth(80)

        row = QHBoxLayout()
        row.addWidget(self._acq)
        row.addWidget(self._ble)
        row.addWidget(self._ads)
        row.addWidget(self._detail)
        row.addStretch()
        row.addWidget(self._error)

        self.setLayout(row)
        self.clear_status()

    def set_language(self, language: str) -> None:
        self._language = language
        zh = language == "zh"
        self.setTitle("设备状态" if zh else "DEVICE STATUS")
        self._acq.setText("● 采集" if zh else "● ACQ")
        self._ble.setText("● 蓝牙" if zh else "● BLE")
        self._ads.setText("● 前端" if zh else "● AFE")
        if self._last_status is not None:
            self.update_status(self._last_status)
        else:
            self.set_transport_mode(self._mode)

    def set_transport_mode(self, mode: TransportMode) -> None:
        self._mode = mode
        visible = mode == TransportMode.BLE
        self._ble.setVisible(visible)
        self._ads.setVisible(visible)
        if not visible:
            self._detail.setText(
                "串口模式 — 无设备状态上报"
                if self._language == "zh" else
                "UART MODE — DEVICE STATUS UNAVAILABLE"
            )

    def set_theme(self, theme: str) -> None:
        self._theme = theme
        if self._last_status is not None:
            self.update_status(self._last_status)
        else:
            self.clear_status()

    def clear_status(self) -> None:
        self._last_status = None
        self._set_indicator(self._acq, False)
        self._set_indicator(self._ble, False)
        self._set_indicator(self._ads, False)
        self._detail.setText("—")
        self._error.setText("")

    def update_status(self, status: DeviceStatus) -> None:
        self._last_status = status
        self._set_indicator(self._acq, bool(status.state & SLECG_STATUS_BIT_ACQUIRING))
        self._set_indicator(self._ble, bool(status.state & SLECG_STATUS_BIT_BLE_CONNECTED))
        self._set_indicator(self._ads, bool(status.state & SLECG_STATUS_BIT_ADS_READY))
        uptime_s = status.uptime_ms / 1000.0
        self._detail.setText((
            f"固件 {status.fw_version_str}  •  {status.sample_rate_hz} Hz  •  "
            f"序号 {status.ecg_seq}  •  运行 {uptime_s:.0f}s"
            if self._language == "zh" else
            f"FW {status.fw_version_str}  •  {status.sample_rate_hz} Hz  •  "
            f"SEQ {status.ecg_seq}  •  UPTIME {uptime_s:.0f}s"
        ))
        if status.error_code:
            prefix = "错误码" if self._language == "zh" else "ERROR"
            self._error.setText(f"{prefix}: {status.error_code}")
        else:
            self._error.setText("")

    def show_ack(self, orig_type: int, result: int) -> None:
        self._error.setStyleSheet("color: #00e676;")
        self._error.setText(f"ACK: type=0x{orig_type:02X} result={result}")

    def show_nack(self, orig_type: int, error: int) -> None:
        self._error.setStyleSheet("color: #ff6680;")
        self._error.setText(f"NACK: type=0x{orig_type:02X} error={error}")

    def _set_indicator(self, label: QLabel, ok: bool) -> None:
        if self._theme == "light":
            color = "#087a3e" if ok else "#8b98a4"
        else:
            color = "#35f29a" if ok else "#607d68"
        label.setStyleSheet(f"color: {color}; font-weight: bold;")
