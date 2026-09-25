# Copyright (c) 2026 CS-Fusion. All Rights Reserved.
#
# List and extract single files from a remote .zip over HTTP range requests,
# without downloading the whole archive. The Sonniss GDC bundles are split
# into multi-gigabyte parts; the project needs a few dozen weapon recordings
# out of them, so it reads the zip directory from the end of the file and
# then fetches only the entries it wants.
#
#   python remote_zip.py list  <url> [--grep REGEX] [--json out.json]
#   python remote_zip.py fetch <url> <out_dir> --grep REGEX
#
# Runs on the Python that ships with the engine
# (Engine/Binaries/ThirdParty/Python3/Win64/python.exe); standard library only.

import argparse
import json
import os
import re
import struct
import sys
import urllib.request
import zlib

UA = {"User-Agent": "CS-Fusion asset tool (range reader)"}


def http_range(url, start, end):
    request = urllib.request.Request(url, headers=dict(UA, Range="bytes=%d-%d" % (start, end)))
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def content_length(url):
    request = urllib.request.Request(url, method="HEAD", headers=UA)
    with urllib.request.urlopen(request, timeout=60) as response:
        return int(response.headers["Content-Length"])


def read_directory(url):
    size = content_length(url)
    tail_len = min(size, 1 << 17)
    tail = http_range(url, size - tail_len, size - 1)

    eocd = tail.rfind(b"PK\x05\x06")
    if eocd < 0:
        raise RuntimeError("no end-of-central-directory record")
    (_, _, _, _, count, cd_size, cd_offset, _) = struct.unpack("<IHHHHIIH", tail[eocd:eocd + 22])

    # ZIP64: the real values live in the zip64 end record.
    locator = tail.rfind(b"PK\x06\x07")
    if locator >= 0 and (cd_offset == 0xFFFFFFFF or count == 0xFFFF or cd_size == 0xFFFFFFFF):
        (_, _, z64_offset, _) = struct.unpack("<IIQI", tail[locator:locator + 20])
        record = http_range(url, z64_offset, z64_offset + 55)
        # signature, record size, made by, needed, disk, cd disk,
        # entries on disk, entries total, cd size, cd offset
        fields = struct.unpack("<IQHHIIQQQQ", record[:56])
        count, cd_size, cd_offset = fields[7], fields[8], fields[9]

    directory = http_range(url, cd_offset, cd_offset + cd_size - 1)
    entries = []
    pos = 0
    while pos + 46 <= len(directory) and directory[pos:pos + 4] == b"PK\x01\x02":
        fields = struct.unpack("<IHHHHHHIIIHHHHHII", directory[pos:pos + 46])
        method, comp_size, uncomp_size = fields[4], fields[8], fields[9]
        name_len, extra_len, comment_len = fields[10], fields[11], fields[12]
        local_offset = fields[16]
        name = directory[pos + 46:pos + 46 + name_len].decode("utf-8", "replace")
        extra = directory[pos + 46 + name_len:pos + 46 + name_len + extra_len]

        # ZIP64 extra field carries the sizes and offset that did not fit.
        e = 0
        while e + 4 <= len(extra):
            tag, length = struct.unpack("<HH", extra[e:e + 4])
            if tag == 0x0001:
                values = list(struct.unpack("<%dQ" % (length // 8), extra[e + 4:e + 4 + (length // 8) * 8]))
                if uncomp_size == 0xFFFFFFFF and values:
                    uncomp_size = values.pop(0)
                if comp_size == 0xFFFFFFFF and values:
                    comp_size = values.pop(0)
                if local_offset == 0xFFFFFFFF and values:
                    local_offset = values.pop(0)
            e += 4 + length

        entries.append({"name": name, "method": method, "compressed": comp_size,
                        "size": uncomp_size, "offset": local_offset})
        pos += 46 + name_len + extra_len + comment_len
    return entries


def fetch_entry(url, entry, out_path):
    header = http_range(url, entry["offset"], entry["offset"] + 29)
    name_len, extra_len = struct.unpack("<HH", header[26:30])
    data_start = entry["offset"] + 30 + name_len + extra_len
    data = http_range(url, data_start, data_start + entry["compressed"] - 1)
    if entry["method"] == 8:
        data = zlib.decompress(data, -15)
    elif entry["method"] != 0:
        raise RuntimeError("unsupported compression %d for %s" % (entry["method"], entry["name"]))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as handle:
        handle.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["list", "fetch"])
    parser.add_argument("url")
    parser.add_argument("out_dir", nargs="?")
    parser.add_argument("--grep", default="")
    parser.add_argument("--json")
    args = parser.parse_args()

    entries = read_directory(args.url)
    pattern = re.compile(args.grep, re.I) if args.grep else None
    chosen = [e for e in entries if not e["name"].endswith("/") and (not pattern or pattern.search(e["name"]))]

    if args.command == "list":
        total = sum(e["size"] for e in chosen)
        for e in chosen:
            print("%10d  %s" % (e["size"], e["name"]))
        print("# %d file(s), %.1f MB (archive has %d entries)" % (len(chosen), total / 1e6, len(entries)))
        if args.json:
            with open(args.json, "w", encoding="utf-8") as handle:
                json.dump(chosen, handle, indent=1)
        return

    for e in chosen:
        out_path = os.path.join(args.out_dir, *e["name"].split("/"))
        fetch_entry(args.url, e, out_path)
        print("fetched %s (%.1f MB)" % (e["name"], e["size"] / 1e6))


if __name__ == "__main__":
    sys.exit(main())
