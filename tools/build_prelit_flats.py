#!/usr/bin/env python3
"""Generate 16 ROM-resident depth-fog variants for every flat slot in a GBA Doom WAD.

Layout of the output binary:
    flat_slot 0: shade 0..15, 4096 bytes each
    flat_slot 1: shade 0..15, 4096 bytes each
    ...

The renderer uses the same even COLORMAP indices as the depth-fog code:
0, 2, 4, ... 30. Keeping a fixed 64 KiB stride per flat slot makes runtime
addressing cheap: base + (flat_index << 16) + (shade << 12).
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

FLAT_BYTES = 64 * 64
FOG_LEVELS = 16
FOG_COLORMAPS = tuple(range(0, 32, 2))


def read_wad(path: Path):
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"{path}: file is too small to be a WAD")
    ident, num_lumps, directory_offset = struct.unpack_from("<4sii", data, 0)
    if ident not in (b"IWAD", b"PWAD"):
        raise ValueError(f"{path}: unsupported WAD signature {ident!r}")
    if num_lumps < 1 or directory_offset < 12:
        raise ValueError(f"{path}: invalid WAD directory")

    lumps = []
    for index in range(num_lumps):
        entry = directory_offset + index * 16
        if entry + 16 > len(data):
            raise ValueError(f"{path}: truncated WAD directory")
        file_pos, size, raw_name = struct.unpack_from("<ii8s", data, entry)
        name = raw_name.rstrip(b"\0").decode("ascii", "replace").upper()
        if file_pos < 0 or size < 0 or file_pos + size > len(data):
            raise ValueError(f"{path}: invalid lump bounds for {name!r}")
        lumps.append((name, file_pos, size))
    return data, lumps


def last_lump(lumps, name: str) -> int:
    name = name.upper()
    for index in range(len(lumps) - 1, -1, -1):
        if lumps[index][0] == name:
            return index
    raise ValueError(f"required lump {name!r} was not found")


def generate(wad_path: Path, output_path: Path) -> tuple[int, int]:
    data, lumps = read_wad(wad_path)
    colormap_index = last_lump(lumps, "COLORMAP")
    _, cmap_pos, cmap_size = lumps[colormap_index]
    required_cmap_size = 32 * 256
    if cmap_size < required_cmap_size:
        raise ValueError(
            f"COLORMAP is {cmap_size} bytes; at least {required_cmap_size} are required"
        )
    colormaps = data[cmap_pos : cmap_pos + cmap_size]

    first = last_lump(lumps, "F_START") + 1
    last = last_lump(lumps, "F_END")
    if first >= last:
        raise ValueError("F_START/F_END contain no flat slots")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as out:
        for lump_index in range(first, last):
            name, pos, size = lumps[lump_index]
            if size == FLAT_BYTES:
                source = data[pos : pos + FLAT_BYTES]
                for map_index in FOG_COLORMAPS:
                    cmap = colormaps[map_index * 256 : (map_index + 1) * 256]
                    out.write(source.translate(cmap))
            elif size == 0:
                # Namespace markers are included in Doom's flat index range.
                # Reserve their fixed slot so the runtime index stays direct.
                out.write(bytes(FLAT_BYTES * FOG_LEVELS))
            else:
                raise ValueError(
                    f"flat-range lump {name!r} has {size} bytes; expected 0 or {FLAT_BYTES}"
                )

    slot_count = last - first
    expected = slot_count * FOG_LEVELS * FLAT_BYTES
    actual = output_path.stat().st_size
    if actual != expected:
        raise RuntimeError(f"generated {actual} bytes; expected {expected}")
    return slot_count, actual


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wad", type=Path, help="GBA-ready WAD used to generate source/iwad/*.c")
    parser.add_argument("output", type=Path, help="output prelit_flats.bin path")
    args = parser.parse_args()

    slots, size = generate(args.wad, args.output)
    print(
        f"Generated {slots} flat slots x {FOG_LEVELS} fog levels: "
        f"{size:,} bytes ({size / (1024 * 1024):.2f} MiB)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
