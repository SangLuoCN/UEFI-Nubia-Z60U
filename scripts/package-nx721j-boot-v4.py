#!/usr/bin/env python3
"""按 NX721J 实机 boot v4 头部封装临时 UEFI 镜像。"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import struct
from pathlib import Path


PAGE_SIZE = 4096
BOOT_HEADER_SIZE_V4 = 1584
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FD = ROOT / "Build/nx721jPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd"
DEFAULT_SHIM = ROOT / "BootShim/BootShim.bin"
DEFAULT_OUTPUT = ROOT / "Mu-nx721j-v4-stock.img"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def page_count(size: int) -> int:
    return (size + PAGE_SIZE - 1) // PAGE_SIZE


def read_stock_header(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < BOOT_HEADER_SIZE_V4:
        raise ValueError(f"boot 头部不足 {BOOT_HEADER_SIZE_V4} 字节: {path}")
    if data[:8] != b"ANDROID!":
        raise ValueError("实机 boot 头部不是 ANDROID! 格式")

    header_version = struct.unpack_from("<I", data, 40)[0]
    header_size = struct.unpack_from("<I", data, 20)[0]
    if header_version != 4 or header_size != BOOT_HEADER_SIZE_V4:
        raise ValueError(
            f"只接受 Android Boot v4/1584 头部，实际为 v{header_version}/{header_size}"
        )

    return {
        "kernel_size": struct.unpack_from("<I", data, 8)[0],
        "ramdisk_size": struct.unpack_from("<I", data, 12)[0],
        "os_version": struct.unpack_from("<I", data, 16)[0],
        "header_size": header_size,
        "reserved": struct.unpack_from("<4I", data, 24),
        "header_version": header_version,
        "cmdline": data[44 : 44 + BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE],
        "signature_size": struct.unpack_from("<I", data, 1580)[0],
    }


def build_header(kernel_size: int, stock: dict) -> bytes:
    header = bytearray(BOOT_HEADER_SIZE_V4)
    header[:8] = b"ANDROID!"
    struct.pack_into(
        "<4I",
        header,
        8,
        kernel_size,
        stock["ramdisk_size"],
        stock["os_version"],
        stock["header_size"],
    )
    struct.pack_into("<4I", header, 24, *stock["reserved"])
    struct.pack_into("<I", header, 40, stock["header_version"])
    header[44 : 44 + BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE] = stock["cmdline"]
    struct.pack_into("<I", header, 1580, stock["signature_size"])
    return bytes(header)


def make_image(kernel: bytes, stock: dict) -> bytes:
    header = build_header(len(kernel), stock)
    image = bytearray(header)
    image.extend(b"\0" * (PAGE_SIZE - len(header)))
    image.extend(kernel)
    image.extend(b"\0" * (page_count(len(kernel)) * PAGE_SIZE - len(kernel)))
    image.extend(b"\0" * (page_count(stock["ramdisk_size"]) * PAGE_SIZE))
    image.extend(b"\0" * (page_count(stock["signature_size"]) * PAGE_SIZE))
    return bytes(image)


def verify(image: bytes, payload: bytes, expected_combined: bytes, stock: dict) -> None:
    if image[:8] != b"ANDROID!":
        raise ValueError("生成镜像的 magic 错误")
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    ramdisk_size = struct.unpack_from("<I", image, 12)[0]
    signature_size = struct.unpack_from("<I", image, 1580)[0]
    if kernel_size != len(payload):
        raise ValueError("kernel_size 与 gzip 载荷长度不一致")
    if ramdisk_size != stock["ramdisk_size"] or signature_size != stock["signature_size"]:
        raise ValueError("镜像的 ramdisk/signature 字段没有保持实机值")
    actual = image[PAGE_SIZE : PAGE_SIZE + kernel_size]
    if actual != payload:
        raise ValueError("镜像回读的 gzip 载荷不一致")
    if gzip.decompress(actual) != expected_combined:
        raise ValueError("gzip 解压后的 BootShim+FD 不一致")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock-header", type=Path, required=True, help="实机 boot_a 首页备份")
    parser.add_argument("--fd", type=Path, default=DEFAULT_FD, help="SILICIUM_UEFI.fd")
    parser.add_argument("--bootshim", type=Path, default=DEFAULT_SHIM, help="BootShim.bin")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="输出 boot 镜像")
    args = parser.parse_args()

    fd = args.fd.read_bytes()
    shim = args.bootshim.read_bytes()
    if len(fd) != 0x300000:
        raise SystemExit(f"FD 大小错误: 0x{len(fd):x}，NX721J 当前配置要求 0x300000")
    if len(shim) < 64 or shim[56:60] != b"ARM\x64":
        raise SystemExit("BootShim.bin 没有有效的 ARM64 Android kernel header")
    shim_base, shim_size = struct.unpack_from("<QQ", shim, 8)
    if shim_size != len(shim) + len(fd):
        raise SystemExit(
            f"BootShim header 的 image_size=0x{shim_size:x}，实际 BootShim+FD=0x{len(shim) + len(fd):x}"
        )

    stock = read_stock_header(args.stock_header)
    combined = shim + fd
    payload = gzip.compress(combined, mtime=0)
    image = make_image(payload, stock)
    verify(image, payload, combined, stock)
    args.output.write_bytes(image)

    report = {
        "说明": "NX721J 按实机 boot v4 头部封装的临时 UEFI 镜像",
        "镜像大小": len(image),
        "镜像SHA256": sha256(image),
        "gzip载荷大小": len(payload),
        "gzip载荷SHA256": sha256(payload),
        "BootShim大小": len(shim),
        "UEFIFD大小": len(fd),
        "UEFIFD基址": f"0x{shim_base:x}",
        "实机头部": {
            "kernel_size": stock["kernel_size"],
            "ramdisk_size": stock["ramdisk_size"],
            "os_version": f"0x{stock['os_version']:08x}",
            "header_size": stock["header_size"],
            "header_version": stock["header_version"],
            "signature_size": stock["signature_size"],
        },
        "DTB来源": "设备现有 vendor_boot_a，由 ABL 提供和修正；本镜像不拼接 DTB",
        "硬件刷写": False,
    }
    report_path = args.output.with_suffix(args.output.suffix + ".json")
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
