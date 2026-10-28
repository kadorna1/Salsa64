#!/usr/bin/env python3
"""Validate the RIFF/ATRAC3 SND0 metadata used for the PS3 XMB sound preview.

The XMB icon player can silently reject an otherwise decodable AT3 file when
its RIFF fact chunk claims more samples than the compressed frames can produce.
For this 44.1 kHz stereo ATRAC3 stream each frame represents 1024 PCM samples.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

FACT_MARGIN = 368
SAMPLES_PER_FRAME = 1024
MAX_XMB_BYTES = 2_400_000


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/ps3/SND0.AT3")
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    if u32(data, 4) + 8 != len(data):
        raise SystemExit(f"{path}: RIFF size does not match file size")
    if len(data) > MAX_XMB_BYTES:
        raise SystemExit(f"{path}: {len(data)} bytes exceeds the 2.4 MB XMB content-info budget")

    chunks: dict[bytes, bytes] = {}
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = u32(data, pos + 4)
        begin, end = pos + 8, pos + 8 + size
        if end > len(data):
            raise SystemExit(f"{path}: truncated {tag!r} chunk")
        chunks[tag] = data[begin:end]
        pos = end + (size & 1)
    if pos != len(data):
        raise SystemExit(f"{path}: malformed RIFF chunk alignment")
    for required in (b"fmt ", b"fact", b"data", b"smpl"):
        if required not in chunks:
            raise SystemExit(f"{path}: missing {required.decode('ascii')} chunk")

    fmt, fact, payload, smpl = (chunks[b"fmt "], chunks[b"fact"], chunks[b"data"], chunks[b"smpl"])
    if len(fmt) < 18 or len(fact) < 8 or len(smpl) < 60:
        raise SystemExit(f"{path}: a metadata chunk is too short")
    codec, channels = u16(fmt, 0), u16(fmt, 2)
    sample_rate, block_align = u32(fmt, 4), u16(fmt, 12)
    if (codec, channels, sample_rate, block_align) != (0x0270, 2, 44100, 384):
        raise SystemExit(
            f"{path}: unexpected format tag/channels/rate/blockAlign "
            f"{codec:#x}/{channels}/{sample_rate}/{block_align}"
        )
    if len(payload) % block_align:
        raise SystemExit(f"{path}: data length is not aligned to ATRAC3 frames")
    frames = len(payload) // block_align
    capacity = frames * SAMPLES_PER_FRAME
    samples, delay = struct.unpack_from("<II", fact)
    if samples + delay + FACT_MARGIN > capacity:
        raise SystemExit(
            f"{path}: XMB-silent fact overclaim: samples={samples}, delay={delay}, "
            f"margin={FACT_MARGIN}, capacity={capacity}"
        )

    loop_count = u32(smpl, 28)
    if loop_count < 1:
        raise SystemExit(f"{path}: smpl chunk has no playback loop")
    loop_start, loop_end = u32(smpl, 44), u32(smpl, 48)
    if loop_start > loop_end or loop_end >= capacity:
        raise SystemExit(f"{path}: invalid loop range {loop_start}..{loop_end} (capacity {capacity})")

    print(
        f"{path}: OK — ATRAC3 stereo {sample_rate} Hz, {frames} frames, "
        f"{len(data)} bytes; fact samples={samples}, delay={delay}, "
        f"margin available={capacity - samples - delay}; smpl loop={loop_start}..{loop_end}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
