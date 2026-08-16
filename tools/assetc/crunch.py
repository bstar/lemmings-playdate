"""Decoder for the backwards LZ container used by DOS Lemmings DAT files."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator


class FormatError(ValueError):
    pass


def be16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "big")


@dataclass(frozen=True)
class Section:
    offset: int
    compressed_size: int
    decompressed_size: int
    bit_count: int
    checksum: int
    payload: bytes


class ReverseBits:
    def __init__(self, data: bytes, bit_count: int):
        # Zero is valid and means that the final stored byte contributes no
        # bits; decoding starts at the preceding byte.
        if not data or not 0 <= bit_count <= 8:
            raise FormatError("invalid reverse bitstream")
        self.data = data
        self.byte = len(data) - 1
        self.bit = 0
        self.first_bits = bit_count

    def read(self, count: int) -> int:
        value = 0
        for _ in range(count):
            if self.byte < 0:
                raise FormatError("compressed bitstream ended early")
            limit = self.first_bits if self.byte == len(self.data) - 1 else 8
            if self.bit >= limit:
                self.byte -= 1
                self.bit = 0
                if self.byte < 0:
                    raise FormatError("compressed bitstream ended early")
            value = (value << 1) | ((self.data[self.byte] >> self.bit) & 1)
            self.bit += 1
        return value


def decompress(payload: bytes, output_size: int, bit_count: int) -> bytes:
    bits = ReverseBits(payload, bit_count)
    reverse = bytearray()

    def literals(count: int) -> None:
        for _ in range(count):
            reverse.append(bits.read(8))

    def previous(count: int, width: int) -> None:
        distance = bits.read(width) + 1
        if distance > len(reverse):
            raise FormatError("invalid back-reference")
        for _ in range(count):
            reverse.append(reverse[-distance])

    while len(reverse) < output_size:
        if bits.read(1) == 0:
            if bits.read(1) == 0:
                literals(bits.read(3) + 1)
            else:
                previous(2, 8)
        else:
            mode = bits.read(2)
            if mode == 0:
                previous(3, 9)
            elif mode == 1:
                previous(4, 10)
            elif mode == 2:
                previous(bits.read(8) + 1, 12)
            else:
                literals(bits.read(8) + 9)
        if len(reverse) > output_size:
            raise FormatError("section expanded beyond declared size")
    reverse.reverse()
    return bytes(reverse)


def sections(data: bytes) -> Iterator[Section]:
    offset = 0
    while offset < len(data):
        if len(data) - offset < 10:
            raise FormatError(f"trailing DAT bytes at {offset}")
        bit_count = data[offset]
        checksum = data[offset + 1]
        decompressed_size = be16(data, offset + 4)
        compressed_size = be16(data, offset + 8)
        if compressed_size < 10 or offset + compressed_size > len(data):
            raise FormatError(f"invalid section size at {offset}")
        compressed = data[offset + 10 : offset + compressed_size]
        actual_checksum = 0
        for value in compressed:
            actual_checksum ^= value
        if actual_checksum != checksum:
            raise FormatError(f"checksum mismatch at {offset}")
        payload = decompress(compressed, decompressed_size, bit_count)
        yield Section(
            offset, compressed_size, decompressed_size, bit_count, checksum, payload
        )
        offset += compressed_size
