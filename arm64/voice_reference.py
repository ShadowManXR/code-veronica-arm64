"""Compare captured opening dialogue PCM with an independent original-float ADX decode.
Usage: python arm64/voice_reference.py DISC.iso PCM_DIRECTORY
Build-VoiceCheck.ps1's device executable writes voice-16.pcm and voice-17.pcm.
"""
import argparse
import array
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "port/pc/tools"))
import rdx


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def decode(data, frames):
    rate = int.from_bytes(data[8:12], "big")
    cutoff = int.from_bytes(data[16:18], "big")
    val1 = f32(f32(math.sqrt(2)) - f32(math.cos(f32(3.14159265 * 2 * cutoff / rate))))
    val2 = f32(math.sqrt(2)) - 1.0
    ratio = (val1 - f32(math.sqrt(f32((val1 + val2) * (val1 - val2))))) / val2
    k0, k1 = int(2 * ratio * 4096), int(-ratio * ratio * 4096)
    channels = data[7]
    assert channels == 1 and rate == 48000
    assert frames <= int.from_bytes(data[12:16], "big")
    pos = int.from_bytes(data[2:4], "big") + 4
    history = [[0.0, 0.0] for _ in range(channels)]
    result = array.array("h")
    for _ in range((frames + 31) // 32):
        decoded = []
        for channel in range(channels):
            block = data[pos:pos + 18]
            pos += 18
            gain = (int.from_bytes(block[:2], "big") & 8191) + 1
            values = []
            d0, d1 = history[channel]
            for byte in block[2:]:
                for nibble in (byte >> 4, byte & 15):
                    if nibble >= 8:
                        nibble -= 16
                    value = f32(f32(f32(k0 / 4096 * d0) + f32(k1 / 4096 * d1)) + f32(nibble * 65536 * gain))
                    d1, d0 = d0, value
                    values.append(max(-2147483648, min(2147483647, int(value))) >> 16)
            history[channel] = [d0, d1]
            decoded.append(values)
        for i in range(32):
            result.extend((decoded[0][i], decoded[-1][i]))
    return result[:frames * 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("pcm_directory", type=Path)
    args = parser.parse_args()
    iso = rdx.Iso(args.iso)
    archive = rdx.Afs(iso, "VOICE1.AFS")
    try:
        for track in (16, 17):
            expected = decode(archive.get(track), 48000)
            actual = array.array("h", (args.pcm_directory / f"voice-{track}.pcm").read_bytes())
            if sys.byteorder != "little":
                actual.byteswap()
            assert len(actual) == len(expected)
            errors = [abs(a - b) for a, b in zip(actual, expected)]
            mismatches = sum(error != 0 for error in errors)
            print(f"Voice {track}: {len(actual)} samples, {mismatches} mismatches, maximum error {max(errors)}")
            assert not mismatches
    finally:
        iso.f.close()


if __name__ == "__main__":
    main()
