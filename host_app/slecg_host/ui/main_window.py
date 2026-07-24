"""主窗口。"""

from __future__ import annotations

import logging
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from concurrent.futures import TimeoutError as FuturesTimeout
from pathlib import Path

import numpy as np
from PyQt6.QtCore import Qt, QTimer, pyqtSlot
from PyQt6.QtGui import QKeySequence, QShortcut
from PyQt6.QtWidgets import (
    QFileDialog,
    QApplication,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from slecg_host.ecg.buffer import EcgBuffer
from slecg_host.ecg.converter import DisplayMode, EcgConverter
from slecg_host.ecg.recorder import EcgRecorder, load_recording
from slecg_host.ecg.processor import EcgAnalysis, EcgProcessor
from slecg_host.protocol.constants import (
    SLECG_ECG_PAYLOAD_LEN,
    SLECG_ECG_SAMPLES_PER_PKT,
    SLECG_SAMPLE_RATE_HZ,
    SLECG_STATUS_PAYLOAD_LEN,
    SLECG_TYPE_ACK,
    SLECG_TYPE_DEVICE_STATUS,
    SLECG_TYPE_ECG_DATA,
    SLECG_TYPE_NACK,
)
from slecg_host.protocol.frame import FrameParser
from slecg_host.protocol.payloads import (
    build_req_status,
    build_start_acq,
    build_stop_acq,
    parse_ack,
    parse_device_status,
    parse_ecg_payload,
    parse_nack,
)
from slecg_host.transport.base import Transport, TransportMode
from slecg_host.transport.ble_transport import BleTransport
from slecg_host.transport.serial_transport import SerialTransport

from .connection_panel import ConnectionPanel
from .control_panel import ControlPanel
from .ecg_plot import EcgPlotWidget
from .status_panel import StatusPanel
from .transport_bridge import TransportBridge
from .theme import DARK_STYLE, LIGHT_STYLE

logger = logging.getLogger(__name__)

# 串口打开若被占用可能长时间阻塞；含启动等待约 1.5s，超时留余量
_CONNECT_TIMEOUT_S = 12.0
_SERIAL_FRAME_LENGTHS = {SLECG_TYPE_ECG_DATA: SLECG_ECG_PAYLOAD_LEN}
_BLE_FRAME_LENGTHS = {
    SLECG_TYPE_ECG_DATA: SLECG_ECG_PAYLOAD_LEN,
    SLECG_TYPE_DEVICE_STATUS: SLECG_STATUS_PAYLOAD_LEN,
    SLECG_TYPE_ACK: 2,
    SLECG_TYPE_NACK: 2,
}
_PARSER_STALL_BYTES = 65


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self._language = "en"
        self._theme = "light"
        self.setWindowTitle("SLECG ECG MONITOR")
        self.resize(1180, 760)

        self._converter = EcgConverter()
        self._processor = EcgProcessor()
        self._buffer = EcgBuffer(window_seconds=5.0)
        self._parser = FrameParser(expected_lengths=_SERIAL_FRAME_LENGTHS)
        self._testdata_dir = Path.cwd() / "testdata"
        self._recorder = EcgRecorder(self._converter, output_dir=self._testdata_dir)

        self._serial = SerialTransport()
        self._ble = BleTransport()
        self._transport: Transport = self._serial
        self._mode = TransportMode.SERIAL
        self._plot_mode = DisplayMode.RAW
        self._last_analysis_count = -1
        self._last_analysis: EcgAnalysis | None = None
        self._heart_rate_bpm: float | None = None
        self._last_logged_hr: int | None = None
        self._bridge = TransportBridge()
        self._executor = ThreadPoolExecutor(max_workers=2, thread_name_prefix="slecg-host")

        self._bytes_in = 0
        self._frames_in = 0
        self._ecg_packets = 0
        self._last_rx_log_bytes = 0
        self._last_ecg_monotonic = 0.0
        self._last_parser_recovery_bytes = 0
        self._connected = False
        self._acquiring = False
        self._frozen = False

        self._conn_panel = ConnectionPanel()
        self._plot = EcgPlotWidget(self._converter)
        self._optimized_plot = EcgPlotWidget(self._converter, processed=True)
        self._optimized_plot.setXLink(self._plot)
        self._control = ControlPanel()
        self._status = StatusPanel()

        header = QFrame()
        header.setObjectName("appHeader")
        header_row = QHBoxLayout(header)
        header_row.setContentsMargins(18, 10, 18, 10)
        title_box = QVBoxLayout()
        self._app_title = QLabel("SLECG ECG MONITOR")
        self._app_title.setObjectName("appTitle")
        self._app_subtitle = QLabel("SINGLE-LEAD BIOPOTENTIAL ACQUISITION & REVIEW")
        self._app_subtitle.setObjectName("appSubtitle")
        title_box.addWidget(self._app_title)
        title_box.addWidget(self._app_subtitle)
        self._live_badge = QLabel("● SYSTEM READY")
        self._live_badge.setObjectName("systemBadge")
        self._heart_rate = QLabel("HR -- BPM")
        self._heart_rate.setObjectName("heartRateBadge")
        self._language_btn = QPushButton("中文")
        self._language_btn.setObjectName("languageButton")
        self._language_btn.setFixedWidth(72)
        self._theme_btn = QPushButton("☾")
        self._theme_btn.setObjectName("themeButton")
        self._theme_btn.setFixedWidth(46)
        header_row.addLayout(title_box)
        header_row.addStretch()
        header_row.addWidget(self._live_badge)
        header_row.addSpacing(14)
        header_row.addWidget(self._heart_rate)
        header_row.addSpacing(14)
        header_row.addWidget(self._theme_btn)
        header_row.addSpacing(6)
        header_row.addWidget(self._language_btn)

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.setContentsMargins(16, 14, 16, 12)
        layout.setSpacing(10)
        layout.addWidget(header)
        layout.addWidget(self._conn_panel)
        plot_splitter = QSplitter(Qt.Orientation.Vertical)
        plot_splitter.setObjectName("plotSplitter")
        plot_splitter.addWidget(self._plot)
        plot_splitter.addWidget(self._optimized_plot)
        plot_splitter.setSizes([260, 260])
        layout.addWidget(plot_splitter, stretch=1)
        layout.addWidget(self._control)
        layout.addWidget(self._status)
        self.setCentralWidget(central)
        self.setStatusBar(QStatusBar())

        self._refresh_timer = QTimer(self)
        self._refresh_timer.timeout.connect(self._refresh_plot)
        self._refresh_timer.start(33)

        self._status_timer = QTimer(self)
        self._status_timer.timeout.connect(self._update_rx_status)
        self._status_timer.start(500)

        self._wire_signals()
        self._language_btn.clicked.connect(self._toggle_language)
        self._theme_btn.clicked.connect(self._toggle_theme)
        self._freeze_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Space), self)
        self._freeze_shortcut.activated.connect(self._toggle_freeze)
        self._apply_language()
        self._apply_theme()
        self._refresh_device_list()
        logger.info("主窗口初始化完成，默认模式=SERIAL")

    def _wire_signals(self) -> None:
        self._bridge.data_received.connect(self._process_data)
        self._bridge.connect_succeeded.connect(self._on_connected)
        self._bridge.connect_failed.connect(self._on_connect_failed)
        self._bridge.devices_updated.connect(self._on_devices_updated)
        self._bridge.refresh_failed.connect(self._on_refresh_failed)
        self._bridge.transport_lost.connect(self._on_transport_lost)

        self._conn_panel.transport_changed.connect(self._on_transport_changed)
        self._conn_panel.refresh_requested.connect(self._refresh_device_list)
        self._conn_panel.connect_requested.connect(self._connect)
        self._conn_panel.disconnect_requested.connect(self._disconnect)

        self._control.start_requested.connect(self._send_start)
        self._control.stop_requested.connect(self._send_stop)
        self._control.req_status_requested.connect(self._send_req_status)
        self._control.playback_requested.connect(self._open_playback)

        self._plot.display_mode_changed.connect(self._on_display_mode_changed)
        self._optimized_plot.display_mode_changed.connect(self._on_display_mode_changed)

    def _tr(self, zh: str, en: str) -> str:
        return zh if self._language == "zh" else en

    def _toggle_language(self) -> None:
        self._language = "en" if self._language == "zh" else "zh"
        self._apply_language()

    def _toggle_theme(self) -> None:
        self._theme = "light" if self._theme == "dark" else "dark"
        self._apply_theme()

    def _apply_theme(self) -> None:
        app = QApplication.instance()
        if app is not None:
            app.setStyleSheet(LIGHT_STYLE if self._theme == "light" else DARK_STYLE)
        self._theme_btn.setText("☾" if self._theme == "light" else "☀")
        self._theme_btn.setToolTip(self._tr(
            "切换到深色模式" if self._theme == "light" else "切换到浅色模式",
            "Switch to dark mode" if self._theme == "light" else "Switch to light mode",
        ))
        self._conn_panel.set_theme(self._theme)
        self._control.set_theme(self._theme)
        self._status.set_theme(self._theme)
        self._plot.set_theme(self._theme)
        self._optimized_plot.set_theme(self._theme)

    def _apply_language(self) -> None:
        zh = self._language == "zh"
        self.setWindowTitle("SLECG 心电监护终端" if zh else "SLECG ECG MONITOR")
        self._app_title.setText("SLECG 心电监护终端" if zh else "SLECG ECG MONITOR")
        self._app_subtitle.setText(
            "单导联生物电信号采集与分析系统"
            if zh else
            "SINGLE-LEAD BIOPOTENTIAL ACQUISITION & REVIEW"
        )
        self._live_badge.setText("● 系统就绪" if zh else "● SYSTEM READY")
        self._language_btn.setText("EN" if zh else "中文")
        self._language_btn.setToolTip("Switch to English" if zh else "切换到中文")
        self._conn_panel.set_language(self._language)
        self._control.set_language(self._language)
        self._status.set_language(self._language)
        self._plot.set_language(self._language)
        self._optimized_plot.set_language(self._language)
        self._update_heart_rate_label()
        self._theme_btn.setToolTip(self._tr(
            "切换到深色模式" if self._theme == "light" else "切换到浅色模式",
            "Switch to dark mode" if self._theme == "light" else "Switch to light mode",
        ))

    def _update_heart_rate_label(self) -> None:
        if self._heart_rate_bpm is None:
            self._heart_rate.setText("心率 -- BPM" if self._language == "zh" else "HR -- BPM")
        else:
            prefix = "心率" if self._language == "zh" else "HR"
            self._heart_rate.setText(f"{prefix} {self._heart_rate_bpm:.0f} BPM")

    @pyqtSlot(TransportMode)
    def _on_transport_changed(self, mode: TransportMode) -> None:
        if self._transport.is_open():
            self._disconnect()
        self._mode = mode
        self._transport = self._ble if mode == TransportMode.BLE else self._serial
        self._parser.set_expected_lengths(
            _BLE_FRAME_LENGTHS if mode == TransportMode.BLE else _SERIAL_FRAME_LENGTHS
        )
        self._control.set_transport_mode(mode)
        self._status.set_transport_mode(mode)
        logger.info("传输方式切换为 %s", mode.value)
        self._refresh_device_list()

    def _refresh_device_list(self) -> None:
        self._refresh_btn_busy(True)
        if self._mode == TransportMode.BLE:

            def scan_job() -> None:
                try:
                    logger.info("开始 BLE 扫描…")
                    devices = self._ble.scan(timeout=4.0)
                    items = [(d.id, d.label) for d in devices]
                    logger.info("BLE 扫描完成: %d 台", len(items))
                    self._bridge.emit_devices_updated(items, True)
                except Exception as exc:  # noqa: BLE001
                    logger.exception("BLE 扫描失败")
                    self._bridge.emit_refresh_failed(str(exc))

            threading.Thread(target=scan_job, daemon=True, name="ble-scan").start()
        else:
            try:
                devices = self._serial.list_devices()
                items = [(d.id, d.label) for d in devices]
                self._on_devices_updated((items, False))
            except Exception as exc:  # noqa: BLE001
                logger.exception("串口枚举失败")
                self._on_refresh_failed(str(exc))

    @pyqtSlot(object)
    def _on_devices_updated(self, payload: object) -> None:
        items, ble = payload  # type: ignore[misc]
        self._conn_panel.set_devices(items)
        self._refresh_btn_busy(False)
        if ble and not items:
            self.statusBar().showMessage(self._tr("未扫描到 ESP_SLECG 设备", "NO ESP_SLECG DEVICE FOUND"), 5000)
            logger.warning("BLE 扫描结果为空")
        else:
            self.statusBar().showMessage(
                self._tr(f"已刷新 {len(items)} 个设备", f"{len(items)} DEVICE(S) FOUND"), 2000
            )
            logger.info("设备列表已刷新: %d 项", len(items))

    @pyqtSlot(str)
    def _on_refresh_failed(self, message: str) -> None:
        self._refresh_btn_busy(False)
        self.statusBar().showMessage(self._tr(f"刷新失败: {message}", f"SCAN FAILED: {message}"), 5000)
        logger.error("刷新失败: %s", message)

    def _refresh_btn_busy(self, busy: bool) -> None:
        self._conn_panel.set_refresh_enabled(not busy)
        if busy:
            self.statusBar().showMessage(self._tr("正在扫描…", "SCANNING…"))

    def _connect(self) -> None:
        device_id = self._conn_panel.selected_device_id
        if not device_id:
            QMessageBox.warning(
                self,
                self._tr("连接", "CONNECTION"),
                self._tr("请先选择端口或蓝牙设备", "Select a UART or BLE device first."),
            )
            return
        if self._connected:
            logger.warning("已处于连接状态，忽略重复连接请求")
            return

        logger.info("用户请求连接: device=%s mode=%s", device_id, self._mode.value)
        self._conn_panel.set_connecting(True)
        self.statusBar().showMessage(self._tr(f"正在连接 {device_id} …", f"CONNECTING TO {device_id} …"))

        def connect_job() -> None:
            t0 = time.monotonic()
            try:
                logger.info("后台连接任务开始: %s (%s)", device_id, self._mode.value)
                self._parser.reset()
                self._buffer.reset()
                self._bytes_in = 0
                self._frames_in = 0
                self._ecg_packets = 0
                self._last_rx_log_bytes = 0
                self._last_ecg_monotonic = 0.0
                self._last_parser_recovery_bytes = 0
                self._transport.set_data_callback(self._bridge.emit_data)
                if isinstance(self._transport, SerialTransport):
                    self._transport.set_lost_callback(self._bridge.emit_transport_lost)

                # 用超时保护：端口被占用时部分系统会长时间阻塞
                future = self._executor.submit(self._transport.open, device_id)
                try:
                    future.result(timeout=_CONNECT_TIMEOUT_S)
                except FuturesTimeout as exc:
                    logger.error(
                        "连接超时(%.0fs): %s — 端口可能被 idf_monitor 占用",
                        _CONNECT_TIMEOUT_S,
                        device_id,
                    )
                    try:
                        self._transport.close()
                    except Exception:  # noqa: BLE001
                        pass
                    raise RuntimeError(
                        f"连接超时（{_CONNECT_TIMEOUT_S:.0f}s）：无法打开 {device_id}\n\n"
                        "请确认已关闭 idf_monitor / 其它串口程序后重试。"
                    ) from exc

                elapsed = time.monotonic() - t0
                logger.info("传输层 open() 成功，耗时 %.2fs", elapsed)
                self._bridge.emit_connect_succeeded(device_id)
            except Exception as exc:  # noqa: BLE001
                logger.exception("连接失败: %s", device_id)
                self._bridge.emit_connect_failed(str(exc))

        threading.Thread(target=connect_job, daemon=True, name="transport-connect").start()

    @pyqtSlot(str)
    def _on_connected(self, device_id: str) -> None:
        self._connected = True
        self._conn_panel.set_connected(True)
        self._control.set_ble_enabled(self._mode == TransportMode.BLE)
        logger.info("UI 已更新为已连接: %s mode=%s", device_id, self._mode.value)
        if self._mode == TransportMode.SERIAL:
            self.statusBar().showMessage(self._tr(
                f"已连接 {device_id} — 使用设备按键开始采集",
                f"CONNECTED: {device_id} — USE DEVICE BUTTON TO START",
            ), 12000)
        else:
            self.statusBar().showMessage(self._tr(f"已连接: {device_id}", f"CONNECTED: {device_id}"), 5000)

    @pyqtSlot(str)
    def _on_connect_failed(self, message: str) -> None:
        self._connected = False
        self._conn_panel.set_connecting(False)
        self._conn_panel.set_connected(False)
        self.statusBar().showMessage(self._tr(f"连接失败: {message}", f"CONNECTION FAILED: {message}"), 8000)
        logger.error("UI 连接失败提示: %s", message)
        QMessageBox.critical(self, self._tr("连接失败", "CONNECTION FAILED"), message)

    @pyqtSlot(str)
    def _on_transport_lost(self, reason: str) -> None:
        logger.warning("传输层断开: %s", reason)
        if self._transport.is_open() or self._connected:
            self._disconnect()
        self.statusBar().showMessage(self._tr(f"连接已断开: {reason}", f"DISCONNECTED: {reason}"), 8000)
        QMessageBox.warning(
            self,
            self._tr("连接断开", "CONNECTION LOST"),
            self._tr(
                f"{reason}\n\n若正在使用 idf_monitor，请勿与上位机同时占用同一串口。",
                f"{reason}\n\nClose idf_monitor before using the same UART port.",
            ),
        )

    def _disconnect(self) -> None:
        logger.info("用户/系统请求断开")
        self._finish_session("连接断开")
        if isinstance(self._transport, SerialTransport):
            self._transport.set_lost_callback(None)
        self._transport.set_data_callback(None)
        self._transport.close()
        self._connected = False
        self._conn_panel.set_connecting(False)
        self._conn_panel.set_connected(False)
        self._control.set_ble_enabled(False)
        self._status.clear_status()
        self.statusBar().showMessage(self._tr("已断开", "DISCONNECTED"), 3000)
        logger.info("已主动断开连接")

    def _update_rx_status(self) -> None:
        if not self._connected:
            return
        if (
            self._acquiring
            and self._last_ecg_monotonic > 0.0
            and time.monotonic() - self._last_ecg_monotonic >= 1.2
        ):
            self._finish_session("采集已停止")
        # 若噪声造成残留半包且真实数据仍持续到达，主动丢弃半包即可恢复，
        # 无需断开设备。固定类型/长度校验通常会更早完成重同步。
        if (
            self._parser.cache_size >= 5
            and self._bytes_in - self._last_parser_recovery_bytes >= _PARSER_STALL_BYTES
            and (
                self._last_ecg_monotonic == 0.0
                or time.monotonic() - self._last_ecg_monotonic >= 1.5
            )
        ):
            cached = self._parser.cache_size
            self._parser.reset()
            self._last_parser_recovery_bytes = self._bytes_in
            logger.warning("解析器卡帧自恢复：丢弃残留 %d B，继续等待下一帧", cached)
        self.statusBar().showMessage(self._tr(
            f"已连接  •  接收 {self._bytes_in} B  •  帧 {self._frames_in}  •  "
            f"ECG包 {self._ecg_packets}  •  缓存 {self._parser.cache_size} B",
            f"CONNECTED  •  RX {self._bytes_in} B  •  FRAMES {self._frames_in}  •  "
            f"ECG {self._ecg_packets}  •  CACHE {self._parser.cache_size} B",
        ))

    @pyqtSlot(bytes)
    def _process_data(self, data: bytes) -> None:
        try:
            self._bytes_in += len(data)
            frames = self._parser.feed(data)
            if frames:
                self._frames_in += len(frames)
                logger.debug(
                    "解析出 %d 帧 (types=%s), 累计字节=%d",
                    len(frames),
                    [f"0x{f.type:02X}" for f in frames],
                    self._bytes_in,
                )
            for frame in frames:
                self._dispatch_frame(frame.type, frame.payload)

            # 尚未同步到协议帧时，周期性提示：可能仍在收文本日志
            if (
                self._ecg_packets == 0
                and self._bytes_in - self._last_rx_log_bytes >= 1024
            ):
                self._last_rx_log_bytes = self._bytes_in
                logger.warning(
                    "已收 %d 字节但尚未解析到 ECG 帧。"
                    "若设备绿灯常亮，请单击按键进入采集（绿灯闪烁）；"
                    "若仍为文本日志，请确认未与 idf_monitor 抢占端口。",
                    self._bytes_in,
                )
        except Exception:  # noqa: BLE001
            logger.exception("数据处理异常")
            traceback.print_exc()

    def _dispatch_frame(self, frame_type: int, payload: bytes) -> None:
        # UART 在采集阶段是 ECG-only 二进制通道。即使噪声/文本中
        # 偶然出现伪帧头，也不接受 STATUS/ACK/未知类型。
        if self._mode == TransportMode.SERIAL and frame_type != SLECG_TYPE_ECG_DATA:
            logger.debug(
                "UART ECG-only: 丢弃 TYPE=0x%02x len=%d", frame_type, len(payload)
            )
            return

        if frame_type == SLECG_TYPE_ECG_DATA:
            if len(payload) != SLECG_ECG_PAYLOAD_LEN:
                logger.warning(
                    "丢弃非法 ECG 帧：payload=%d，期望=%d",
                    len(payload),
                    SLECG_ECG_PAYLOAD_LEN,
                )
                return
            pkt = parse_ecg_payload(payload)
            if pkt.n_samples != SLECG_ECG_SAMPLES_PER_PKT or len(pkt.samples) != SLECG_ECG_SAMPLES_PER_PKT:
                logger.warning(
                    "丢弃非法 ECG 帧：n_samples=%d parsed=%d",
                    pkt.n_samples,
                    len(pkt.samples),
                )
                return
            if not self._acquiring:
                self._begin_session()
            self._ecg_packets += 1
            self._last_ecg_monotonic = time.monotonic()
            self._last_parser_recovery_bytes = self._bytes_in
            if self._ecg_packets == 1:
                logger.info(
                    "收到首个 ECG_DATA 包 seq=%d ts=%d n=%d loff=0x%02x samples[0]=%s",
                    pkt.seq,
                    pkt.ts_ms,
                    pkt.n_samples,
                    pkt.loff,
                    pkt.samples[0] if pkt.samples else None,
                )
            elif self._ecg_packets % 20 == 0:
                logger.info("ECG 包累计 %d 帧 seq=%d", self._ecg_packets, pkt.seq)
            self._buffer.add_packet(pkt)
            self._recorder.add_packet(pkt)
            snap = self._buffer.snapshot()
            self._control.update_stats(snap.dropped_packets, snap.loff)
        elif frame_type == SLECG_TYPE_DEVICE_STATUS:
            status = parse_device_status(payload)
            logger.info(
                "DEVICE_STATUS state=0x%02x error=%d acquiring=%s",
                status.state,
                status.error_code,
                bool(status.state & 0x01),
            )
            self._status.update_status(status)
            if self._acquiring and not bool(status.state & 0x01):
                self._finish_session("采集已停止")
        elif frame_type == SLECG_TYPE_ACK:
            ack = parse_ack(payload)
            logger.info("ACK orig=0x%02x result=%d", ack.orig_type, ack.result)
            self._status.show_ack(ack.orig_type, ack.result)
        elif frame_type == SLECG_TYPE_NACK:
            nack = parse_nack(payload)
            logger.warning("NACK orig=0x%02x error=%d", nack.orig_type, nack.error)
            self._status.show_nack(nack.orig_type, nack.error)
        else:
            logger.debug("忽略帧 TYPE=0x%02x len=%d", frame_type, len(payload))

    def _refresh_plot(self) -> None:
        if self._frozen:
            return
        snap = self._buffer.recent_snapshot()
        self._plot.update_data(snap.times, snap.raw, follow_live=True)
        full = self._buffer.snapshot()
        self._update_optimized(full, follow_live=True)

    def _update_optimized(self, snap, *, follow_live: bool, force: bool = False) -> None:  # noqa: ANN001
        count = len(snap.raw)
        if count == 0:
            self._optimized_plot.update_data(snap.times, snap.raw, follow_live=follow_live)
            self._optimized_plot.update_r_peaks(snap.times, snap.raw, np.array([], dtype=np.int64))
            self._set_heart_rate(None)
            return
        if not force and count == self._last_analysis_count:
            return

        # 实时模式只处理最近15秒，既给零相位滤波保留边缘上下文，
        # 又避免会话越长计算量越大；冻结/停止/回放时处理完整记录。
        if follow_live:
            context = int(SLECG_SAMPLE_RATE_HZ * 15)
            times = snap.times[-context:]
            raw = snap.raw[-context:]
        else:
            times = snap.times
            raw = snap.raw
        try:
            analysis = self._processor.process(times, raw)
        except Exception:  # noqa: BLE001
            logger.exception("上位机 ECG 优化处理失败")
            return
        self._last_analysis_count = count
        self._last_analysis = analysis
        self._optimized_plot.update_data(
            analysis.times,
            analysis.optimized_raw,
            follow_live=follow_live,
        )
        self._optimized_plot.update_r_peaks(
            analysis.times,
            analysis.optimized_raw,
            analysis.r_peaks,
        )
        self._set_heart_rate(analysis.heart_rate_bpm)

    def _set_heart_rate(self, bpm: float | None) -> None:
        self._heart_rate_bpm = bpm
        self._update_heart_rate_label()
        rounded = round(bpm) if bpm is not None else None
        if rounded is not None and rounded != self._last_logged_hr:
            logger.info("R-R 心率估计: %d BPM", rounded)
            self._last_logged_hr = rounded

    def _send_start(self) -> None:
        try:
            # START 代表一次新的采集会话。先清掉上次遗留的半帧和绘图时间基准，
            # 避免首个 Notify 与旧缓存拼接后必须重连才能恢复。
            self._parser.reset()
            self._finish_session("准备新采集")
            self._buffer.reset()
            self._ecg_packets = 0
            self._last_ecg_monotonic = 0.0
            self._last_parser_recovery_bytes = self._bytes_in
            logger.info("发送 START_ACQ")
            self._transport.write(build_start_acq())
        except Exception as exc:  # noqa: BLE001
            logger.error("START 失败: %s", exc)
            QMessageBox.warning(self, self._tr("指令", "COMMAND"), str(exc))

    def _send_stop(self) -> None:
        try:
            logger.info("发送 STOP_ACQ")
            self._transport.write(build_stop_acq())
            self._finish_session("采集已停止")
        except Exception as exc:  # noqa: BLE001
            logger.error("STOP 失败: %s", exc)
            QMessageBox.warning(self, self._tr("指令", "COMMAND"), str(exc))

    def _send_req_status(self) -> None:
        try:
            logger.info("发送 REQ_STATUS")
            self._transport.write(build_req_status())
        except Exception as exc:  # noqa: BLE001
            logger.error("REQ_STATUS 失败: %s", exc)
            QMessageBox.warning(self, self._tr("指令", "COMMAND"), str(exc))

    def _begin_session(self) -> None:
        """首个ECG包到达时开始一段新会话并自动落盘。"""
        self._buffer.reset()
        self._acquiring = True
        self._frozen = False
        self._ecg_packets = 0
        self._plot.set_history_mode(False)
        self._optimized_plot.set_history_mode(False)
        self._last_analysis_count = -1
        self._last_analysis = None
        self._last_logged_hr = None
        self._set_heart_rate(None)
        try:
            if self._recorder.is_recording:
                self._recorder.stop()
            path = self._recorder.start()
            self._control.set_session_state("live", str(path))
            self.statusBar().showMessage(self._tr(f"自动记录: {path}", f"AUTO-RECORDING: {path}"), 5000)
            logger.info("新采集会话自动记录: %s", path)
        except Exception as exc:  # noqa: BLE001
            logger.exception("自动记录启动失败")
            self._control.set_session_state("record_failed")
            QMessageBox.warning(self, self._tr("自动记录", "AUTO RECORDING"), str(exc))

    def _finish_session(self, reason: str) -> None:
        if not self._acquiring and not self._recorder.is_recording:
            return
        path = self._recorder.current_path
        self._recorder.stop()
        self._acquiring = False
        self._frozen = True
        self._plot.set_history_mode(True)
        self._optimized_plot.set_history_mode(True)
        snap = self._buffer.snapshot()
        self._plot.update_data(snap.times, snap.raw, follow_live=False)
        self._update_optimized(snap, follow_live=False, force=True)
        self._control.set_session_state("stopped", str(path or ""))
        if path:
            self.statusBar().showMessage(self._tr(f"记录已保存: {path}", f"RECORDING SAVED: {path}"), 5000)
            logger.info("采集会话结束并保存: %s", path)

    def _toggle_freeze(self) -> None:
        if not self._acquiring:
            self._plot.set_history_mode(True)
            self._optimized_plot.set_history_mode(True)
            self._control.set_session_state("history")
            return
        self._frozen = not self._frozen
        self._plot.set_history_mode(self._frozen)
        self._optimized_plot.set_history_mode(self._frozen)
        if self._frozen:
            snap = self._buffer.snapshot()
            self._plot.update_data(snap.times, snap.raw, follow_live=False)
            self._update_optimized(snap, follow_live=False, force=True)
            path = self._recorder.current_path
            self._control.set_session_state("frozen", str(path or ""))
        else:
            self._control.set_session_state("live", str(self._recorder.current_path or ""))
            self._refresh_plot()

    def _open_playback(self) -> None:
        if self._acquiring:
            QMessageBox.information(
                self,
                self._tr("打开回放", "OPEN RECORDING"),
                self._tr("请先停止当前采集，再打开历史记录。", "Stop acquisition before opening a recording."),
            )
            return
        self._testdata_dir.mkdir(parents=True, exist_ok=True)
        path, _ = QFileDialog.getOpenFileName(
            self,
            self._tr("打开 ECG 回放", "OPEN ECG RECORDING"),
            str(self._testdata_dir),
            "SLECG CSV (*.csv)",
        )
        if not path:
            return
        try:
            times, raw = load_recording(path)
            self._buffer.load_samples(times, raw)
            self._frozen = True
            self._plot.set_history_mode(True)
            self._optimized_plot.set_history_mode(True)
            self._plot.update_data(times, raw, follow_live=False)
            snap = self._buffer.snapshot()
            self._last_analysis_count = -1
            self._update_optimized(snap, follow_live=False, force=True)
            if len(times):
                end = min(float(times[-1]), 5.0)
                self._plot.setXRange(0.0, max(5.0, end), padding=0)
            self._control.set_session_state("playback", path)
            self.statusBar().showMessage(self._tr(f"已打开回放: {path}", f"RECORDING OPENED: {path}"), 5000)
            logger.info("打开回放: %s (%d samples)", path, len(raw))
        except Exception as exc:  # noqa: BLE001
            logger.exception("打开回放失败")
            QMessageBox.warning(self, self._tr("打开回放", "OPEN RECORDING"), str(exc))

    @pyqtSlot(DisplayMode)
    def _on_display_mode_changed(self, mode: DisplayMode) -> None:
        self._plot_mode = mode
        self._plot.set_display_mode(mode)
        self._optimized_plot.set_display_mode(mode)
        if self._frozen:
            snap = self._buffer.snapshot()
            self._plot.update_data(snap.times, snap.raw, follow_live=False)
            self._update_optimized(snap, follow_live=False, force=True)
        else:
            self._refresh_plot()

    def closeEvent(self, event) -> None:  # noqa: ANN001, N802
        logger.info("主窗口关闭")
        self._disconnect()
        self._executor.shutdown(wait=False, cancel_futures=True)
        super().closeEvent(event)

    def keyPressEvent(self, event) -> None:  # noqa: ANN001, N802
        if event.key() == Qt.Key.Key_Space and not event.isAutoRepeat():
            self._toggle_freeze()
            event.accept()
            return
        super().keyPressEvent(event)
