"""Business payload parse/build helpers."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .constants import (
    SLECG_ECG_PAYLOAD_LEN,
    SLECG_ECG_SAMPLES_PER_PKT,
    SLECG_START_MODE_NORMAL,
    SLECG_STATUS_PAYLOAD_LEN,
    SLECG_TYPE_REQ_STATUS,
    SLECG_TYPE_START_ACQ,
    SLECG_TYPE_STOP_ACQ,
)
from .frame import build_frame


@dataclass(frozen=True)
class EcgPacket:
    seq: int
    ts_ms: int
    n_samples: int
    loff: int
    samples: tuple[int, ...]


@dataclass(frozen=True)
class DeviceStatus:
    state: int
    error_code: int
    sample_rate_hz: int
    ecg_seq: int
    uptime_ms: int
    fw_version: int

    @property
    def fw_version_str(self) -> str:
        major = (self.fw_version >> 8) & 0xFF
        minor = self.fw_version & 0xFF
        return f"v{major}.{minor}"


@dataclass(frozen=True)
class AckPacket:
    orig_type: int
    result: int


@dataclass(frozen=True)
class NackPacket:
    orig_type: int
    error: int


def parse_ecg_payload(payload: bytes) -> EcgPacket:
    if len(payload) < 8:
        raise ValueError(f"ECG payload too short: {len(payload)}")
    seq = struct.unpack_from("<H", payload, 0)[0]
    ts_ms = struct.unpack_from("<I", payload, 2)[0]
    n = payload[6]
    loff = payload[7]
    samples: list[int] = []
    for i in range(n):
        off = 8 + i * 2
        if off + 2 > len(payload):
            break
        samples.append(struct.unpack_from("<h", payload, off)[0])
    return EcgPacket(seq=seq, ts_ms=ts_ms, n_samples=n, loff=loff, samples=tuple(samples))


def parse_device_status(payload: bytes) -> DeviceStatus:
    if len(payload) < SLECG_STATUS_PAYLOAD_LEN:
        raise ValueError(f"STATUS payload too short: {len(payload)}")
    state = payload[0]
    error_code = payload[1]
    sample_rate_hz = struct.unpack_from("<H", payload, 2)[0]
    ecg_seq = struct.unpack_from("<H", payload, 4)[0]
    uptime_ms = struct.unpack_from("<I", payload, 6)[0]
    fw_version = struct.unpack_from("<H", payload, 10)[0]
    return DeviceStatus(
        state=state,
        error_code=error_code,
        sample_rate_hz=sample_rate_hz,
        ecg_seq=ecg_seq,
        uptime_ms=uptime_ms,
        fw_version=fw_version,
    )


def parse_ack(payload: bytes) -> AckPacket:
    if len(payload) < 2:
        raise ValueError("ACK payload too short")
    return AckPacket(orig_type=payload[0], result=payload[1])


def parse_nack(payload: bytes) -> NackPacket:
    if len(payload) < 2:
        raise ValueError("NACK payload too short")
    return NackPacket(orig_type=payload[0], error=payload[1])


def build_start_acq(mode: int = SLECG_START_MODE_NORMAL, flags: int = 0) -> bytes:
    return build_frame(SLECG_TYPE_START_ACQ, bytes([mode & 0xFF, flags & 0xFF]))


def build_stop_acq() -> bytes:
    return build_frame(SLECG_TYPE_STOP_ACQ, b"\x00")


def build_req_status() -> bytes:
    return build_frame(SLECG_TYPE_REQ_STATUS)
