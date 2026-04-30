#!/usr/bin/env bash
#
# dump_vpd.sh - Dump SCSI Vital Product Data (VPD) pages from a device.
#
# Usage:
#   sudo ./dump_vpd.sh /dev/sdX [output_dir]
#
# Requires: sg3_utils (sg_vpd, sg_inq).
#
set -euo pipefail

# ---------- args ----------
if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <device> [output_dir]" >&2
    echo "Example: $0 /dev/sdb ./vpd_dump" >&2
    exit 1
fi

DEV="$1"
OUTDIR="${2:-vpd_$(basename "$DEV")_$(date +%Y%m%d_%H%M%S)}"

# ---------- sanity checks ----------
for tool in sg_vpd sg_inq; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: '$tool' not found. Install sg3_utils." >&2
        exit 1
    fi
done

if [[ ! -e "$DEV" ]]; then
    echo "Error: device '$DEV' does not exist." >&2
    exit 1
fi

if [[ ! -r "$DEV" ]]; then
    echo "Warning: '$DEV' is not readable by current user. You probably need root." >&2
fi

mkdir -p "$OUTDIR"
echo "[*] Dumping VPD pages of $DEV into $OUTDIR/"

# ---------- standard INQUIRY (not a VPD page, but useful context) ----------
echo "[*] Standard INQUIRY -> standard_inquiry.txt"
sg_inq "$DEV" > "$OUTDIR/standard_inquiry.txt" 2>&1 || true
sg_inq --raw "$DEV" > "$OUTDIR/standard_inquiry.bin"  2>/dev/null || true

# ---------- enumerate supported VPD pages (page 0x00) ----------
echo "[*] Reading Supported VPD Pages (0x00)"
sg_vpd --page=0x00 "$DEV" > "$OUTDIR/page_00_supported.txt" 2>&1 || {
    echo "Error: cannot read page 0x00 from $DEV" >&2
    exit 1
}
sg_vpd --page=0x00 --raw "$DEV" > "$OUTDIR/page_00_supported.bin" 2>/dev/null || true

# Extract the list of supported page codes (hex bytes after the header).
# `sg_vpd -p 0x00 -HHH` prints raw hex bytes; we parse those.
mapfile -t PAGES < <(
    sg_vpd --page=0x00 -HHH "$DEV" 2>/dev/null \
        | tr -s ' \t\n' '\n' \
        | grep -E '^[0-9a-fA-F]{2}$' \
        | awk 'NR>4 {print "0x"$1}'   # skip 4-byte VPD header
)

if [[ ${#PAGES[@]} -eq 0 ]]; then
    echo "[!] Could not parse supported page list; falling back to common pages."
    PAGES=(0x00 0x80 0x83 0x85 0x86 0x87 0x89 0x8a 0x8b 0x8c 0x8d 0x8f
           0x90 0x91 0xb0 0xb1 0xb2 0xb3 0xb4 0xb5 0xb6 0xb7)
fi

echo "[*] Device advertises ${#PAGES[@]} VPD page(s): ${PAGES[*]}"

# ---------- dump each page in decoded + raw form ----------
for p in "${PAGES[@]}"; do
    name="page_${p#0x}"
    echo "    - $p"
    sg_vpd --page="$p"        "$DEV" > "$OUTDIR/${name}.txt"  2>&1 || true
    sg_vpd --page="$p" --raw  "$DEV" > "$OUTDIR/${name}.bin"  2>/dev/null || true
    sg_vpd --page="$p" -HHH   "$DEV" > "$OUTDIR/${name}.hex"  2>/dev/null || true
done

# ---------- summary ----------
{
    echo "VPD dump for $DEV"
    echo "Date: $(date -Is)"
    echo "Pages dumped: ${PAGES[*]}"
} > "$OUTDIR/SUMMARY.txt"

echo "[+] Done. Files in $OUTDIR/"
