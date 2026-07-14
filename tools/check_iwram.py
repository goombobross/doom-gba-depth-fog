#!/usr/bin/env python3
"""Fail a GBA build before ROM creation if IWRAM or hot-path stack usage is unsafe."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass(frozen=True)
class Section:
    name: str
    sh_type: int
    addr: int
    offset: int
    size: int
    link: int
    entsize: int


def c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    return data[offset:end].decode("utf-8", errors="replace")


def parse_elf32_little(path: Path) -> Tuple[Dict[str, Section], Dict[str, Tuple[int, int]]]:
    blob = path.read_bytes()
    if len(blob) < 52 or blob[:4] != b"\x7fELF":
        raise ValueError(f"{path} is not an ELF file")
    if blob[4] != 1 or blob[5] != 1:
        raise ValueError("Only 32-bit little-endian ELF files are supported")

    # Elf32_Ehdr after e_ident.
    header = struct.unpack_from("<HHIIIIIHHHHHH", blob, 16)
    e_shoff = header[5]
    e_shentsize = header[10]
    e_shnum = header[11]
    e_shstrndx = header[12]

    if e_shentsize < 40 or e_shoff + e_shentsize * e_shnum > len(blob):
        raise ValueError("Invalid section-header table")

    raw_sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        raw_sections.append(struct.unpack_from("<IIIIIIIIII", blob, off))

    if e_shstrndx >= len(raw_sections):
        raise ValueError("Invalid section-name string table index")
    shstr = raw_sections[e_shstrndx]
    shstr_data = blob[shstr[4] : shstr[4] + shstr[5]]

    sections_by_index: List[Section] = []
    sections: Dict[str, Section] = {}
    for raw in raw_sections:
        name = c_string(shstr_data, raw[0])
        sec = Section(
            name=name,
            sh_type=raw[1],
            addr=raw[3],
            offset=raw[4],
            size=raw[5],
            link=raw[6],
            entsize=raw[9],
        )
        sections_by_index.append(sec)
        if name:
            sections[name] = sec

    symbols: Dict[str, Tuple[int, int]] = {}
    for sec in sections_by_index:
        # SHT_SYMTAB=2, SHT_DYNSYM=11
        if sec.sh_type not in (2, 11) or sec.entsize < 16 or sec.link >= len(sections_by_index):
            continue
        strtab = sections_by_index[sec.link]
        strdata = blob[strtab.offset : strtab.offset + strtab.size]
        count = sec.size // sec.entsize
        for i in range(count):
            off = sec.offset + i * sec.entsize
            if off + 16 > len(blob):
                break
            st_name, st_value, st_size, _st_info, _st_other, _st_shndx = struct.unpack_from(
                "<IIIBBH", blob, off
            )
            name = c_string(strdata, st_name)
            if name:
                # Prefer a real, non-zero definition if duplicate names exist.
                old = symbols.get(name)
                if old is None or (old[0] == 0 and st_value != 0):
                    symbols[name] = (st_value, st_size)

    return sections, symbols


def parse_stack_usage(paths: Iterable[Path]) -> List[Tuple[int, str, Path]]:
    frames: List[Tuple[int, str, Path]] = []
    for path in paths:
        try:
            lines = path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            parts = line.rsplit("\t", 2)
            if len(parts) < 2:
                continue
            try:
                size = int(parts[-2])
            except ValueError:
                continue
            lhs = parts[0]
            function = lhs.rsplit(":", 1)[-1]
            frames.append((size, function, path))
    frames.sort(reverse=True)
    return frames


def fmt_bytes(value: int) -> str:
    return f"{value:,} bytes"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--min-gap", type=int, default=2560,
                        help="minimum bytes between __data_end__ and __sp_usr")
    parser.add_argument("--max-frame", type=int, default=768,
                        help="maximum allowed single IWRAM function stack frame")
    parser.add_argument("--stack-dir", type=Path, default=None,
                        help="directory containing GCC .su stack-usage files")
    parser.add_argument("--reject-libgba-handles", action="store_true",
                        help="fail if libgba's 4 KiB handles table is still linked")
    args = parser.parse_args()

    try:
        sections, symbols = parse_elf32_little(args.elf)
    except (OSError, ValueError, struct.error) as exc:
        print(f"IWRAM CHECK ERROR: {exc}", file=sys.stderr)
        return 2

    required = ["__data_end__", "__sp_usr"]
    missing = [name for name in required if name not in symbols]
    if missing:
        print("IWRAM CHECK ERROR: missing symbols: " + ", ".join(missing), file=sys.stderr)
        return 2

    data_end = symbols["__data_end__"][0]
    sp_usr = symbols["__sp_usr"][0]
    gap = sp_usr - data_end

    iwram = sections.get(".iwram")
    bss = sections.get(".bss")
    data = sections.get(".data")

    print("IWRAM budget:")
    if iwram:
        print(f"  .iwram code : {fmt_bytes(iwram.size)} (0x{iwram.addr:08X}-0x{iwram.addr + iwram.size:08X})")
    if bss:
        print(f"  .bss        : {fmt_bytes(bss.size)}")
    if data:
        print(f"  .data       : {fmt_bytes(data.size)}")
    print(f"  data end    : 0x{data_end:08X}")
    print(f"  user stack  : 0x{sp_usr:08X}")
    print(f"  safe gap    : {fmt_bytes(gap)} (required {fmt_bytes(args.min_gap)})")

    failed = False
    if gap < args.min_gap:
        print(
            f"FAIL: IWRAM-to-stack gap is {gap} bytes; at least {args.min_gap} bytes are required.",
            file=sys.stderr,
        )
        failed = True

    handles = symbols.get("handles")
    if handles:
        print(f"  libgba handles table detected: {fmt_bytes(handles[1])} at 0x{handles[0]:08X}")
        if args.reject_libgba_handles:
            print("FAIL: libgba's handle table is still linked; the libtonc conversion did not take effect.",
                  file=sys.stderr)
            failed = True
    else:
        print("  libgba handles table: not present")

    stack_dir = args.stack_dir
    if stack_dir is None:
        # ELF normally lives in the project root; stack files live in ./build.
        stack_dir = args.elf.parent / "build"
    su_files = list(stack_dir.rglob("*.su")) if stack_dir.exists() else []
    frames = parse_stack_usage(su_files)
    if frames:
        print("Largest generated IWRAM stack frames:")
        for size, function, path in frames[:8]:
            print(f"  {size:4d} bytes  {function}  ({path.name})")
        if frames[0][0] > args.max_frame:
            print(
                f"FAIL: largest IWRAM frame is {frames[0][0]} bytes; limit is {args.max_frame} bytes.",
                file=sys.stderr,
            )
            failed = True
    else:
        print("  stack usage: no .su files found (informational only)")

    if failed:
        print("Build stopped before .gba creation to prevent an intermittent hardware crash.",
              file=sys.stderr)
        return 1

    if gap < 3072:
        print("WARNING: build passes, but less than the preferred 3,072-byte headroom remains.")
    else:
        print("PASS: IWRAM and stack headroom are within the guarded budget.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
