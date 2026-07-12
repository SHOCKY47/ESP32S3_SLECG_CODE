"""Frame envelope build and stream parser — aligned with slecg_proto_frame.c."""

from __future__ import annotations

from dataclasses import dataclass

from .constants import (
    SLECG_FOOT_H,
    SLECG_FOOT_L,
    SLECG_FRAME_HEADER_SIZE,
    SLECG_FRAME_MAX_PAYLOAD,
    SLECG_FRAME_OVERHEAD,
    SLECG_SYNC_H,
    SLECG_SYNC_L,
)


@dataclass(frozen=True)
class ParsedFrame:
    type: int
    payload: bytes


def build_frame(frame_type: int, payload: bytes = b"") -> bytes:
    if len(payload) > SLECG_FRAME_MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)}")
    length = len(payload)
    frame = bytearray(SLECG_FRAME_OVERHEAD + length)
    frame[0] = SLECG_SYNC_H
    frame[1] = SLECG_SYNC_L
    frame[2] = frame_type & 0xFF
    frame[3] = length & 0xFF
    frame[4] = (length >> 8) & 0xFF
    if length:
        frame[5 : 5 + length] = payload
    foot = 5 + length
    frame[foot] = SLECG_FOOT_H
    frame[foot + 1] = SLECG_FOOT_L
    return bytes(frame)


class FrameParser:
    """Incremental byte-stream frame parser."""

    def __init__(self, max_cache: int = 8192) -> None:
        # 默认 8KB：覆盖突发二进制流；过小会导致半包被截断、长期无法同步
        self._cache = bytearray()
        self._max_cache = max(max_cache, SLECG_FRAME_MAX_PAYLOAD + SLECG_FRAME_OVERHEAD)

    def reset(self) -> None:
        self._cache.clear()

    @property
    def cache_size(self) -> int:
        return len(self._cache)

    def feed(self, data: bytes) -> list[ParsedFrame]:
        if not data:
            return []
        self._cache.extend(data)
        if len(self._cache) > self._max_cache:
            self._cache = self._cache[-self._max_cache :]

        frames: list[ParsedFrame] = []
        while True:
            before = len(self._cache)
            parsed = self._try_parse_one()
            if parsed is not None:
                frames.append(parsed)
            elif len(self._cache) == before:
                break
        return frames

    def _try_parse_one(self) -> ParsedFrame | None:
        cache = self._cache
        length = len(cache)

        sync_idx = self._find_sync()
        if sync_idx < 0:
            if length > 1:
                self._cache = bytearray([cache[-1]])
            elif length == 1:
                self._cache = bytearray()
            return None

        if sync_idx > 0:
            del cache[:sync_idx]
            length = len(cache)

        if length < 2 or cache[0] != SLECG_SYNC_H or cache[1] != SLECG_SYNC_L:
            return None

        if length < SLECG_FRAME_HEADER_SIZE:
            return None

        payload_len = cache[3] | (cache[4] << 8)
        if payload_len > SLECG_FRAME_MAX_PAYLOAD:
            del cache[0]
            return None

        total = payload_len + SLECG_FRAME_OVERHEAD
        if length < total:
            return None

        foot = SLECG_FRAME_HEADER_SIZE + payload_len
        if cache[foot] != SLECG_FOOT_H or cache[foot + 1] != SLECG_FOOT_L:
            del cache[0]
            return None

        frame_type = cache[2]
        payload = bytes(cache[5:foot])
        del cache[:total]
        return ParsedFrame(type=frame_type, payload=payload)

    def _find_sync(self) -> int:
        cache = self._cache
        for i in range(len(cache) - 1):
            if cache[i] == SLECG_SYNC_H and cache[i + 1] == SLECG_SYNC_L:
                return i
        return -1
