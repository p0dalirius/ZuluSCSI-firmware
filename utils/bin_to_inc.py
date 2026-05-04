#!/usr/bin/env python3
# Convert AS/400 disk-profile .bin captures into committable .inc files
# for #include into the corresponding profile.cpp source.
#
# Usage:
#     python utils/bin_to_inc.py <profile_dir>
#
# For every <name>.bin in <profile_dir>, writes <name>.inc next to it.
# The .inc declares a translation-unit-local
#     static const uint8_t k<TitleCase>[] = { ... };
# whose name is derived from the file stem (e.g. page_80.bin -> kPage80).
# The first comment line of the generated .inc records the source file's
# size and sha256 so drift between .bin and .inc is detectable by eye.

import argparse
import hashlib
import sys
from pathlib import Path


def stem_to_identifier(stem: str) -> str:
    # standard_inquiry -> kStandardInquiry
    # page_80          -> kPage80
    # mode_sense_all   -> kModeSenseAll
    # page_b0          -> kPageB0
    parts = stem.replace("-", "_").split("_")
    title_parts = []
    for p in parts:
        if not p:
            continue
        # Hex page suffixes like "b0" -> "B0"; word parts like "page" -> "Page".
        if len(p) == 2 and all(c in "0123456789abcdefABCDEF" for c in p):
            title_parts.append(p.upper())
        else:
            title_parts.append(p[:1].upper() + p[1:].lower())
    return "k" + "".join(title_parts)


def emit_inc(bin_path: Path, inc_path: Path, identifier: str) -> None:
    data = bin_path.read_bytes()
    sha = hashlib.sha256(data).hexdigest()

    lines = [
        f"// Generated from {bin_path.name} ({len(data)} bytes, sha256: {sha})",
        "// DO NOT EDIT - re-run utils/bin_to_inc.py",
        f"static const uint8_t {identifier}[] = {{",
    ]

    bytes_per_line = 8
    for offset in range(0, len(data), bytes_per_line):
        chunk = data[offset:offset + bytes_per_line]
        is_last = offset + bytes_per_line >= len(data)
        hexes = ", ".join(f"0x{b:02x}" for b in chunk)
        if not is_last:
            hexes += ","
        # Pad short tail row so the comment column stays aligned.
        pad = " " * (6 * (bytes_per_line - len(chunk)))
        ascii_chars = "".join(chr(b) if 0x20 <= b < 0x7f else "." for b in chunk)
        lines.append(f"    {hexes}{pad}  // 0x{offset:04x}  {ascii_chars}")

    lines.append("};")
    lines.append("")
    inc_path.write_text("\n".join(lines))


def process_dir(profile_dir: Path) -> int:
    if not profile_dir.is_dir():
        print(f"not a directory: {profile_dir}", file=sys.stderr)
        return 2
    bins = sorted(profile_dir.glob("*.bin"))
    if not bins:
        print(f"no .bin files in {profile_dir}", file=sys.stderr)
        return 1
    for b in bins:
        ident = stem_to_identifier(b.stem)
        inc = b.with_suffix(".inc")
        emit_inc(b, inc, ident)
        print(f"{b.name} -> {inc.name} ({b.stat().st_size} B, ident {ident})")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description="Convert .bin files to .inc files for AS/400 disk profiles.")
    p.add_argument("profile_dir", type=Path, help="profile directory containing *.bin files")
    args = p.parse_args()
    return process_dir(args.profile_dir)


if __name__ == "__main__":
    sys.exit(main())
