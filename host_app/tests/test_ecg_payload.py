"""Tests for ECG payload and buffer."""

from slecg_host.ecg.buffer import EcgBuffer
from slecg_host.ecg.converter import DisplayMode, EcgConverter
from slecg_host.protocol.payloads import EcgPacket


def test_ecg_buffer_seq_gap():
    buf = EcgBuffer(window_seconds=1.0)
    p0 = EcgPacket(seq=0, ts_ms=0, n_samples=25, loff=0, samples=tuple(range(25)))
    p2 = EcgPacket(seq=2, ts_ms=50, n_samples=25, loff=0, samples=tuple(range(25, 50)))
    buf.add_packet(p0)
    buf.add_packet(p2)
    snap = buf.snapshot()
    assert snap.dropped_packets == 1
    assert len(snap.raw) == 50


def test_mv_converter():
    conv = EcgConverter(vref=4.033, gain=1)
    mv = conv.to_mv(32767)
    assert mv > 0
    assert conv.axis_label(DisplayMode.MV) == "Amplitude (mV)"
    assert conv.axis_label(DisplayMode.RAW) == "Amplitude (int16)"
