#!/usr/bin/env python3
"""Normalize plain ATRAC3 RIFF/WAVE for PS3 XMB SND0.AT3.

Some encoder-written fact sample counts exceed what the encoded frames can
produce after decoder delay and Sony's required tail padding. The PS3 XMB can
silently reject those files. Clamp the count, write the matching delay, and
include a standard smpl loop so the preview repeats while the app is selected.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

SAMPLES_PER_FRAME = 1024
TAIL_PADDING_SAMPLES = 368
DEFAULT_DELAY_SAMPLES = 1024
MAX_PS3_XMB_BYTES = int(2.4 * 1024 * 1024)


def parse_chunks(blob: bytes) -> list[tuple[bytes, bytes]]:
    if len(blob) < 12 or blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("input is not a RIFF/WAVE file")
    if struct.unpack_from("<I", blob, 4)[0] + 8 != len(blob):
        raise ValueError("RIFF size field does not match file length")
    chunks = []
    offset = 12
    while offset + 8 <= len(blob):
        tag = blob[offset:offset + 4]
        size = struct.unpack_from("<I", blob, offset + 4)[0]
        start, end = offset + 8, offset + 8 + size
        if end > len(blob):
            raise ValueError(f"{tag!r} chunk extends past end of file")
        chunks.append((tag, blob[start:end]))
        offset = end + (size & 1)
    if offset != len(blob):
        raise ValueError("trailing/truncated bytes after RIFF chunks")
    return chunks


def chunk_bytes(tag: bytes, payload: bytes) -> bytes:
    if len(tag) != 4:
        raise ValueError("RIFF chunk tag must be 4 bytes")
    pad = b"\0" if len(payload) & 1 else b""
    return tag + struct.pack("<I", len(payload)) + payload + pad


def smpl_payload(sample_rate: int, delay: int, sample_count: int) -> bytes:
    # 60-byte standard WAVE sampler chunk with one forward, infinite loop.
    smpl = bytearray(60)
    struct.pack_into("<I", smpl, 8, round(1_000_000_000 / sample_rate))
    struct.pack_into("<I", smpl, 12, 60)
    struct.pack_into("<I", smpl, 28, 1)
    struct.pack_into("<I", smpl, 32, 24)
    struct.pack_into("<6I", smpl, 36, 0, 0, delay, delay + sample_count - 1, 0, 0)
    return bytes(smpl)


def normalize(blob: bytes) -> tuple[bytes, dict[str, int | str]]:
    chunks = parse_chunks(blob)
    by_tag: dict[bytes, list[bytes]] = {}
    for tag, data in chunks:
        by_tag.setdefault(tag, []).append(data)
    if len(by_tag.get(b"fmt ", [])) != 1 or len(by_tag.get(b"data", [])) != 1:
        raise ValueError("expected exactly one fmt and one data chunk")

    fmt, data = by_tag[b"fmt "][0], by_tag[b"data"][0]
    if len(fmt) < 16:
        raise ValueError("fmt chunk is too short")
    tag, channels, sample_rate = struct.unpack_from("<HHI", fmt, 0)
    block_align = struct.unpack_from("<H", fmt, 12)[0]
    if tag != 0x0270:
        raise ValueError(f"expected plain ATRAC3 format tag 0x0270, got {tag:#06x}")
    if channels != 2:
        raise ValueError(f"expected stereo audio, got {channels} channel(s)")
    if sample_rate != 44100:
        raise ValueError(f"expected 44100 Hz, got {sample_rate} Hz")
    if block_align <= 0 or not data or len(data) % block_align:
        raise ValueError("data size is empty or not an integral number of ATRAC3 frames")

    frame_count = len(data) // block_align
    fact_chunks = by_tag.get(b"fact", [])
    if len(fact_chunks) > 1:
        raise ValueError("multiple fact chunks are ambiguous")
    if fact_chunks:
        fact = fact_chunks[0]
        if len(fact) < 4:
            raise ValueError("fact chunk is too short")
        original_samples = struct.unpack_from("<I", fact, 0)[0]
        delay = struct.unpack_from("<I", fact, 4)[0] if len(fact) >= 8 else DEFAULT_DELAY_SAMPLES
    else:
        original_samples, delay = frame_count * SAMPLES_PER_FRAME, DEFAULT_DELAY_SAMPLES

    capacity = frame_count * SAMPLES_PER_FRAME - delay - TAIL_PADDING_SAMPLES
    if capacity <= 0:
        raise ValueError("audio is too short after codec delay and tail padding")
    samples = min(original_samples, capacity)
    if samples <= 0:
        raise ValueError("fact sample count is empty")

    fact_payload = struct.pack("<II", samples, delay)
    loop_payload = smpl_payload(sample_rate, delay, samples)
    known = {b"fmt ", b"fact", b"smpl", b"data"}
    extras = [(t, d) for t, d in chunks if t not in known]
    payload = b"".join((
        chunk_bytes(b"fmt ", fmt),
        chunk_bytes(b"fact", fact_payload),
        chunk_bytes(b"smpl", loop_payload),
        chunk_bytes(b"data", data),
        *(chunk_bytes(t, d) for t, d in extras),
    ))
    result = b"RIFF" + struct.pack("<I", len(payload) + 4) + b"WAVE" + payload
    if len(result) > MAX_PS3_XMB_BYTES:
        raise ValueError(f"SND0.AT3 is {len(result)} bytes, over PS3 XMB limit {MAX_PS3_XMB_BYTES}")
    info = {
        "format_tag": tag, "channels": channels, "sample_rate": sample_rate,
        "block_align": block_align, "frames": frame_count,
        "original_samples": original_samples, "samples": samples, "delay": delay,
        "duration_seconds": round(samples / sample_rate, 3), "size_bytes": len(result),
        "sha256": hashlib.sha256(result).hexdigest(),
    }
    return result, info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="source assets/ps3/SND0.AT3")
    parser.add_argument("output", type=Path, nargs="?", help="destination staged as SND0.AT3")
    parser.add_argument("--check", action="store_true", help="compare output with normalized source")
    args = parser.parse_args()
    if args.check and args.output is None:
        parser.error("--check requires source and output paths")
    if not args.check and args.output is None:
        parser.error("output path is required unless --check is used")

    try:
        normalized, info = normalize(args.source.read_bytes())
        if args.check:
            if args.output.read_bytes() != normalized:
                print("SND0.AT3 is not the normalized XMB-compatible output", file=sys.stderr)
                return 1
            print("SND0.AT3 validation passed: normalized bytes match staged package asset")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(normalized)
            print(f"Prepared {args.output}")
        print("ATRAC3: {channels} ch, {sample_rate} Hz, {frames} frames, "
              "fact {original_samples}->{samples} samples, delay {delay}, "
              "duration {duration_seconds}s, {size_bytes} bytes, SHA-256 {sha256}".format(**info))
    except (OSError, ValueError) as exc:
        print(f"prepare_snd0: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
