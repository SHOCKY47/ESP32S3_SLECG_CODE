"""Real-time ECG plot with Y-axis click toggle."""

from __future__ import annotations

import numpy as np
import pyqtgraph as pg
from PyQt6.QtCore import pyqtSignal

from slecg_host.ecg.converter import DisplayMode, EcgConverter


class EcgPlotWidget(pg.PlotWidget):
    display_mode_changed = pyqtSignal(DisplayMode)

    def __init__(
        self,
        converter: EcgConverter,
        parent=None,  # noqa: ANN001
        *,
        processed: bool = False,
    ) -> None:
        super().__init__(parent=parent)
        self._converter = converter
        self._language = "zh"
        self._theme = "dark"
        self._text_color = "#8fffb6"
        self._title_color = "#00ff88"
        self._processed = processed
        self._mode = DisplayMode.RAW
        self._window_seconds = 5.0
        self._history_mode = False
        self._display_times = np.array([], dtype=np.float64)
        self._display_y = np.array([], dtype=np.float64)

        self.setBackground("#020705")
        self.showGrid(x=True, y=True, alpha=0.22)
        axis_pen = pg.mkPen("#397a55", width=1)
        text_pen = pg.mkPen("#8fffb6")
        for axis_name in ("left", "bottom"):
            axis = self.getAxis(axis_name)
            axis.setPen(axis_pen)
            axis.setTextPen(text_pen)
        self.setLabel("bottom", "TIME", units="s", color="#8fffb6")
        self._update_axis_label()
        # 旧固定 ±5000 会把正常大码值画成“冲顶”；改为按数据自适应
        self.setYRange(-32768, 32767)
        self.setXRange(0, self._window_seconds)
        self.disableAutoRange(axis="y")

        self._curve = self.plot(
            pen=pg.mkPen(color="#00ff66", width=2.6),
            antialias=True,
        )
        self._r_markers = pg.ScatterPlotItem(
            size=8,
            pen=pg.mkPen("#ffd166", width=1.2),
            brush=pg.mkBrush("#ff9f1c"),
            symbol="o",
        )
        self.addItem(self._r_markers)
        self._r_markers.setVisible(processed)
        self._axis_click_proxy = pg.SignalProxy(
            self.getAxis("left").scene().sigMouseClicked,
            rateLimit=20,
            slot=self._on_axis_click,
        )
        self.getViewBox().setMouseEnabled(x=False, y=False)
        self.getViewBox().sigXRangeChanged.connect(self._on_x_range_changed)
        self.set_theme("dark")

    @property
    def display_mode(self) -> DisplayMode:
        return self._mode

    def set_language(self, language: str) -> None:
        self._language = language
        self.setLabel(
            "bottom",
            "时间" if language == "zh" else "TIME",
            units="s",
            color=self._text_color,
        )
        self._update_axis_label()

    def set_theme(self, theme: str) -> None:
        self._theme = theme
        light = theme == "light"
        background = "#ffffff" if light else "#020705"
        wave = "#e87514" if light else "#00ff66"
        axis_color = "#3e4650" if light else "#397a55"
        self._text_color = "#20262d" if light else "#8fffb6"
        self._title_color = "#b85300" if light else "#00ff88"
        self.setBackground(background)
        self.showGrid(x=True, y=True, alpha=0.18 if light else 0.22)
        for axis_name in ("left", "bottom"):
            axis = self.getAxis(axis_name)
            axis.setPen(pg.mkPen(axis_color, width=1))
            axis.setTextPen(pg.mkPen(self._text_color))
        self._curve.setPen(pg.mkPen(wave, width=2.6))
        if light:
            self._r_markers.setPen(pg.mkPen("#1956a3", width=1.2))
            self._r_markers.setBrush(pg.mkBrush("#2878d0"))
        else:
            self._r_markers.setPen(pg.mkPen("#ffd166", width=1.2))
            self._r_markers.setBrush(pg.mkBrush("#ff9f1c"))
        self.set_language(self._language)

    def toggle_display_mode(self) -> DisplayMode:
        self._mode = DisplayMode.MV if self._mode == DisplayMode.RAW else DisplayMode.RAW
        self._update_axis_label()
        self.display_mode_changed.emit(self._mode)
        return self._mode

    def set_display_mode(self, mode: DisplayMode) -> None:
        self._mode = mode
        self._update_axis_label()

    def _update_axis_label(self) -> None:
        zh = self._language == "zh"
        axis_label = (
            "幅值（原始码）" if self._mode == DisplayMode.RAW else "幅值（mV）"
        ) if zh else self._converter.axis_label(self._mode)
        self.setLabel("left", axis_label, color=self._text_color)
        hint = "点击Y轴切换单位" if zh else "CLICK Y-AXIS TO TOGGLE UNITS"
        if self._processed:
            title = "优化处理后" if zh else "OPTIMIZED ECG"
        else:
            title = "原始心电波形" if zh else "RAW ECG SIGNAL"
        self.setTitle(
            f"{title}  //  {self._mode.value.upper()}  //  {hint}",
            color=self._title_color,
            size="12pt",
        )

    def update_r_peaks(
        self,
        times: np.ndarray,
        optimized_raw: np.ndarray,
        peak_indices: np.ndarray,
    ) -> None:
        if not self._processed or len(peak_indices) == 0:
            self._r_markers.setData([], [])
            return
        valid = peak_indices[(peak_indices >= 0) & (peak_indices < len(times))]
        peak_y = optimized_raw[valid].astype(np.float64)
        if self._mode == DisplayMode.MV:
            peak_y *= self._converter._scale  # noqa: SLF001
        self._r_markers.setData(times[valid], peak_y)

    def _on_axis_click(self, event) -> None:  # noqa: ANN001
        pos = event[0].scenePos()
        if self.getAxis("left").sceneBoundingRect().contains(pos):
            self.toggle_display_mode()

    def set_history_mode(self, enabled: bool) -> None:
        """History mode freezes auto-follow and enables horizontal drag."""
        self._history_mode = enabled
        self.getViewBox().setMouseEnabled(x=enabled, y=False)

    def _on_x_range_changed(self, *_args) -> None:  # noqa: ANN002
        if self._history_mode:
            self._fit_visible_y()

    def update_data(
        self,
        times: np.ndarray,
        raw: np.ndarray,
        *,
        follow_live: bool = True,
    ) -> None:
        if len(times) == 0:
            self._curve.setData([], [])
            return
        if self._mode == DisplayMode.RAW:
            y = raw.astype(np.float64)
            min_span = 200.0
        else:
            y = raw.astype(np.float64) * self._converter._scale  # noqa: SLF001
            min_span = 2.0  # mV
        self._display_times = times
        self._display_y = y
        t_max = float(times[-1])
        t_min = max(0.0, t_max - self._window_seconds)
        self._curve.setData(times, y)

        if follow_live:
            self.setXRange(t_min, max(t_min + self._window_seconds, t_max), padding=0)
            self._fit_y(y, min_span)
        else:
            self._fit_visible_y()

    def _fit_visible_y(self) -> None:
        if len(self._display_times) == 0:
            return
        x_min, x_max = self.getViewBox().viewRange()[0]
        visible = (self._display_times >= x_min) & (self._display_times <= x_max)
        y = self._display_y[visible]
        if len(y) == 0:
            return
        self._fit_y(y, 200.0 if self._mode == DisplayMode.RAW else 2.0)

    def _fit_y(self, y: np.ndarray, min_span: float) -> None:
        y_min = float(np.min(y))
        y_max = float(np.max(y))
        span = max(y_max - y_min, min_span)
        pad = 0.15 * span
        mid = 0.5 * (y_min + y_max)
        self.setYRange(mid - 0.5 * span - pad, mid + 0.5 * span + pad)
