"""Async CSV recorder for ECG samples."""

from __future__ import annotations

import csv
import queue
import threading
from datetime import datetime
from pathlib import Path

import numpy as np

from slecg_host.ecg.converter import EcgConverter
from slecg_host.protocol.constants import SLECG_SAMPLE_RATE_HZ
from slecg_host.protocol.payloads import EcgPacket


class EcgRecorder:
    def __init__(self, converter: EcgConverter, output_dir: str | Path = ".") -> None:
        self._converter = converter
        self._output_dir = Path(output_dir)
        self._queue: queue.Queue[tuple | None] = queue.Queue()
        self._thread: threading.Thread | None = None
        self._file = None
        self._writer: csv.writer | None = None
        self._active = False

    @property
    def is_recording(self) -> bool:
        return self._active

    @property
    def current_path(self) -> Path | None:
        if self._file:
            return Path(self._file.name)
        return None

    def start(self) -> Path:
        if self._active:
            raise RuntimeError("Already recording")
        self._output_dir.mkdir(parents=True, exist_ok=True)
        stem = datetime.now().strftime("%Y%m%d_%H%M")
        path = self._output_dir / f"{stem}.csv"
        suffix = 1
        while path.exists():
            path = self._output_dir / f"{stem}_{suffix:02d}.csv"
            suffix += 1
        self._file = open(path, "w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(
            ["timestamp_ms", "seq", "sample_index", "raw_int16", "mv", "loff"]
        )
        self._active = True
        self._thread = threading.Thread(target=self._write_loop, daemon=True)
        self._thread.start()
        return path

    def stop(self) -> None:
        if not self._active:
            return
        self._active = False
        self._queue.put(None)
        if self._thread:
            self._thread.join(timeout=2.0)
        self._thread = None
        if self._file:
            self._file.close()
            self._file = None
        self._writer = None

    def add_packet(self, pkt: EcgPacket) -> None:
        if not self._active:
            return
        dt_ms = 1000.0 / float(SLECG_SAMPLE_RATE_HZ)
        for i, sample in enumerate(pkt.samples):
            ts = pkt.ts_ms + int(i * dt_ms)
            mv = self._converter.to_mv(sample)
            self._queue.put((ts, pkt.seq, i, sample, mv, pkt.loff))

    def _write_loop(self) -> None:
        while True:
            item = self._queue.get()
            if item is None:
                break
            if self._writer:
                self._writer.writerow(item)
            self._queue.task_done()


def load_recording(path: str | Path) -> tuple[np.ndarray, np.ndarray]:
    """Load recorder CSV and return session-relative seconds plus raw int16."""
    timestamps: list[float] = []
    raw_values: list[int] = []
    with open(path, newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        required = {"timestamp_ms", "raw_int16"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("不是有效的 SLECG testdata CSV")
        for row in reader:
            timestamps.append(float(row["timestamp_ms"]))
            raw_values.append(int(row["raw_int16"]))
    if not timestamps:
        raise ValueError("记录文件中没有采样数据")
    first = timestamps[0]
    times = (np.asarray(timestamps, dtype=np.float64) - first) / 1000.0
    raw = np.asarray(raw_values, dtype=np.int16)
    return times, raw
