#!/usr/bin/env python3
"""Attach an official UniXNU FAT payload to the known-good NX721J boot v4 image.

The NX721J stock boot image uses header v4, carries the device tree after the
gzip kernel field, and the known-good v21 image already proves the
BootShim + UEFI FD + DTB path. This script keeps that kernel field
byte-for-byte identical and replaces only the dummy boot ramdisk with the
supplied UniXNU payload. The v21 signature placeholder is preserved.

The result is for ``fastboot boot`` testing only. It never modifies a device
partition and it does not create or change a userdata image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


PAGE_SIZE = 4096
V4_HEADER_SIZE = 1584
V4_HEADER_VERSION_OFFSET = 40
V4_HEADER_SIZE_OFFSET = 20
V4_RAMDISK_SIZE_OFFSET = 12
V4_SIGNATURE_SIZE_OFFSET = 1580


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pages(size: int) -> int:
    return (size + PAGE_SIZE - 1) // PAGE_SIZE


def padded(data: bytes) -> bytes:
    return data + b"\0" * (pages(len(data)) * PAGE_SIZE - len(data))


def parse_image(image: bytes) -> dict:
    if len(image) < PAGE_SIZE or image[:8] != b"ANDROID!":
        raise ValueError("base image is not an Android boot image")

    kernel_size, ramdisk_size, os_version, header_size = struct.unpack_from(
        "<4I", image, 8
    )
    header_version = struct.unpack_from("<I", image, V4_HEADER_VERSION_OFFSET)[0]
    signature_size = struct.unpack_from("<I", image, V4_SIGNATURE_SIZE_OFFSET)[0]
    if header_version != 4 or header_size != V4_HEADER_SIZE:
        raise ValueError(
            f"expected Android boot v4/{V4_HEADER_SIZE}, "
            f"got v{header_version}/{header_size}"
        )

    kernel_start = PAGE_SIZE
    kernel_end = kernel_start + kernel_size
    ramdisk_start = PAGE_SIZE * (1 + pages(kernel_size))
    ramdisk_end = ramdisk_start + ramdisk_size
    signature_start = ramdisk_start + pages(ramdisk_size) * PAGE_SIZE
    signature_end = signature_start + pages(signature_size) * PAGE_SIZE
    if kernel_end > len(image) or signature_end > len(image):
        raise ValueError("base image ends before its declared kernel, ramdisk or signature")

    return {
        "kernel_size": kernel_size,
        "ramdisk_size": ramdisk_size,
        "os_version": os_version,
        "header_size": header_size,
        "header_version": header_version,
        "signature_size": signature_size,
        "kernel": image[kernel_start:kernel_end],
        "old_ramdisk": image[ramdisk_start:ramdisk_end],
        "old_signature": image[signature_start:signature_end],
    }


def verify_kernel(kernel: bytes, bootshim: bytes, fd: bytes, dtb: bytes) -> dict:
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    combined = decoder.decompress(kernel)
    if not decoder.eof:
        raise ValueError("v21 kernel field does not contain a complete gzip stream")
    if combined != bootshim + fd:
        raise ValueError("v21 gzip payload is not BootShim + the supplied UEFI FD")
    if decoder.unused_data != dtb:
        raise ValueError("v21 appended DTB differs from the supplied NX721J DTB")
    if dtb[:4] != b"\xd0\x0d\xfe\xed":
        raise ValueError("supplied DTB is not an FDT")
    return {
        "bootshim_bytes": len(bootshim),
        "uefi_fd_bytes": len(fd),
        "dtb_bytes": len(dtb),
        "bootshim_sha256": sha256(bootshim),
        "uefi_fd_sha256": sha256(fd),
        "dtb_sha256": sha256(dtb),
        "gzip_kernel_sha256": sha256(kernel),
    }


def build(base: Path, payload: Path, bootshim: Path, fd: Path, dtb: Path, output: Path) -> dict:
    base_data = base.read_bytes()
    payload_data = payload.read_bytes()
    bootshim_data = bootshim.read_bytes()
    fd_data = fd.read_bytes()
    dtb_data = dtb.read_bytes()

    if not payload_data:
        raise ValueError("payload is empty")
    parsed = parse_image(base_data)
    kernel_report = verify_kernel(parsed["kernel"], bootshim_data, fd_data, dtb_data)

    header = bytearray(base_data[:V4_HEADER_SIZE])
    struct.pack_into("<I", header, V4_RAMDISK_SIZE_OFFSET, len(payload_data))
    image = bytes(header)
    image += b"\0" * (PAGE_SIZE - len(header))
    image += padded(parsed["kernel"])
    image += padded(payload_data)
    image += b"\0" * (pages(parsed["signature_size"]) * PAGE_SIZE)

    rebuilt = parse_image(image)
    if rebuilt["kernel"] != parsed["kernel"]:
        raise ValueError("kernel field changed while attaching the payload")
    if rebuilt["old_ramdisk"] != payload_data:
        raise ValueError("payload ramdisk does not round-trip")
    if rebuilt["old_signature"] != parsed["old_signature"]:
        raise ValueError("v21 signature placeholder changed")

    report = {
        "base_image": str(base),
        "base_image_sha256": sha256(base_data),
        "output_image": str(output),
        "image_bytes": len(image),
        "image_sha256": sha256(image),
        "header_version": rebuilt["header_version"],
        "header_size": rebuilt["header_size"],
        "kernel_bytes": rebuilt["kernel_size"],
        "kernel_sha256": sha256(rebuilt["kernel"]),
        "ramdisk_bytes": rebuilt["ramdisk_size"],
        "ramdisk_sha256": sha256(payload_data),
        "signature_bytes": rebuilt["signature_size"],
        "signature_sha256": sha256(rebuilt["old_signature"]),
        "hardware_tested": False,
        **kernel_report,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    output.with_suffix(output.suffix + ".json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-image", required=True, type=Path)
    parser.add_argument("--payload", required=True, type=Path)
    parser.add_argument("--bootshim", required=True, type=Path)
    parser.add_argument("--fd", required=True, type=Path)
    parser.add_argument("--dtb", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = build(
        args.base_image,
        args.payload,
        args.bootshim,
        args.fd,
        args.dtb,
        args.output,
    )
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()