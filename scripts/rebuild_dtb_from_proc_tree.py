#!/usr/bin/env python3
"""Build a flattened device tree from a captured Linux /proc/device-tree tree."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 0x1
FDT_END_NODE = 0x2
FDT_PROP = 0x3
FDT_END = 0x9
FDT_HEADER_SIZE = 40
FDT_VERSION = 17
FDT_LAST_COMP_VERSION = 16


def be32(value: int) -> bytes:
    return struct.pack(">I", value)


def pad4(data: bytes) -> bytes:
    return data + (b"\x00" * ((-len(data)) % 4))


class StringTable:
    def __init__(self) -> None:
        self._offsets: dict[str, int] = {}
        self._data = bytearray()

    def add(self, value: str) -> int:
        if value not in self._offsets:
            self._offsets[value] = len(self._data)
            self._data.extend(value.encode("utf-8") + b"\x00")
        return self._offsets[value]

    def to_bytes(self) -> bytes:
        return bytes(self._data)


def entries(path: Path) -> tuple[list[Path], list[Path]]:
    directories: list[Path] = []
    properties: list[Path] = []

    for entry in path.iterdir():
        if entry.is_symlink():
            raise ValueError(f"Symbolic links are not valid in a captured device tree: {entry}")
        if entry.is_dir():
            directories.append(entry)
        elif entry.is_file():
            properties.append(entry)
        else:
            raise ValueError(f"Unsupported device-tree entry: {entry}")

    return sorted(directories, key=lambda item: item.name), sorted(properties, key=lambda item: item.name)


def serialize_node(path: Path, strings: StringTable, is_root: bool = False) -> tuple[bytes, int, int]:
    node_name = b"" if is_root else path.name.encode("utf-8")
    structure = bytearray(be32(FDT_BEGIN_NODE))
    structure.extend(pad4(node_name + b"\x00"))
    node_count = 1
    property_count = 0

    directories, properties = entries(path)
    for property_path in properties:
        value = property_path.read_bytes()
        structure.extend(be32(FDT_PROP))
        structure.extend(be32(len(value)))
        structure.extend(be32(strings.add(property_path.name)))
        structure.extend(pad4(value))
        property_count += 1

    for directory_path in directories:
        child, child_nodes, child_properties = serialize_node(directory_path, strings)
        structure.extend(child)
        node_count += child_nodes
        property_count += child_properties

    structure.extend(be32(FDT_END_NODE))
    return bytes(structure), node_count, property_count


def build_dtb(source: Path) -> tuple[bytes, int, int]:
    strings = StringTable()
    structure, node_count, property_count = serialize_node(source, strings, is_root=True)
    structure += be32(FDT_END)
    reserve_map = b"\x00" * 16
    strings_data = strings.to_bytes()
    off_mem_rsvmap = FDT_HEADER_SIZE
    off_dt_struct = off_mem_rsvmap + len(reserve_map)
    off_dt_strings = off_dt_struct + len(structure)
    total_size = off_dt_strings + len(strings_data)
    header = struct.pack(
        ">10I",
        FDT_MAGIC,
        total_size,
        off_dt_struct,
        off_dt_strings,
        off_mem_rsvmap,
        FDT_VERSION,
        FDT_LAST_COMP_VERSION,
        0,
        len(strings_data),
        len(structure),
    )
    return header + reserve_map + structure + strings_data, node_count, property_count


def verify_header(blob: bytes) -> None:
    if len(blob) < FDT_HEADER_SIZE:
        raise ValueError("Generated DTB is shorter than the FDT header")
    magic, total_size, off_struct, off_strings, off_reserve, version, last_compat, _, size_strings, size_struct = struct.unpack(
        ">10I", blob[:FDT_HEADER_SIZE]
    )
    if magic != FDT_MAGIC:
        raise ValueError("Generated DTB has an invalid magic")
    if total_size != len(blob):
        raise ValueError("Generated DTB total size does not match its header")
    if version < FDT_LAST_COMP_VERSION or last_compat > FDT_VERSION:
        raise ValueError("Generated DTB has an invalid version range")
    if off_reserve != FDT_HEADER_SIZE or off_struct < off_reserve + 16:
        raise ValueError("Generated DTB has invalid reserve-map offsets")
    if off_strings != off_struct + size_struct or off_strings + size_strings != total_size:
        raise ValueError("Generated DTB has invalid structure/string offsets")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Captured /proc/device-tree directory")
    parser.add_argument("--output", required=True, type=Path, help="Output DTB path outside the repository")
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise SystemExit(f"Source is not a directory: {source}")
    if output.is_relative_to(source):
        raise SystemExit("Output must not be placed inside the source device-tree directory")

    blob, node_count, property_count = build_dtb(source)
    verify_header(blob)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)
    digest = hashlib.sha256(blob).hexdigest()
    print(f"Wrote {output}")
    print(f"Nodes: {node_count}; properties: {property_count}; bytes: {len(blob)}")
    print(f"SHA256: {digest}")


if __name__ == "__main__":
    main()
