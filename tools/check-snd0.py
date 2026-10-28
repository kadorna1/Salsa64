#!/usr/bin/env python3
"""Check SND0.AT3's RIFF metadata against the PS3 XMB preview profile."""
from __future__ import annotations
import struct
import sys
from pathlib import Path

FACT_TAIL_MARGIN = 368
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
        raise SystemExit(f"{path}: {len(data)} bytes exceeds the 2.4 MB PS3 content-info limit")

    chunks: dict[bytes, tuple[int, bytes]] = {}
    pos = 12
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], u32(data, pos + 4)
        begin, end = pos + 8, pos + 8 + size
        if end > len(data):
            raise SystemExit(f"{path}: truncated {tag!r} chunk")
        chunks[tag] = (size, data[begin:end])
        pos = end + (size & 1)
    if pos != len(data):
        raise SystemExit(f"{path}: malformed RIFF chunk alignment")
    for required in (b"fmt ", b"fact", b"data", b"smpl"):
        if required not in chunks:
            raise SystemExit(f"{path}: missing {required.decode('ascii')} chunk")

    fmt_size, fmt = chunks[b"fmt "]
    fact_size, fact = chunks[b"fact"]
    smpl_size, smpl = chunks[b"smpl"]
    _, payload = chunks[b"data"]
    if fmt_size < 18 or fact_size != 12 or len(fact) != 12 or smpl_size != 60 or len(smpl) != 60:
        raise SystemExit(f"{path}: expected PS3-style fact(12) and smpl(60); got fmt={fmt_size}, fact={fact_size}, smpl={smpl_size}")
    codec, channels = u16(fmt, 0), u16(fmt, 2)
    sample_rate, block_align = u32(fmt, 4), u16(fmt, 12)
    if (codec, channels, sample_rate, block_align) != (0x0270, 2, 44100, 384):
        raise SystemExit(f"{path}: unexpected codec/channels/rate/blockAlign {codec:#x}/{channels}/{sample_rate}/{block_align}")
    if len(payload) % block_align:
        raise SystemExit(f"{path}: data length is not aligned to ATRAC3 frames")

    frames = len(payload) // block_align
    capacity = frames * SAMPLES_PER_FRAME
    samples, base_skip, sample_skip = struct.unpack_from("<III", fact)
    if base_skip != 1024 or sample_skip != base_skip + 184:
        raise SystemExit(f"{path}: unexpected PS3 ATRAC3 skip fields ({base_skip}, {sample_skip})")
    margin = capacity - samples - sample_skip
    if margin < FACT_TAIL_MARGIN:
        raise SystemExit(f"{path}: XMB-silent fact overclaim: samples={samples}, skip={sample_skip}, margin={margin}, capacity={capacity}")
    loop_count = u32(smpl, 28)
    loop_start, loop_end = u32(smpl, 44), u32(smpl, 48)
    if loop_count < 1 or loop_start != sample_skip or loop_end != sample_skip + samples - 1:
        raise SystemExit(f"{path}: invalid smpl loop ({loop_start}..{loop_end}, count={loop_count})")
    print(f"{path}: OK — PS3-style ATRAC3 fact(12), 44.1 kHz stereo, {frames} frames, {len(data)} bytes; samples={samples}, base skip={base_skip}, skip={sample_skip}, tail margin={margin}; loop={loop_start}..{loop_end}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
