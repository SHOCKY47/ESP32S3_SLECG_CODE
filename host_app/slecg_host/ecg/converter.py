"""int16 to mV conversion — ADS1291 gain=6, Vref=2.42V, shift=8."""

from __future__ import annotations

from enum import Enum


class DisplayMode(Enum):
    RAW = "raw"
    MV = "mv"


class EcgConverter:
    def __init__(self, vref: float = 2.42, gain: int = 6) -> None:
        self.vref = vref
        self.gain = gain
        # shift=8 时 int16 近似覆盖 24-bit 满幅：mV = raw * Vref*1000 / (gain * 2^15)
        self._scale = (vref * 1000.0) / (gain * 32768.0)

    def to_mv(self, raw: int) -> float:
        return raw * self._scale

    def to_mv_array(self, raw_values: list[int]) -> list[float]:
        return [self.to_mv(v) for v in raw_values]

    def axis_label(self, mode: DisplayMode) -> str:
        if mode == DisplayMode.RAW:
            return "Amplitude (int16)"
        return "Amplitude (mV)"
