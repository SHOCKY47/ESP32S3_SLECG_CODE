"""Tests for SLECG frame parser."""

from slecg_host.protocol.constants import (
    SLECG_ECG_PAYLOAD_LEN,
    SLECG_TYPE_ECG_DATA,
    SLECG_TYPE_REQ_STATUS,
)
from slecg_host.protocol.frame import FrameParser, build_frame
from slecg_host.protocol.payloads import (
    build_start_acq,
    build_stop_acq,
    parse_ecg_payload,
)


def _build_ecg_example_frame() -> bytes:
    """ECG frame from ble_protocol/packets/ecg_data.md: seq=0, ts=1000, n=25, loff=0, samples=0."""
    payload = bytearray(SLECG_ECG_PAYLOAD_LEN)
    payload[0:2] = (0).to_bytes(2, "little")
    payload[2:6] = (1000).to_bytes(4, "little")
    payload[6] = 25
    payload[7] = 0
    return build_frame(SLECG_TYPE_ECG_DATA, bytes(payload))


def test_build_req_status():
    frame = build_frame(SLECG_TYPE_REQ_STATUS)
    assert frame == bytes.fromhex("A5 5A 12 00 00 5A A5")


def test_build_start_stop():
    assert build_start_acq() == bytes.fromhex("A5 5A 10 02 00 00 00 5A A5")
    assert build_stop_acq() == bytes.fromhex("A5 5A 11 01 00 00 5A A5")


def test_parse_ecg_example():
    frame = _build_ecg_example_frame()
    parser = FrameParser()
    frames = parser.feed(frame)
    assert len(frames) == 1
    assert frames[0].type == SLECG_TYPE_ECG_DATA
    pkt = parse_ecg_payload(frames[0].payload)
    assert pkt.seq == 0
    assert pkt.ts_ms == 1000
    assert pkt.n_samples == 25
    assert pkt.loff == 0
    assert len(pkt.samples) == 25
    assert all(s == 0 for s in pkt.samples)


def test_sticky_packets():
    frame = _build_ecg_example_frame()
    parser = FrameParser()
    part1 = frame[:20]
    part2 = frame[20:]
    assert parser.feed(part1) == []
    frames = parser.feed(part2)
    assert len(frames) == 1


def test_concatenated_frames():
    f1 = build_frame(SLECG_TYPE_REQ_STATUS)
    f2 = _build_ecg_example_frame()
    parser = FrameParser()
    frames = parser.feed(f1 + f2)
    assert len(frames) == 2
    assert frames[0].type == SLECG_TYPE_REQ_STATUS
    assert frames[1].type == SLECG_TYPE_ECG_DATA


def test_invalid_footer_resync():
    bad = bytearray(_build_ecg_example_frame())
    bad[-2] = 0x00
    good = _build_ecg_example_frame()
    parser = FrameParser()
    frames = parser.feed(bytes(bad) + good)
    assert any(f.type == SLECG_TYPE_ECG_DATA for f in frames)
