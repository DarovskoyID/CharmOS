#!/usr/bin/env python3
"""Verify QEMU's captured HDA output after clean guest shutdown."""
import array
from pathlib import Path
import sys
import wave

with wave.open(str(Path(sys.argv[1]) / "hda-output.wav"), "rb") as capture:
    assert (capture.getnchannels(), capture.getsampwidth(), capture.getframerate()) == (2, 2, 48000)
    samples = array.array("h", capture.readframes(capture.getnframes()))
if sys.byteorder != "little":
    samples.byteswap()
nonzero = sum(sample != 0 for sample in samples)
assert nonzero >= 512, f"expected PCM signal, got {nonzero} nonzero samples"
assert min(samples) < 0 < max(samples), "expected both waveform polarities"
print(f"hda capture: stereo 48000 s16 verified; {nonzero} nonzero samples")
