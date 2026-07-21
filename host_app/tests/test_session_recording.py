from pathlib import Path

import numpy as np

from slecg_host.ecg.buffer import EcgBuffer
from slecg_host.ecg.converter import EcgConverter
from slecg_host.ecg.recorder import EcgRecorder, load_recording
from slecg_host.protocol.payloads import EcgPacket


def _packet(seq: int, ts_ms: int, sample: int = 123) -> EcgPacket:
    return EcgPacket(
        seq=seq,
        ts_ms=ts_ms,
        n_samples=25,
        loff=0,
        samples=(sample,) * 25,
    )


def test_full_history_and_five_second_recent_window():
    buffer = EcgBuffer(window_seconds=5.0)
    for seq in range(70):  # 7 seconds at 10 packets/s
        buffer.add_packet(_packet(seq, seq * 100))
    assert len(buffer.snapshot().raw) == 1750
    assert len(buffer.recent_snapshot().raw) == 1250


def test_auto_recording_round_trip(tmp_path: Path):
    recorder = EcgRecorder(EcgConverter(), output_dir=tmp_path)
    path = recorder.start()
    recorder.add_packet(_packet(0, 1000, sample=-321))
    recorder.stop()

    assert path.parent == tmp_path
    assert path.suffix == ".csv"
    times, raw = load_recording(path)
    assert len(times) == 25
    assert np.isclose(times[0], 0.0)
    assert raw.tolist() == [-321] * 25
