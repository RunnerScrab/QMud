#!/usr/bin/env python3
"""!
@file png_to_icns.py
@brief Wraps a square PNG image into a minimal macOS ICNS file.
"""

from pathlib import Path
import struct
import sys


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ICNS_TYPES_BY_SIZE = {
    16: b"icp4",
    32: b"icp5",
    64: b"icp6",
    128: b"ic07",
    256: b"ic08",
    512: b"ic09",
    1024: b"ic10",
}


def png_dimensions(data: bytes) -> tuple[int, int]:
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("input is not a PNG file")
    if data[12:16] != b"IHDR":
        raise ValueError("PNG IHDR chunk is missing")
    return struct.unpack(">II", data[16:24])


def write_icns(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    width, height = png_dimensions(data)
    if width != height:
        raise ValueError(f"PNG must be square, got {width}x{height}")
    icon_type = ICNS_TYPES_BY_SIZE.get(width)
    if icon_type is None:
        supported = ", ".join(str(size) for size in sorted(ICNS_TYPES_BY_SIZE))
        raise ValueError(f"unsupported PNG size {width}x{height}; supported sizes: {supported}")

    chunk_length = 8 + len(data)
    total_length = 8 + chunk_length
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(
        b"icns"
        + struct.pack(">I", total_length)
        + icon_type
        + struct.pack(">I", chunk_length)
        + data
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: png_to_icns.py SOURCE.png DESTINATION.icns", file=sys.stderr)
        return 2

    try:
        write_icns(Path(sys.argv[1]), Path(sys.argv[2]))
    except OSError as error:
        print(f"png_to_icns.py: {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"png_to_icns.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
