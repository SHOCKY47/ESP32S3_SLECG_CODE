"""Async CSV recorder for ECG samples."""

from __future__ import annotations

import csv
import queue
import threading
from datetime import datetime
from pathlib import Path

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
        path = self._output_dir / f"ecg_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
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
