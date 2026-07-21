"""主窗口。"""

from __future__ import annotations

import logging
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from concurrent.futures import TimeoutError as FuturesTimeout
from pathlib import Path

from PyQt6.QtCore import QTimer, pyqtSlot
from PyQt6.QtWidgets import (
    QMainWindow,
    QMessageBox,
    QStatusBar,
    QVBoxLayout,
    QWidget,
)

from slecg_host.ecg.buffer import EcgBuffer
from slecg_host.ecg.converter import DisplayMode, EcgConverter
from slecg_host.ecg.recorder import EcgRecorder
from slecg_host.protocol.constants import (
    SLECG_ECG_PAYLOAD_LEN,
    SLECG_ECG_SAMPLES_PER_PKT,
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

logger = logging.getLogger(__name__)

# 串口打开若被占用可能长时间阻塞；含启动等待约 1.5s，超时留余量
_CONNECT_TIMEOUT_S = 12.0


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("SLECG Host — ECG 上位机")
        self.resize(1000, 700)

        self._converter = EcgConverter()
        self._buffer = EcgBuffer(window_seconds=10.0)
        self._parser = FrameParser()
        self._recorder = EcgRecorder(self._converter, output_dir=Path.cwd() / "recordings")

        self._serial = SerialTransport()
        self._ble = BleTransport()
        self._transport: Transport = self._serial
        self._mode = TransportMode.SERIAL
        self._plot_mode = DisplayMode.RAW
        self._bridge = TransportBridge()
        self._executor = ThreadPoolExecutor(max_workers=2, thread_name_prefix="slecg-host")

        self._bytes_in = 0
        self._frames_in = 0
        self._ecg_packets = 0
        self._last_rx_log_bytes = 0
        self._connected = False

        self._conn_panel = ConnectionPanel()
        self._plot = EcgPlotWidget(self._converter)
        self._control = ControlPanel()
        self._status = StatusPanel()

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self._conn_panel)
        layout.addWidget(self._plot, stretch=1)
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
        self._control.record_toggled.connect(self._toggle_recording)

        self._plot.display_mode_changed.connect(self._on_display_mode_changed)

    @pyqtSlot(TransportMode)
    def _on_transport_changed(self, mode: TransportMode) -> None:
        if self._transport.is_open():
            self._disconnect()
        self._mode = mode
        self._transport = self._ble if mode == TransportMode.BLE else self._serial
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
            self.statusBar().showMessage("未扫描到 ESP_SLECG 设备", 5000)
            logger.warning("BLE 扫描结果为空")
        else:
            self.statusBar().showMessage(f"已刷新 {len(items)} 个设备", 2000)
            logger.info("设备列表已刷新: %d 项", len(items))

    @pyqtSlot(str)
    def _on_refresh_failed(self, message: str) -> None:
        self._refresh_btn_busy(False)
        self.statusBar().showMessage(f"刷新失败: {message}", 5000)
        logger.error("刷新失败: %s", message)

    def _refresh_btn_busy(self, busy: bool) -> None:
        self._conn_panel.set_refresh_enabled(not busy)
        if busy:
            self.statusBar().showMessage("正在扫描…")

    def _connect(self) -> None:
        device_id = self._conn_panel.selected_device_id
        if not device_id:
            QMessageBox.warning(self, "连接", "请先选择端口或蓝牙设备")
            return
        if self._connected:
            logger.warning("已处于连接状态，忽略重复连接请求")
            return

        logger.info("用户请求连接: device=%s mode=%s", device_id, self._mode.value)
        self._conn_panel.set_connecting(True)
        self.statusBar().showMessage(f"正在连接 {device_id} …")

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
            self.statusBar().showMessage(
                f"已连接 {device_id} — 打开串口会复位芯片，请等绿灯常亮后"
                "再单击按键开始采集（绿灯闪烁后应出现波形）",
                12000,
            )
        else:
            self.statusBar().showMessage(f"已连接: {device_id}", 5000)

    @pyqtSlot(str)
    def _on_connect_failed(self, message: str) -> None:
        self._connected = False
        self._conn_panel.set_connecting(False)
        self._conn_panel.set_connected(False)
        self.statusBar().showMessage(f"连接失败: {message}", 8000)
        logger.error("UI 连接失败提示: %s", message)
        QMessageBox.critical(self, "连接失败", message)

    @pyqtSlot(str)
    def _on_transport_lost(self, reason: str) -> None:
        logger.warning("传输层断开: %s", reason)
        if self._transport.is_open() or self._connected:
            self._disconnect()
        self.statusBar().showMessage(f"连接已断开: {reason}", 8000)
        QMessageBox.warning(
            self,
            "连接断开",
            f"{reason}\n\n若正在使用 idf_monitor，请勿与上位机同时占用同一串口。",
        )

    def _disconnect(self) -> None:
        logger.info("用户/系统请求断开")
        if self._recorder.is_recording:
            self._stop_recording()
        if isinstance(self._transport, SerialTransport):
            self._transport.set_lost_callback(None)
        self._transport.set_data_callback(None)
        self._transport.close()
        self._connected = False
        self._conn_panel.set_connecting(False)
        self._conn_panel.set_connected(False)
        self._control.set_ble_enabled(False)
        self._status.clear_status()
        self.statusBar().showMessage("已断开", 3000)
        logger.info("已主动断开连接")

    def _update_rx_status(self) -> None:
        if not self._connected:
            return
        self.statusBar().showMessage(
            f"已连接 | RX {self._bytes_in} B | 帧 {self._frames_in} | "
            f"ECG包 {self._ecg_packets} | 缓存 {self._parser.cache_size} B"
        )

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
            self._ecg_packets += 1
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
        snap = self._buffer.snapshot()
        self._plot.update_data(snap.times, snap.raw)

    def _send_start(self) -> None:
        try:
            logger.info("发送 START_ACQ")
            self._transport.write(build_start_acq())
        except Exception as exc:  # noqa: BLE001
            logger.error("START 失败: %s", exc)
            QMessageBox.warning(self, "指令", str(exc))

    def _send_stop(self) -> None:
        try:
            logger.info("发送 STOP_ACQ")
            self._transport.write(build_stop_acq())
        except Exception as exc:  # noqa: BLE001
            logger.error("STOP 失败: %s", exc)
            QMessageBox.warning(self, "指令", str(exc))

    def _send_req_status(self) -> None:
        try:
            logger.info("发送 REQ_STATUS")
            self._transport.write(build_req_status())
        except Exception as exc:  # noqa: BLE001
            logger.error("REQ_STATUS 失败: %s", exc)
            QMessageBox.warning(self, "指令", str(exc))

    def _toggle_recording(self, active: bool) -> None:
        if active:
            self._start_recording()
        else:
            self._stop_recording()

    def _start_recording(self) -> None:
        try:
            path = self._recorder.start()
            self._control.set_recording(True, str(path))
            self.statusBar().showMessage(f"录制中: {path}", 5000)
            logger.info("开始录制: %s", path)
        except Exception as exc:  # noqa: BLE001
            self._control.set_recording(False)
            QMessageBox.warning(self, "录制", str(exc))

    def _stop_recording(self) -> None:
        path = self._recorder.current_path
        self._recorder.stop()
        self._control.set_recording(False)
        if path:
            self.statusBar().showMessage(f"录制已保存: {path}", 5000)
            logger.info("录制结束: %s", path)

    @pyqtSlot(DisplayMode)
    def _on_display_mode_changed(self, mode: DisplayMode) -> None:
        self._plot_mode = mode
        self._refresh_plot()

    def closeEvent(self, event) -> None:  # noqa: ANN001, N802
        logger.info("主窗口关闭")
        self._disconnect()
        self._executor.shutdown(wait=False, cancel_futures=True)
        super().closeEvent(event)
