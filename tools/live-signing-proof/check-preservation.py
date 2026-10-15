#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare the exact admitted input with signed Annex-B; no decoding/normalization."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

UUID = bytes.fromhex("005bc93f2d715e95ada4796f90877a6f")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def nals(data):
    starts = list(re.finditer(b"\x00\x00(?:\x00)?\x01", data))
    require(starts and starts[0].start() == 0, "missing initial start code")
    units = [data[m.end():starts[i + 1].start() if i + 1 < len(starts) else len(data)]
             for i, m in enumerate(starts)]
    require(all(units), "empty NAL")
    return units


def signing(unit):
    if unit[0] & 31 != 6:
        return False
    # SEI first message type and size, followed by official UUID. Generated
    # ONVIF NALs use user_data_unregistered; retain all other SEI unchanged.
    if len(unit) < 3 or unit[1] != 5:
        return False
    offset = 2
    while offset < len(unit) and unit[offset] == 255:
        offset += 1
    return unit[offset + 1:offset + 17] == UUID


def main():
    parser = argparse.ArgumentParser(description="Check bounded H.264 parser-output payload preservation; not signature verification.")
    parser.add_argument("before", type=Path, help="admitted unsigned Annex-B")
    parser.add_argument("after", type=Path, help="signed Annex-B")
    parser.add_argument("timing", type=Path, help="AU timing.csv from the same capture")
    args = parser.parse_args()
    before_path, after_path, timing_path = args.before, args.after, args.timing
    require(before_path.stat().st_size <= 32 * 1024 * 1024 and after_path.stat().st_size <= 32 * 1024 * 1024,
            "media exceeds prototype 32 MiB limit")
    before, after = before_path.read_bytes(), after_path.read_bytes()
    original, signed = nals(before), nals(after)
    require(not any(map(signing, original)), "input already signed")
    extra = [u for u in signed if signing(u)]
    require(extra, "no signing metadata")
    require(original == [u for u in signed if not signing(u)], "original NAL payload/order changed")
    vcl = lambda units: [u for u in units if 1 <= u[0] & 31 <= 5]
    require(vcl(original) == vcl(signed), "VCL payload/order changed")
    with timing_path.open(newline="") as timing_file:
        rows = list(csv.DictReader(timing_file))
    require(rows, "empty AU sidecar")
    input_end = output_end = counted_nals = 0
    for index, row in enumerate(rows):
        require(int(row["au"]) == index, 'invalid ranges/counts')
        require(int(row["input_offset"]) == input_end, 'invalid ranges/counts')
        require(int(row["output_offset"]) == output_end, 'invalid ranges/counts')
        require(int(row["input_size"]) > 0 and int(row["output_size"]) > 0, "non-positive AU size")
        input_end += int(row["input_size"])
        output_end += int(row["output_size"])
        require(input_end <= len(before) and output_end <= len(after), "AU range exceeds media")
        au_in = nals(before[int(row["input_offset"]):input_end])
        au_out = nals(after[int(row["output_offset"]):output_end])
        require(au_in == [u for u in au_out if not signing(u)], "AU association changed")
        require(len(au_in) == int(row["nals"]), 'invalid ranges/counts')
        counted_nals += len(au_in)
    require(input_end == len(before) and counted_nals == len(original), 'invalid ranges/counts')
    require(output_end < len(after), "expected finalized trailer missing")
    trailer = nals(after[output_end:])
    require(all(map(signing, trailer)), "non-signing EOS additions")
    print(json.dumps({
        "result": "PASS", "comparison": "exact NAL payload bytes excluding start codes; no normalization",
        "aus": len(rows), "original_nals": len(original), "vcl_nals": len(vcl(original)),
        "all_original_payloads_identical_in_order": True,
        "all_vcl_payloads_identical_in_order": True,
        "added_signing_seis": len(extra), "trailing_signing_seis": len(trailer),
        "au_sidecar_byte_ranges_verified": True,
        "unsigned_bytes": len(before), "signed_bytes": len(after),
        "unsigned_sha256": hashlib.sha256(before).hexdigest(),
        "signed_sha256": hashlib.sha256(after).hexdigest(),
    }, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, csv.Error) as error:
        print(f"preservation: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
