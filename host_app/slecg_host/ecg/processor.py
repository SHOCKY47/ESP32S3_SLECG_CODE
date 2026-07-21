"""Desktop ECG enhancement and Pan-Tompkins-inspired R-peak detection."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy.signal import butter, find_peaks, savgol_filter, sosfiltfilt

from slecg_host.protocol.constants import SLECG_SAMPLE_RATE_HZ


@dataclass(frozen=True)
class EcgAnalysis:
    times: np.ndarray
    optimized_raw: np.ndarray
    r_peaks: np.ndarray
    heart_rate_bpm: float | None


class EcgProcessor:
    """Zero-phase display filtering plus robust RR-derived heart rate."""

    def __init__(self, sample_rate_hz: int = SLECG_SAMPLE_RATE_HZ) -> None:
        self.fs = float(sample_rate_hz)
        self._display_sos = butter(3, (0.5, 35.0), btype="bandpass", fs=self.fs, output="sos")
        self._qrs_sos = butter(2, (5.0, 18.0), btype="bandpass", fs=self.fs, output="sos")

    def process(self, times: np.ndarray, raw: np.ndarray) -> EcgAnalysis:
        times = np.asarray(times, dtype=np.float64)
        signal = np.asarray(raw, dtype=np.float64)
        if len(times) != len(signal):
            raise ValueError("times/raw length mismatch")
        if len(signal) < int(self.fs * 1.5):
            centered = signal - np.median(signal) if len(signal) else signal.copy()
            return EcgAnalysis(times, centered, np.array([], dtype=np.int64), None)

        optimized = sosfiltfilt(self._display_sos, signal)
        # 约36 ms短窗，只消除像素级毛刺，不跨越典型QRS宽度进行拟合。
        smooth_window = max(5, int(round(self.fs * 0.036)) | 1)
        optimized = savgol_filter(optimized, smooth_window, polyorder=3, mode="interp")

        peaks = self._detect_r_peaks(optimized)
        heart_rate = self._heart_rate(peaks)
        return EcgAnalysis(times, optimized, peaks, heart_rate)

    def _detect_r_peaks(self, optimized: np.ndarray) -> np.ndarray:
        qrs = sosfiltfilt(self._qrs_sos, optimized)
        slope = np.gradient(qrs)
        energy = slope * slope
        integrate_n = max(3, int(round(self.fs * 0.12)))
        envelope = np.convolve(energy, np.ones(integrate_n) / integrate_n, mode="same")

        median = float(np.median(envelope))
        mad = float(np.median(np.abs(envelope - median)))
        threshold = median + 3.0 * max(mad, np.finfo(float).eps)
        prominence = max(2.0 * mad, float(np.max(envelope)) * 0.025)
        candidates, _ = find_peaks(
            envelope,
            height=threshold,
            prominence=prominence,
            distance=max(1, int(self.fs * 0.30)),
        )

        radius = max(1, int(self.fs * 0.12))
        refined: list[int] = []
        for candidate in candidates:
            lo = max(0, candidate - radius)
            hi = min(len(optimized), candidate + radius + 1)
            local = optimized[lo:hi]
            if len(local):
                refined.append(lo + int(np.argmax(np.abs(local))))

        # 积分峰可能指向同一个QRS；精定位后再次执行生理不应期去重。
        refractory = int(self.fs * 0.30)
        unique: list[int] = []
        for peak in sorted(refined):
            if not unique or peak - unique[-1] >= refractory:
                unique.append(peak)
            elif abs(optimized[peak]) > abs(optimized[unique[-1]]):
                unique[-1] = peak
        return np.asarray(unique, dtype=np.int64)

    def _heart_rate(self, peaks: np.ndarray) -> float | None:
        if len(peaks) < 2:
            return None
        rr = np.diff(peaks) / self.fs
        valid = rr[(rr >= 0.30) & (rr <= 2.0)]  # 30–200 bpm
        if len(valid) == 0:
            return None
        recent = valid[-5:]
        return 60.0 / float(np.median(recent))
