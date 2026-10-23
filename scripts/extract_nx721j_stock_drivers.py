#!/usr/bin/env python3
"""Extract selected NX721J DXE FFS files from the stock Z60U UEFI FV."""

from __future__ import annotations

import argparse
import gzip
import uuid
from dataclasses import dataclass
from pathlib import Path


SECTION_PE32 = 0x10
SECTION_DXE_DEPEX = 0x13
FFS_HEADER_SIZE = 24
SECTION_HEADER_SIZE = 4


@dataclass(frozen=True)
class Driver:
    name: str
    guid: str


DRIVERS = (
    Driver("IPCCDxe", "42031F68-1F0B-429B-93F1-0EB0CF89DDBF"),
    Driver("Smp2pDxe", "23F4E10E-3050-4073-B94C-B80D7EBC959E"),
    Driver("SSRDxe", "6B593A13-4E39-4CF5-B4E2-50B9540A324A"),
    Driver("GLinkDxe", "7942A7EB-B7D5-4E2E-B908-831E4DE55B58"),
    Driver("PmicGlinkDxe", "6D4A9CF7-CC0C-42B0-AA89-F6A6A8D99273"),
    Driver("VariableDxe", "2B0ECDCE-01AE-446E-9FFD-C4217DE0340F"),
    Driver("ResetRuntimeDxe", "3AE17DB7-3CC5-4B89-9304-9549211057EF"),
)


def align(value: int, boundary: int) -> int:
    return (value + boundary - 1) & ~(boundary - 1)


def find_ffs(data: bytes, guid: str) -> int:
    needle = uuid.UUID(guid).bytes_le
    cursor = 0
    while True:
        offset = data.find(needle, cursor)
        if offset < 0:
            raise ValueError(f"FFS {guid} not found")
        if offset + FFS_HEADER_SIZE <= len(data):
            size = int.from_bytes(data[offset + 20 : offset + 23], "little")
            file_type = data[offset + 18]
            state = data[offset + 23]
            if 24 <= size <= len(data) - offset and file_type in range(1, 8) and state != 0:
                return offset
        cursor = offset + 1


def parse_sections(data: bytes, ffs_offset: int) -> tuple[int, dict[int, bytes]]:
    ffs_size = int.from_bytes(data[ffs_offset + 20 : ffs_offset + 23], "little")
    ffs_end = ffs_offset + ffs_size
    cursor = ffs_offset + FFS_HEADER_SIZE
    sections: dict[int, bytes] = {}
    while cursor + SECTION_HEADER_SIZE <= ffs_end:
        section_size = int.from_bytes(data[cursor : cursor + 3], "little")
        section_type = data[cursor + 3]
        if section_size < SECTION_HEADER_SIZE or cursor + section_size > ffs_end:
            raise ValueError(
                f"invalid section at 0x{cursor:X}: size=0x{section_size:X}, "
                f"ffs_end=0x{ffs_end:X}"
            )
        sections.setdefault(section_type, data[cursor + SECTION_HEADER_SIZE : cursor + section_size])
        cursor = align(cursor + section_size, 4)
    return ffs_size, sections


def extract_compressed_volume(data: bytes, ffs_offset: int) -> bytes:
    """Extract the gzip-guided volume image used by the stock UEFI container."""
    ffs_size = int.from_bytes(data[ffs_offset + 20 : ffs_offset + 23], "little")
    section_offset = ffs_offset + FFS_HEADER_SIZE
    section_size = int.from_bytes(data[section_offset : section_offset + 3], "little")
    section_type = data[section_offset + 3]
    if section_type != 0x02:
        raise ValueError(f"expected GUID-defined section, got 0x{section_type:02X}")
    if ffs_offset + ffs_size > len(data) or section_offset + section_size > len(data):
        raise ValueError("compressed volume FFS exceeds input")
    compressed = data[section_offset + 24 : section_offset + section_size]
    return gzip.decompress(compressed)


def write_inf(output_dir: Path, driver: Driver) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    text = f"""[Defines]
  INF_VERSION                    = 0x0001001B
  BASE_NAME                      = {driver.name}
  FILE_GUID                      = {driver.guid}
  MODULE_TYPE                    = DXE_DRIVER
  VERSION_STRING                 = 1.0
  ENTRY_POINT                    = EfiEntry

[Binaries.AARCH64]
  DXE_DEPEX|{driver.name}.depex|*
  PE32|{driver.name}.efi|*

[Depex]
  TRUE
"""
    (output_dir / f"{driver.name}.inf").write_text(text, encoding="ascii", newline="\n")


def extract_driver(data: bytes, output_root: Path, driver: Driver) -> dict[str, int]:
    ffs_offset = find_ffs(data, driver.guid)
    ffs_size, sections = parse_sections(data, ffs_offset)
    if SECTION_PE32 not in sections:
        raise ValueError(f"{driver.name}: PE32 section not found")
    if SECTION_DXE_DEPEX not in sections:
        raise ValueError(f"{driver.name}: DXE_DEPEX section not found")

    output_dir = output_root / driver.name
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / f"{driver.name}.efi").write_bytes(sections[SECTION_PE32])
    (output_dir / f"{driver.name}.depex").write_bytes(sections[SECTION_DXE_DEPEX])
    write_inf(output_dir, driver)
    return {
        "ffs_offset": ffs_offset,
        "ffs_size": ffs_size,
        "pe32_size": len(sections[SECTION_PE32]),
        "depex_size": len(sections[SECTION_DXE_DEPEX]),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="stock uefi-full.img")
    parser.add_argument("--output", required=True, type=Path, help="Binaries/nx721j/QcomPkg/Drivers")
    parser.add_argument(
        "--compressed-volume-ffs-offset",
        type=lambda value: int(value, 0),
        help="offset of the gzip-guided Volume Image FFS inside uefi-full.img",
    )
    parser.add_argument(
        "--drivers",
        help="comma-separated driver names; default is all drivers",
    )
    args = parser.parse_args()

    data = args.input.read_bytes()
    if args.compressed_volume_ffs_offset is not None:
        data = extract_compressed_volume(data, args.compressed_volume_ffs_offset)
        print(f"decompressed volume: 0x{len(data):X} bytes")
    selected_names = set(args.drivers.split(",")) if args.drivers else {driver.name for driver in DRIVERS}
    selected = tuple(driver for driver in DRIVERS if driver.name in selected_names)
    unknown = selected_names - {driver.name for driver in DRIVERS}
    if unknown:
        raise ValueError(f"unknown driver names: {', '.join(sorted(unknown))}")
    for driver in selected:
        result = extract_driver(data, args.output, driver)
        print(
            f"{driver.name}: FFS=0x{result['ffs_offset']:X}/0x{result['ffs_size']:X} "
            f"PE32=0x{result['pe32_size']:X} DEPEX=0x{result['depex_size']:X}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
