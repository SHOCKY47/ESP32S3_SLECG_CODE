"""Ring buffer for ECG samples with seq-based gap detection."""

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
    def __init__(self, window_seconds: float = 10.0) -> None:
        self._capacity = int(SLECG_SAMPLE_RATE_HZ * window_seconds)
        self._times = np.zeros(self._capacity, dtype=np.float64)
        self._raw = np.zeros(self._capacity, dtype=np.int16)
        self._count = 0
        self._write_idx = 0
        self._last_seq: int | None = None
        self._dropped_packets = 0
        self._last_loff = 0
        self._t0: float | None = None

    @property
    def dropped_packets(self) -> int:
        return self._dropped_packets

    def reset(self) -> None:
        self._count = 0
        self._write_idx = 0
        self._last_seq = None
        self._dropped_packets = 0
        self._last_loff = 0
        self._t0 = None
        self._raw.fill(0)
        self._times.fill(0.0)

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
        for i, sample in enumerate(pkt.samples):
            t_ms = pkt.ts_ms + i * dt_ms
            if self._t0 is None:
                self._t0 = t_ms / 1000.0
            t_sec = (t_ms / 1000.0) - self._t0

            self._times[self._write_idx] = t_sec
            self._raw[self._write_idx] = sample
            self._write_idx = (self._write_idx + 1) % self._capacity
            if self._count < self._capacity:
                self._count += 1

    def snapshot(self) -> EcgBufferSnapshot:
        if self._count == 0:
            return EcgBufferSnapshot(
                times=np.array([], dtype=np.float64),
                raw=np.array([], dtype=np.int16),
                loff=self._last_loff,
                dropped_packets=self._dropped_packets,
            )
        if self._count < self._capacity:
            times = self._times[: self._count].copy()
            raw = self._raw[: self._count].copy()
        else:
            times = np.concatenate(
                (self._times[self._write_idx :], self._times[: self._write_idx])
            )
            raw = np.concatenate((self._raw[self._write_idx :], self._raw[: self._write_idx]))
        return EcgBufferSnapshot(
            times=times,
            raw=raw,
            loff=self._last_loff,
            dropped_packets=self._dropped_packets,
        )
