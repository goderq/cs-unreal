"""
v1.1 weapon sounds from real recordings.

Source: "The Free Firearm Sound Library" by Ben Jaszczak, Brian Nelson,
Kevin Heras and Matthew Nanney - CC0 (public domain), from
https://opengameart.org/content/the-free-firearm-sound-library
(see docs/ASSETS.md). The library is 96 kHz / 24-bit stereo takes of 10-15 s
each; this script cuts the first shot out of a take - from just before its
attack through its natural tail - fades it, normalises it and writes a 44.1 kHz
16-bit MONO file (mono so Unreal can spatialise it) under SourceArt/Audio/Weapons.

Only the prepared clips are committed; the raw library is not (it stays in
SourceArt/_download, which is ignored).

Run with the Python bundled with Unreal (it has the `audioop` module):
    "<UE>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Scripts/prepare_weapon_sounds.py
"""

import audioop
import os
import struct
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, "SourceArt", "_download", "lib", "Prepared SFX Library")
OUT = os.path.join(ROOT, "SourceArt", "Audio", "Weapons")
RATE = 44100

# output name: (take, seconds kept after the attack, fade-out share, gain)
CLIPS = {
    "S_Real_Pistol_Fire": ("Walther PPQ/X_39P.wav", 0.95, 0.55, 1.0),       # 9 mm striker pistol
    "S_Real_AK47_Fire": ("AK-47/C_28P.wav", 1.25, 0.6, 1.0),               # 7.62x39, single shot, near
    "S_Real_M4_Fire": ("AR-15/D_32P.wav", 1.15, 0.6, 1.0),                 # AR-15 / M4 5.56, near
    "S_Real_SMG_Fire": ("Carl Gustav M45/G_31P.wav", 0.7, 0.6, 1.0),        # 9 mm sub machine gun
    "S_Real_Shotgun_Fire": ("Nova/O_21P.wav", 1.5, 0.6, 1.0),              # Benelli Nova 12 gauge
    "S_Real_Sniper_Fire": ("Tikka/W_29P.wav", 2.2, 0.65, 1.0),             # Tikka T3 .30-06 bolt action
}


def read_mono16(path):
    w = wave.open(path, "rb")
    channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    data = w.readframes(frames)
    w.close()
    if width != 2:
        data = audioop.lin2lin(data, width, 2)
    if channels == 2:
        data = audioop.tomono(data, 2, 0.5, 0.5)
    if rate != RATE:
        data, _ = audioop.ratecv(data, 2, 1, rate, RATE, None)
    return list(struct.unpack("<%dh" % (len(data) // 2), data))


def cut_shot(samples, keep_seconds, fade_share):
    peak = max(abs(s) for s in samples)
    threshold = peak * 0.25
    onset = next(i for i, s in enumerate(samples) if abs(s) >= threshold)
    start = max(0, onset - int(0.004 * RATE))          # keep the first milliseconds of the attack
    end = min(len(samples), onset + int(keep_seconds * RATE))
    clip = [float(s) for s in samples[start:end]]
    # Short fade-in (click-free), long fade-out that follows the natural tail.
    fade_in = int(0.002 * RATE)
    for i in range(min(fade_in, len(clip))):
        clip[i] *= i / fade_in
    fade = int(len(clip) * fade_share)
    for i in range(fade):
        k = len(clip) - fade + i
        t = i / max(1, fade - 1)
        clip[k] *= (1.0 - t) ** 2
    return clip


def write(name, clip, gain):
    peak = max(1.0, max(abs(s) for s in clip))
    scale = 32767.0 * 0.95 * gain / peak
    data = struct.pack("<%dh" % len(clip), *[int(max(-32768, min(32767, s * scale))) for s in clip])
    path = os.path.join(OUT, name + ".wav")
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(data)
    w.close()
    print("wrote %s  %.2fs" % (os.path.relpath(path, ROOT), len(clip) / RATE))


def main():
    if not os.path.isdir(LIB):
        raise SystemExit("Library not found at %s - download it first (see the docstring)." % LIB)
    os.makedirs(OUT, exist_ok=True)
    for name, (take, keep, fade, gain) in CLIPS.items():
        samples = read_mono16(os.path.join(LIB, take))
        write(name, cut_shot(samples, keep, fade), gain)


if __name__ == "__main__":
    main()
