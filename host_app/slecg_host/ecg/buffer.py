"""Growable ECG session buffer with seq-based gap detection."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from slecg_host.protocol.constants import SLECG_ECG_SAMPLES_PER_PKT, SLECG_SAMPLE_RATE_HZ
from slecg_host.protocol.payloads import EcgPacket


@dataclass(frozen=True)
class EcgBufferSnapshot:
    times: np.ndarray
    raw: np.ndarray
    loff: int
    dropped_packets: int


class EcgBuffer:
    def __init__(self, window_seconds: float = 5.0) -> None:
        self._window_samples = int(SLECG_SAMPLE_RATE_HZ * window_seconds)
        self._capacity = max(self._window_samples * 12, SLECG_SAMPLE_RATE_HZ * 60)
        self._times = np.zeros(self._capacity, dtype=np.float64)
        self._raw = np.zeros(self._capacity, dtype=np.int16)
        self._count = 0
        self._last_seq: int | None = None
        self._dropped_packets = 0
        self._last_loff = 0
        self._t0: float | None = None

    @property
    def dropped_packets(self) -> int:
        return self._dropped_packets

    def reset(self) -> None:
        self._count = 0
        self._last_seq = None
        self._dropped_packets = 0
        self._last_loff = 0
        self._t0 = None
        # 已分配数组可复用；长会话结束后恢复到一分钟初始容量。
        if self._capacity > SLECG_SAMPLE_RATE_HZ * 60:
            self._capacity = max(self._window_samples * 12, SLECG_SAMPLE_RATE_HZ * 60)
            self._times = np.zeros(self._capacity, dtype=np.float64)
            self._raw = np.zeros(self._capacity, dtype=np.int16)

    def _ensure_capacity(self, required: int) -> None:
        if required <= self._capacity:
            return
        new_capacity = max(required, self._capacity * 2)
        times = np.zeros(new_capacity, dtype=np.float64)
        raw = np.zeros(new_capacity, dtype=np.int16)
        times[: self._count] = self._times[: self._count]
        raw[: self._count] = self._raw[: self._count]
        self._times = times
        self._raw = raw
        self._capacity = new_capacity

    def add_packet(self, pkt: EcgPacket) -> None:
        if pkt.n_samples != SLECG_ECG_SAMPLES_PER_PKT:
            pass  # warn handled upstream

        if self._last_seq is not None:
            expected = (self._last_seq + 1) & 0xFFFF
            if pkt.seq != expected:
                gap = (pkt.seq - expected) & 0xFFFF
                if gap < 0x8000:
                    self._dropped_packets += gap
        self._last_seq = pkt.seq
        self._last_loff = pkt.loff

        dt_ms = 1000.0 / SLECG_SAMPLE_RATE_HZ
        self._ensure_capacity(self._count + len(pkt.samples))
        for i, sample in enumerate(pkt.samples):
            t_ms = pkt.ts_ms + i * dt_ms
            if self._t0 is None:
                self._t0 = t_ms / 1000.0
            t_sec = (t_ms / 1000.0) - self._t0

            self._times[self._count] = t_sec
            self._raw[self._count] = sample
            self._count += 1

    def load_samples(self, times: np.ndarray, raw: np.ndarray) -> None:
        if len(times) != len(raw):
            raise ValueError("times/raw length mismatch")
        self.reset()
        self._ensure_capacity(len(times))
        self._times[: len(times)] = times
        self._raw[: len(raw)] = raw
        self._count = len(times)

    def snapshot(self) -> EcgBufferSnapshot:
        if self._count == 0:
            return EcgBufferSnapshot(
                times=np.array([], dtype=np.float64),
                raw=np.array([], dtype=np.int16),
                loff=self._last_loff,
                dropped_packets=self._dropped_packets,
            )
        times = self._times[: self._count].copy()
        raw = self._raw[: self._count].copy()
        return EcgBufferSnapshot(
            times=times,
            raw=raw,
            loff=self._last_loff,
            dropped_packets=self._dropped_packets,
        )

    def recent_snapshot(self) -> EcgBufferSnapshot:
        snap = self.snapshot()
        if len(snap.times) <= self._window_samples:
            return snap
        return EcgBufferSnapshot(
            times=snap.times[-self._window_samples :],
            raw=snap.raw[-self._window_samples :],
            loff=snap.loff,
            dropped_packets=snap.dropped_packets,
        )
