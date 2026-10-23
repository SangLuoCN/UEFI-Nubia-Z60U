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
    directory: str | None = None


DRIVERS = (
    Driver("UsbInitDxe", "0A134F0E-075E-40B3-9C63-3B3906804663"),
    Driver("UsbMsdDxe", "5AF77F10-90DF-4E7E-8325-A17EC09D5443"),
    Driver("PmicDxe", "5776232E-082D-4B75-9A0E-FE1D13F7A5D9"),
    # The output root is Binaries/nx721j/QcomPkg/Drivers.  These UEFI-core
    # drivers intentionally live in the sibling MdeModulePkg hierarchy.
    Driver(
        "DiskIoDxe",
        "6B38F7B4-AD98-40E9-9093-ACA2B5A253C4",
        "../../MdeModulePkg/Universal/Disk/DiskIoDxe",
    ),
    Driver(
        "PartitionDxe",
        "1FA1F39E-FEFF-4AAE-BD7B-38A070A3B609",
        "../../MdeModulePkg/Universal/Disk/PartitionDxe",
    ),
    Driver("TsensDxe", "F09AD92E-485C-9CDB-3E05-3385C38F94CC"),
    Driver("PwrUtilsDxe", "0401B830-BFF9-44A2-A5F6-95734A33C017"),
    Driver("QcomChargerDxeLA", "4BCE7F36-428E-4393-99E3-7E0844404DBA"),
    Driver("UsbPwrCtrlDxe", "11FAED4C-B21F-4D88-8E48-C4C28A1E50DF"),
    Driver("UsbConfigDxe", "CD823A4D-7DEC-4531-AE5D-4134FA4127B8"),
    Driver("UsbDeviceDxe", "3299A266-15F0-4346-8318-716336736D3E"),
    Driver("UsbfnDwc3Dxe", "94F8A6A7-DC34-4101-88C1-99179CCEAE83"),
    Driver("I2CDxe", "04DE8591-D2B3-4077-BBBE-B12070094EB6"),
    Driver("ButtonsDxe", "5BD181DB-0487-4F1A-AE73-820E165611B3"),
    Driver("ChargerExDxe", "2E3042F3-2BB9-463F-8866-56D75B7C4AB0"),
    Driver("IPCCDxe", "42031F68-1F0B-429B-93F1-0EB0CF89DDBF"),
    Driver("Smp2pDxe", "23F4E10E-3050-4073-B94C-B80D7EBC959E"),
    Driver("SSRDxe", "6B593A13-4E39-4CF5-B4E2-50B9540A324A"),
    Driver("GLinkDxe", "7942A7EB-B7D5-4E2E-B908-831E4DE55B58"),
    Driver("PmicGlinkDxe", "6D4A9CF7-CC0C-42B0-AA89-F6A6A8D99273"),
    Driver(
        "ShmBridgeDxeLA",
        "41EBB6A6-2C19-4C02-841D-848CE004A282",
        "ShmBridgeDxe",
    ),
    Driver("ScmDxeCompat", "4B4973EE-401B-4F36-A6A9-533DFCCDFC33", "TzDxe"),
    Driver("TzDxeLA", "CF6DFC66-14AB-4E13-84DB-9C02912D1404", "TzDxe"),
    Driver("VariableDxe", "2B0ECDCE-01AE-446E-9FFD-C4217DE0340F"),
    Driver("ResetRuntimeDxe", "3AE17DB7-3CC5-4B89-9304-9549211057EF"),
    Driver("VendorLogfsDxe", "74BFFA36-92DC-4D44-BE1C-B31E37DD7733"),
    Driver("CfgUtilDxe", "9399AEF4-FC3C-4C60-D213-9EB988D12F28"),
    Driver("NubiaBoardDxe", "11CC956F-48B8-0F8C-78FC-218ED0821F3A"),
    Driver("RscDxe", "EA888DF7-D431-44AF-844C-7B29390F2758"),
)

DEFAULT_DRIVER_NAMES = {
    "IPCCDxe",
    "Smp2pDxe",
    "SSRDxe",
    "GLinkDxe",
    "PmicGlinkDxe",
    "VariableDxe",
    "ResetRuntimeDxe",
}


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


def write_inf(output_dir: Path, driver: Driver, has_depex: bool) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    depex_binary = (
        f"  DXE_DEPEX|{driver.name}.depex|*\n" if has_depex else ""
    )
    text = f"""[Defines]
  INF_VERSION                    = 0x0001001B
  BASE_NAME                      = {driver.name}
  FILE_GUID                      = {driver.guid}
  MODULE_TYPE                    = DXE_DRIVER
  VERSION_STRING                 = 1.0
  ENTRY_POINT                    = EfiEntry

[Binaries.AARCH64]
{depex_binary}  PE32|{driver.name}.efi|*

[Depex]
  TRUE
"""
    (output_dir / f"{driver.name}.inf").write_text(text, encoding="ascii", newline="\n")


def extract_driver(data: bytes, output_root: Path, driver: Driver) -> dict[str, int]:
    ffs_offset = find_ffs(data, driver.guid)
    ffs_size, sections = parse_sections(data, ffs_offset)
    if SECTION_PE32 not in sections:
        raise ValueError(f"{driver.name}: PE32 section not found")

    output_dir = output_root / (driver.directory or driver.name)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / f"{driver.name}.efi").write_bytes(sections[SECTION_PE32])
    if SECTION_DXE_DEPEX in sections:
        (output_dir / f"{driver.name}.depex").write_bytes(sections[SECTION_DXE_DEPEX])
    write_inf(output_dir, driver, SECTION_DXE_DEPEX in sections)
    return {
        "ffs_offset": ffs_offset,
        "ffs_size": ffs_size,
        "pe32_size": len(sections[SECTION_PE32]),
        "depex_size": len(sections.get(SECTION_DXE_DEPEX, b"")),
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
    selected_names = (
        set(args.drivers.split(",")) if args.drivers else DEFAULT_DRIVER_NAMES
    )
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
