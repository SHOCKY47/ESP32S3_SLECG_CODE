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
    playback_requested = pyqtSignal()

    def __init__(self, parent=None) -> None:  # noqa: ANN001
        super().__init__("控制", parent)
        self._language = "zh"
        self._theme = "dark"
        self._last_dropped = 0
        self._last_loff = 0
        self._session_state = "idle"

        self._start_btn = QPushButton("开始采集")
        self._stop_btn = QPushButton("停止采集")
        self._status_btn = QPushButton("请求状态")
        self._playback_btn = QPushButton("打开回放")

        self._stats = QLabel(f"丢包: 0 | 采样率: {SLECG_SAMPLE_RATE_HZ} Hz | 导联: —")
        self._session = QLabel("待机 | 空格：冻结/恢复实时窗口")
        self._session.setStyleSheet("color: #00e676; font-weight: bold;")
        self._uart_hint = QLabel("串口模式：请使用设备按键启停采集")
        self._uart_hint.setStyleSheet("color: #84a9cc;")

        btn_row = QHBoxLayout()
        btn_row.addWidget(self._start_btn)
        btn_row.addWidget(self._stop_btn)
        btn_row.addWidget(self._status_btn)
        btn_row.addWidget(self._playback_btn)
        btn_row.addStretch()

        layout = QVBoxLayout(self)
        layout.addLayout(btn_row)
        layout.addWidget(self._stats)
        layout.addWidget(self._session)
        layout.addWidget(self._uart_hint)

        self._start_btn.clicked.connect(self.start_requested.emit)
        self._stop_btn.clicked.connect(self.stop_requested.emit)
        self._status_btn.clicked.connect(self.req_status_requested.emit)
        self._playback_btn.clicked.connect(self.playback_requested.emit)

        self.set_transport_mode(TransportMode.SERIAL)

    def set_language(self, language: str) -> None:
        self._language = language
        zh = language == "zh"
        self.setTitle("采集控制" if zh else "ACQUISITION CONTROL")
        self._start_btn.setText("开始采集" if zh else "START")
        self._stop_btn.setText("停止采集" if zh else "STOP")
        self._status_btn.setText("请求状态" if zh else "STATUS")
        self._playback_btn.setText("打开回放" if zh else "OPEN RECORDING")
        self._uart_hint.setText(
            "串口模式：请使用设备按键启停采集"
            if zh else
            "UART mode: use the device button to start or stop acquisition"
        )
        self.update_stats(self._last_dropped, self._last_loff)
        self.set_session_state(self._session_state, self._session.toolTip())

    def set_transport_mode(self, mode: TransportMode) -> None:
        is_ble = mode == TransportMode.BLE
        self._start_btn.setVisible(is_ble)
        self._stop_btn.setVisible(is_ble)
        self._status_btn.setVisible(is_ble)
        self._uart_hint.setVisible(not is_ble)

    def set_theme(self, theme: str) -> None:
        self._theme = theme
        self._uart_hint.setStyleSheet(
            "color: #687684;" if theme == "light" else "color: #75b58a;"
        )
        self.set_session_state(self._session_state, self._session.toolTip())

    def set_ble_enabled(self, enabled: bool) -> None:
        self._start_btn.setEnabled(enabled)
        self._stop_btn.setEnabled(enabled)
        self._status_btn.setEnabled(enabled)

    def update_stats(self, dropped: int, loff: int) -> None:
        self._last_dropped = dropped
        self._last_loff = loff
        zh = self._language == "zh"
        lead = ("正常" if zh else "OK") if loff == 0 else (
            f"脱落 0x{loff:02X}" if zh else f"LEAD-OFF 0x{loff:02X}"
        )
        if zh:
            text = f"丢包 {dropped}   •   采样率 {SLECG_SAMPLE_RATE_HZ} Hz   •   导联 {lead}"
        else:
            text = f"LOST {dropped}   •   RATE {SLECG_SAMPLE_RATE_HZ} Hz   •   LEAD {lead}"
        self._stats.setText(text)

    def set_session_state(self, state: str, path: str = "") -> None:
        self._session_state = state or "idle"
        zh = self._language == "zh"
        states = {
            "idle": ("待机   •   空格键：冻结/恢复窗口", "STANDBY   •   SPACE: FREEZE/RESUME"),
            "live": ("● 实时采集   •   空格键：冻结窗口", "● LIVE   •   SPACE: FREEZE VIEW"),
            "frozen": ("❚❚ 窗口已冻结，采集与保存继续   •   空格键：恢复", "❚❚ VIEW FROZEN, RECORDING CONTINUES   •   SPACE: RESUME"),
            "stopped": ("采集已停止   •   拖动横轴查看历史", "STOPPED   •   DRAG X-AXIS TO REVIEW"),
            "history": ("历史浏览   •   拖动横轴", "HISTORY   •   DRAG X-AXIS"),
            "playback": ("回放模式   •   拖动横轴", "PLAYBACK   •   DRAG X-AXIS"),
            "record_failed": ("● 实时采集   •   自动保存失败", "● LIVE   •   AUTO-SAVE FAILED"),
        }
        pair = states.get(self._session_state, states["idle"])
        self._session.setText(pair[0] if zh else pair[1])
        color = "#087a3e" if self._theme == "light" else "#35f29a"
        if self._session_state != "live":
            color = "#34404c" if self._theme == "light" else "#d7eaff"
        if self._session_state == "record_failed":
            color = "#ff6680"
        self._session.setStyleSheet(f"color: {color}; font-weight: bold;")
        self._session.setToolTip(path)
