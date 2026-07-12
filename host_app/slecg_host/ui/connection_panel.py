"""Connection panel — transport mode and device selection."""

from __future__ import annotations

from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import (
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QRadioButton,
    QButtonGroup,
    QVBoxLayout,
    QWidget,
)

from slecg_host.transport.base import TransportMode


class ConnectionPanel(QGroupBox):
    transport_changed = pyqtSignal(TransportMode)
    refresh_requested = pyqtSignal()
    connect_requested = pyqtSignal()
    disconnect_requested = pyqtSignal()

    def __init__(self, parent=None) -> None:  # noqa: ANN001
        super().__init__("连接", parent)
        self._connected = False
        self._connecting = False

        self._serial_radio = QRadioButton("串口 (UART)")
        self._ble_radio = QRadioButton("蓝牙 (BLE)")
        self._serial_radio.setChecked(True)
        self._mode_group = QButtonGroup(self)
        self._mode_group.addButton(self._serial_radio)
        self._mode_group.addButton(self._ble_radio)

        self._device_combo = QComboBox()
        self._device_combo.setMinimumWidth(280)

        self._refresh_btn = QPushButton("刷新列表")
        self._connect_btn = QPushButton("连接")
        self._disconnect_btn = QPushButton("断开")
        self._disconnect_btn.setEnabled(False)

        self._hint = QLabel()
        self._hint.setWordWrap(True)
        self._hint.setStyleSheet("color: #666;")

        mode_row = QHBoxLayout()
        mode_row.addWidget(self._serial_radio)
        mode_row.addWidget(self._ble_radio)
        mode_row.addStretch()

        btn_row = QHBoxLayout()
        btn_row.addWidget(self._refresh_btn)
        btn_row.addWidget(self._connect_btn)
        btn_row.addWidget(self._disconnect_btn)

        layout = QVBoxLayout(self)
        layout.addLayout(mode_row)
        layout.addWidget(self._device_combo)
        layout.addLayout(btn_row)
        layout.addWidget(self._hint)

        self._serial_radio.toggled.connect(self._on_mode_toggled)
        self._refresh_btn.clicked.connect(self.refresh_requested.emit)
        self._connect_btn.clicked.connect(self.connect_requested.emit)
        self._disconnect_btn.clicked.connect(self.disconnect_requested.emit)
        self._update_hint()

    @property
    def transport_mode(self) -> TransportMode:
        return TransportMode.BLE if self._ble_radio.isChecked() else TransportMode.SERIAL

    @property
    def selected_device_id(self) -> str | None:
        idx = self._device_combo.currentIndex()
        if idx < 0:
            return None
        return self._device_combo.itemData(idx)

    def set_devices(self, devices: list[tuple[str, str]]) -> None:
        current = self.selected_device_id
        self._device_combo.clear()
        for device_id, label in devices:
            self._device_combo.addItem(label, device_id)
        if current:
            idx = self._device_combo.findData(current)
            if idx >= 0:
                self._device_combo.setCurrentIndex(idx)

    def set_refresh_enabled(self, enabled: bool) -> None:
        if not self._connected and not self._connecting:
            self._refresh_btn.setEnabled(enabled)

    def set_connecting(self, connecting: bool) -> None:
        """连接进行中：禁用连接/刷新，按钮文案提示状态。"""
        self._connecting = connecting
        if connecting:
            self._connect_btn.setEnabled(False)
            self._connect_btn.setText("正在连接…")
            self._refresh_btn.setEnabled(False)
            self._serial_radio.setEnabled(False)
            self._ble_radio.setEnabled(False)
            self._device_combo.setEnabled(False)
            self._disconnect_btn.setEnabled(False)
        else:
            self._connect_btn.setText("连接")
            if not self._connected:
                self._connect_btn.setEnabled(True)
                self._refresh_btn.setEnabled(True)
                self._serial_radio.setEnabled(True)
                self._ble_radio.setEnabled(True)
                self._device_combo.setEnabled(True)

    def set_connected(self, connected: bool) -> None:
        self._connected = connected
        self._connecting = False
        self._connect_btn.setText("连接")
        self._connect_btn.setEnabled(not connected)
        self._disconnect_btn.setEnabled(connected)
        self._serial_radio.setEnabled(not connected)
        self._ble_radio.setEnabled(not connected)
        self._device_combo.setEnabled(not connected)
        if connected:
            self._refresh_btn.setEnabled(False)
        else:
            self._refresh_btn.setEnabled(True)

    def _on_mode_toggled(self) -> None:
        self._update_hint()
        self.transport_changed.emit(self.transport_mode)

    def _update_hint(self) -> None:
        if self.transport_mode == TransportMode.SERIAL:
            self._hint.setText(
                "串口模式：确认设备绿灯常亮；单击设备按键开始/停止采集。"
                "上位机仅被动接收 ECG 数据，不支持远程启停。"
            )
        else:
            self._hint.setText(
                "蓝牙模式：确认设备蓝灯常亮；连接后开启 Notify，"
                "可通过下方按钮或设备按键控制采集。"
            )
