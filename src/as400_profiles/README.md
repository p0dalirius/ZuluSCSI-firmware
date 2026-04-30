# AS/400 disk profiles

This directory holds the per-disk-model byte captures and metadata used by
the AS/400 emulation path when responding to standard INQUIRY, VPD page
reads, and MODE SENSE (all-pages) commands. Each subdirectory describes
one IBM AS/400 disk model.

| Profile     | IBM FRU   | SCSI product id | Underlying drive   |
|-------------|-----------|-----------------|--------------------|
| `dgvs09u/`  | `09L4044` | `DGVS09U`       | IBM 9.1 GB         |
| `xcpr036/`  | `53P3239` | `XCPR036`       | Seagate ST336753LC |

Set `AS400_IBMDiskPartNumber = "<FRU>"` in the relevant `[SCSIn]` section
of `zuluscsi.ini` to select a profile at runtime. An unset or unmatched
value falls back to `dgvs09u`.

## Per-profile directory layout

```
src/as400_profiles/<model>/
├── SOURCE.txt              # provenance: device, date, sg3_utils versions
├── profile.cpp             # as400_disk_profile_t instance + injection table
├── standard_inquiry.bin    # raw standard INQUIRY response capture
├── standard_inquiry.inc    # generated C array
├── page_<NN>.bin           # raw VPD page capture, one per supported page
├── page_<NN>.inc           # generated C array
├── mode_sense_all.bin      # 6-byte-format MODE SENSE (all pages) reply
└── mode_sense_all.inc      # generated C array
```

`.bin` files are the source of truth -- a fresh capture diffs cleanly
against them. `.inc` files are committed generated artifacts so the
PlatformIO build requires no pre-build step. Re-run
`utils/bin_to_inc.py src/as400_profiles/<model>/` after editing any
`.bin` to refresh the corresponding `.inc`.

## Adding a new profile

End-to-end flow:

1. Capture VPD pages with `utils/dump_vpd.sh`.
2. Capture MODE SENSE with `sg_modes -a -H`.
3. Re-encode the MODE SENSE reply from 10-byte to 6-byte format if needed.
4. Drop the `.bin` files into a new `src/as400_profiles/<model>/` directory.
5. Run `utils/bin_to_inc.py` to generate `.inc` files.
6. Write `profile.cpp` with the metadata and injection tables.
7. Register the profile in `profiles_index.cpp`.
8. Document the new FRU in `zuluscsi.ini`.

### 1. Capture VPD pages

Find the SCSI device node assigned to the target IBM disk -- **not** the
host's boot disk:

```bash
lsscsi -g
# Look for a row whose vendor is "IBM" and product matches your disk.
# The /dev/sg<N> on the right-hand column is what dump_vpd.sh wants.
```

Run the dump script (requires `sg3_utils`):

```bash
sudo ./utils/dump_vpd.sh /dev/sgN ./vpd_dump
```

Output (`./vpd_dump/`):

| File                    | Format          | Used for                       |
|-------------------------|-----------------|--------------------------------|
| `standard_inquiry.bin`  | raw bytes       | imported into the profile      |
| `standard_inquiry.txt`  | sg_inq decoded  | reference                      |
| `page_<NN>.bin`         | raw bytes       | imported into the profile      |
| `page_<NN>.txt`         | sg_vpd decoded  | reference                      |
| `page_<NN>.hex`         | hex dump        | sanity-check                   |
| `page_00_supported.*`   | enumeration     | which pages the disk advertises|
| `SUMMARY.txt`           | metadata        | source for `SOURCE.txt`        |

Open `standard_inquiry.txt` and confirm vendor / product / revision match
the disk you meant to dump. **Common mistake:** capturing `/dev/sg0` and
ending up with the host system's boot disk instead of the IBM disk on
the SCSI HBA.

### 2. Capture MODE SENSE

```bash
sudo sg_modes -a -H /dev/sgN > /tmp/modes.txt
```

`-a` requests all pages; `-H` prints hex output.

### 3. Re-encode MODE SENSE to 6-byte format if necessary

`sg_modes` defaults to a 10-byte response (8-byte header). The firmware's
AS/400 mode-sense path replies to 6-byte commands (4-byte header), so a
10-byte capture must be re-headered before commit. The transformation is
mechanical:

```text
10-byte header (8 bytes):           6-byte header (4 bytes):
    [0..1] = mode data length          [0] = mode data length
    [2]    = medium type               [1] = medium type
    [3]    = device-specific param     [2] = device-specific param
    [4..5] = reserved                  [3] = block descriptor length
    [6..7] = block descriptor length
```

The payload (block descriptor + mode pages) is identical between the two
formats. The `xcpr036/SOURCE.txt` records a worked example, and the
commit that introduced `xcpr036/mode_sense_all.bin` carries the small
Python helper that performed the conversion.

### 4. Drop captures into a new profile directory

```bash
mkdir -p src/as400_profiles/<model>
cp ./vpd_dump/standard_inquiry.bin src/as400_profiles/<model>/
cp ./vpd_dump/page_*.bin            src/as400_profiles/<model>/
# Only the pages page 0x00 lists as supported. Drop stale leftovers from
# any earlier wrong-target dump (the script does not clean its output dir).
cp /path/to/mode_sense_all.bin      src/as400_profiles/<model>/
```

Write `SOURCE.txt` next to the bins with the dump device, date, decoded
identification, and any format notes future maintainers will need.

### 5. Generate the .inc files

```bash
python3 utils/bin_to_inc.py src/as400_profiles/<model>/
```

Each `<name>.bin` becomes a `<name>.inc` containing a translation-unit-
local `static const uint8_t k<TitleCase>[]` array suitable for
`#include` from `profile.cpp`. The header comment of each `.inc` records
the source size and sha256 so drift between `.bin` and `.inc` is
detectable by eye.

### 6. Write profile.cpp

Copy `xcpr036/profile.cpp` as a template. Update:

- `partNumber` -- the IBM FRU; also the profile lookup key
- `vendorId`, `productId`, `revision` -- decoded from `standard_inquiry.txt`
- `defaultSerial` -- 8-char fallback when no SD/MCU id is available
- `modelName` -- human-readable, free-form
- `kPages[]` -- one entry per `.inc` you generated
- Injection tables -- match each captured byte offset to the matching
  `AS400_INJECT_*` enum value declared in `as400_profile.h`. Browse the
  `xcpr036` and `dgvs09u` profiles for worked examples of which slots
  each enum value addresses.

### 7. Register the profile

Edit `profiles_index.cpp`: declare the new instance and add it to
`kProfiles[]`:

```c++
extern "C" const as400_disk_profile_t as400_profile_<model>;

const as400_disk_profile_t *const kProfiles[] = {
    &as400_profile_dgvs09u,
    &as400_profile_xcpr036,
    &as400_profile_<model>,         // <-- new
};
```

### 8. Document in zuluscsi.ini

Append the new FRU to the supported-profiles table in the comment block
above `AS400_IBMDiskPartNumber` so future users know the value to set.

## Verification

Build at least one target:

```bash
pio run -e ZuluSCSI_Pico
```

On hardware, the boot log emits one line per AS/400 target:

```text
---- Loaded default AS/400 inquiry data for SCSI ID 0 (profile: <model name>)
```

Confirm the reported profile matches what you registered. If you see
`profile: IBM DGVS09U 9.1GB` unexpectedly, the FRU lookup didn't match
-- check that `AS400_IBMDiskPartNumber` is set in `[SCSIn]` and matches
`partNumber` exactly (case-insensitive).

For a deeper check, run `utils/dump_vpd.sh` against the *emulated*
target on a host machine and diff its output against the original
captures committed to this directory.
