"""Transport abstraction."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import Enum
from typing import Callable


class TransportMode(Enum):
    SERIAL = "serial"
    BLE = "ble"


@dataclass(frozen=True)
class DeviceInfo:
    id: str
    label: str


class Transport(ABC):
    @abstractmethod
    def list_devices(self) -> list[DeviceInfo]:
        ...

    @abstractmethod
    def open(self, device_id: str) -> None:
        ...

    @abstractmethod
    def close(self) -> None:
        ...

    @abstractmethod
    def is_open(self) -> bool:
        ...

    @abstractmethod
    def write(self, data: bytes) -> None:
        ...

    @abstractmethod
    def set_data_callback(self, callback: Callable[[bytes], None] | None) -> None:
        ...
