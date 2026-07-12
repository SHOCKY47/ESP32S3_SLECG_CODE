"""Real-time ECG plot with Y-axis click toggle."""

from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PyQt6.QtCore import pyqtSignal

from slecg_host.ecg.converter import DisplayMode, EcgConverter


class EcgPlotWidget(pg.PlotWidget):
    display_mode_changed = pyqtSignal(DisplayMode)

    def __init__(self, converter: EcgConverter, parent=None) -> None:  # noqa: ANN001
        super().__init__(parent=parent)
        self._converter = converter
        self._mode = DisplayMode.RAW
        self._window_seconds = 10.0

        self.setBackground("w")
        self.showGrid(x=True, y=True, alpha=0.3)
        self.setLabel("bottom", "Time", units="s")
        self._update_axis_label()
        # 旧固定 ±5000 会把正常大码值画成“冲顶”；改为按数据自适应
        self.setYRange(-32768, 32767)
        self.setXRange(0, self._window_seconds)
        self.disableAutoRange(axis="y")

        self._curve = self.plot(pen=pg.mkPen(color="#0066cc", width=1))
        self._axis_click_proxy = pg.SignalProxy(
            self.getAxis("left").scene().sigMouseClicked,
            rateLimit=20,
            slot=self._on_axis_click,
        )

    @property
    def display_mode(self) -> DisplayMode:
        return self._mode

    def toggle_display_mode(self) -> DisplayMode:
        self._mode = DisplayMode.MV if self._mode == DisplayMode.RAW else DisplayMode.RAW
        self._update_axis_label()
        self.display_mode_changed.emit(self._mode)
        return self._mode

    def _update_axis_label(self) -> None:
        self.setLabel("left", self._converter.axis_label(self._mode))
        hint = "点击 Y 轴切换 raw/mV" if self._mode == DisplayMode.RAW else "Click Y-axis to toggle raw/mV"
        self.setTitle(f"ECG Waveform — {self._mode.value.upper()} ({hint})")

    def _on_axis_click(self, event) -> None:  # noqa: ANN001
        pos = event[0].scenePos()
        if self.getAxis("left").sceneBoundingRect().contains(pos):
            self.toggle_display_mode()

    def update_data(self, times: np.ndarray, raw: np.ndarray) -> None:
        if len(times) == 0:
            self._curve.setData([], [])
            return
        if self._mode == DisplayMode.RAW:
            y = raw.astype(np.float64)
            min_span = 200.0
        else:
            y = raw.astype(np.float64) * self._converter._scale  # noqa: SLF001
            min_span = 2.0  # mV
        t_max = float(times[-1])
        t_min = max(0.0, t_max - self._window_seconds)
        self.setXRange(t_min, max(t_min + self._window_seconds, t_max))
        self._curve.setData(times, y)

        y_min = float(np.min(y))
        y_max = float(np.max(y))
        span = max(y_max - y_min, min_span)
        pad = 0.15 * span
        mid = 0.5 * (y_min + y_max)
        self.setYRange(mid - 0.5 * span - pad, mid + 0.5 * span + pad)
