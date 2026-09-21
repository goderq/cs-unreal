"""
CS-Fusion procedural sound generator.

Synthesises every game sound from scratch - noise, sine and saw oscillators,
filters and envelopes - and writes 16-bit mono WAV files under
SourceArt/Audio. No recordings or third-party samples are used, so the
output is entirely this project's own work (see docs/ASSETS.md).

Run with any Python 3 (the one bundled with Unreal works):
    "<UE>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Scripts/generate_audio.py

The bootstrap script then imports the WAVs into /Game/Audio. The generator is
deterministic (fixed random seed), so re-running it reproduces the same files.
"""

import math
import os
import random
import struct
import wave

RATE = 44100
ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "SourceArt", "Audio")

rng = random.Random(1337)


# ---------------------------------------------------------------------------
# Building blocks
# ---------------------------------------------------------------------------

def silence(seconds):
    return [0.0] * int(seconds * RATE)


def noise(seconds):
    return [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]


def sine(freq_start, seconds, freq_end=None, phase=0.0):
    freq_end = freq_start if freq_end is None else freq_end
    n = int(seconds * RATE)
    out = []
    for i in range(n):
        t = i / max(1, n - 1)
        f = freq_start * (freq_end / freq_start) ** t  # exponential sweep
        phase += 2.0 * math.pi * f / RATE
        out.append(math.sin(phase))
    return out


def saw(freq, seconds, detune=0.0):
    n = int(seconds * RATE)
    f = freq * (1.0 + detune)
    return [2.0 * ((i * f / RATE) % 1.0) - 1.0 for i in range(n)]


def lowpass(samples, cutoff_start, cutoff_end=None):
    """One-pole low-pass with an optionally sweeping cutoff."""
    cutoff_end = cutoff_start if cutoff_end is None else cutoff_end
    out = []
    y = 0.0
    n = len(samples)
    for i, x in enumerate(samples):
        c = cutoff_start + (cutoff_end - cutoff_start) * (i / max(1, n - 1))
        a = 1.0 - math.exp(-2.0 * math.pi * c / RATE)
        y += a * (x - y)
        out.append(y)
    return out


def highpass(samples, cutoff):
    low = lowpass(samples, cutoff)
    return [x - l for x, l in zip(samples, low)]


def bandpass(samples, low_cut, high_cut):
    return lowpass(highpass(samples, low_cut), high_cut)


def resonator(samples, freq, q=0.995):
    """Two-pole resonant filter: turns a click into a metallic ping."""
    out = []
    y1 = y2 = 0.0
    w = 2.0 * math.pi * freq / RATE
    b1 = 2.0 * q * math.cos(w)
    b2 = -q * q
    gain = (1.0 - q)
    for x in samples:
        y = gain * x + b1 * y1 + b2 * y2
        y2, y1 = y1, y
        out.append(y)
    return out


def env_exp(samples, decay_seconds, attack_seconds=0.0):
    n = len(samples)
    out = []
    attack = max(1, int(attack_seconds * RATE))
    for i, x in enumerate(samples):
        t = i / RATE
        a = min(1.0, i / attack) if attack_seconds > 0 else 1.0
        out.append(x * a * math.exp(-t / decay_seconds))
    return out


def env_adsr(samples, attack, release):
    n = len(samples)
    a = max(1, int(attack * RATE))
    r = max(1, int(release * RATE))
    out = []
    for i, x in enumerate(samples):
        g = 1.0
        if i < a:
            g = i / a
        if i > n - r:
            g *= max(0.0, (n - i) / r)
        out.append(x * g)
    return out


def mix(*tracks, offsets=None):
    offsets = offsets or [0.0] * len(tracks)
    length = max(int(o * RATE) + len(t) for t, o in zip(tracks, offsets))
    out = [0.0] * length
    for t, o in zip(tracks, offsets):
        start = int(o * RATE)
        for i, x in enumerate(t):
            out[start + i] += x
    return out


def gain(samples, g):
    return [x * g for x in samples]


def echo(samples, delays_gains):
    """Early reflections: (seconds, gain) pairs."""
    tracks = [samples] + [gain(samples, g) for _, g in delays_gains]
    return mix(*tracks, offsets=[0.0] + [d for d, _ in delays_gains])


def soft_clip(samples, drive=1.0):
    return [math.tanh(x * drive) for x in samples]


def normalize(samples, peak=0.89):
    m = max(1e-9, max(abs(x) for x in samples))
    return [x * peak / m for x in samples]


def fade_tail(samples, seconds=0.01):
    n = len(samples)
    k = min(n, int(seconds * RATE))
    return samples[:n - k] + [x * (1.0 - i / k) for i, x in enumerate(samples[n - k:])]


def write(category, name, samples, peak=0.89):
    folder = os.path.join(ROOT, category)
    os.makedirs(folder, exist_ok=True)
    data = normalize(fade_tail(samples), peak)
    path = os.path.join(folder, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, x)) * 32767)) for x in data))
    print("wrote", os.path.relpath(path, ROOT), "%.2fs" % (len(samples) / RATE))


# ---------------------------------------------------------------------------
# Weapons
# ---------------------------------------------------------------------------

def gunshot(body_hz, body_decay, crack_cut, crack_decay, tail_decay, tail_cut, length, room=0.25, drive=2.2):
    crack = env_exp(highpass(noise(0.04), 1500), 0.006)
    blast = env_exp(lowpass(noise(length), crack_cut, crack_cut * 0.15), crack_decay)
    body = env_exp(sine(body_hz * 2.2, length, body_hz), body_decay)
    tail = env_exp(lowpass(noise(length), tail_cut, tail_cut * 0.3), tail_decay, attack_seconds=0.01)
    shot = mix(gain(crack, 0.6), blast, gain(body, 0.9), gain(tail, 0.35))
    shot = soft_clip(shot, drive)
    return echo(shot, [(0.043, room), (0.071, room * 0.7), (0.118, room * 0.45)])


def mechanical_click(freq, decay=0.02, brightness=4000):
    click = env_exp(bandpass(noise(0.03), 800, brightness), 0.004)
    ping = env_exp(resonator(click + silence(0.12), freq, 0.997), decay)
    return mix(click, gain(ping, 6.0))


def reload_sequence(steps):
    """steps: list of (time, freq, level)."""
    parts = []
    offsets = []
    for t, f, lvl in steps:
        parts.append(gain(mechanical_click(f, 0.03), lvl))
        offsets.append(t)
    # Cloth / handling rustle underneath.
    rustle = env_adsr(lowpass(noise(steps[-1][0] + 0.2), 1800), 0.1, 0.2)
    parts.append(gain(rustle, 0.08))
    offsets.append(0.0)
    return mix(*parts, offsets=offsets)


def make_weapons():
    write("Weapons", "S_Pistol_Fire", gunshot(150, 0.05, 9000, 0.03, 0.10, 3000, 0.45, room=0.2))
    write("Weapons", "S_Rifle_Fire", gunshot(110, 0.08, 8000, 0.05, 0.22, 2500, 0.8, room=0.3, drive=2.8))
    write("Weapons", "S_Rifle2_Fire", gunshot(125, 0.07, 10000, 0.045, 0.18, 3200, 0.75, room=0.28, drive=2.5))
    write("Weapons", "S_SMG_Fire", gunshot(170, 0.04, 11000, 0.025, 0.09, 3500, 0.4, room=0.18))
    write("Weapons", "S_Shotgun_Fire", gunshot(70, 0.14, 6000, 0.09, 0.35, 1800, 1.1, room=0.35, drive=3.2))
    write("Weapons", "S_Sniper_Fire", gunshot(90, 0.12, 12000, 0.07, 0.6, 2200, 1.8, room=0.45, drive=3.5))

    write("Weapons", "S_Empty", mechanical_click(2600, 0.015, 6000))
    write("Weapons", "S_Pistol_Reload", reload_sequence([(0.0, 1900, 0.8), (0.55, 2300, 1.0), (1.0, 3100, 0.9)]))
    write("Weapons", "S_Rifle_Reload", reload_sequence([(0.0, 1500, 0.9), (0.2, 900, 0.5), (0.9, 1800, 1.0), (1.45, 2600, 1.0), (1.65, 2200, 0.8)]))
    write("Weapons", "S_Shotgun_Reload", reload_sequence([(0.0, 1300, 0.7), (0.35, 1300, 0.7), (0.7, 1300, 0.7), (1.2, 1100, 1.0), (1.4, 1600, 1.0)]))
    write("Weapons", "S_Equip", mix(env_adsr(lowpass(noise(0.3), 2500), 0.05, 0.15), gain(mechanical_click(2000, 0.02), 0.8), offsets=[0.0, 0.22]))


# ---------------------------------------------------------------------------
# Player, impacts, feedback
# ---------------------------------------------------------------------------

def footstep(variant):
    thud = env_exp(lowpass(noise(0.12), 500 + variant * 60), 0.025)
    scuff = env_exp(bandpass(noise(0.1), 1500, 5000 + variant * 400), 0.018)
    return mix(thud, gain(scuff, 0.35), offsets=[0.0, 0.008 + variant * 0.003])


def make_player():
    for i in range(4):
        write("Player", "S_Footstep_%02d" % (i + 1), footstep(i), peak=0.7)
    write("Player", "S_Land", mix(env_exp(lowpass(noise(0.25), 350), 0.06), gain(footstep(1), 0.6)), peak=0.8)
    death_body = env_exp(lowpass(noise(0.6), 300), 0.12)
    death_gear = gain(reload_sequence([(0.05, 1200, 0.5), (0.18, 900, 0.4)]), 0.5)
    write("Player", "S_Death", mix(death_body, death_gear, env_exp(sine(180, 0.5, 60), 0.1)))
    write("Player", "S_Pickup", mix(env_exp(sine(700, 0.14, 1400), 0.06), gain(mechanical_click(2400, 0.03), 0.5)), peak=0.7)
    write("Player", "S_Respawn", env_adsr(mix(lowpass(noise(0.9), 800, 4000), gain(sine(220, 0.9, 660), 0.4)), 0.4, 0.4), peak=0.6)


def make_impacts():
    surface = mix(env_exp(highpass(noise(0.05), 2000), 0.008), env_exp(lowpass(noise(0.2), 2500, 600), 0.04),
                  gain(env_exp(sine(3200, 0.25, 1800), 0.05), 0.25))
    write("Impacts", "S_Impact_Surface", surface, peak=0.75)
    body = mix(env_exp(lowpass(noise(0.18), 900), 0.035), env_exp(sine(140, 0.15, 70), 0.04))
    write("Impacts", "S_Impact_Body", body, peak=0.8)


def make_feedback():
    write("Feedback", "S_HitMarker", env_exp(mix(sine(2600, 0.05), gain(sine(5200, 0.05), 0.3)), 0.012), peak=0.55)
    write("Feedback", "S_Headshot", env_exp(mix(sine(1760, 0.45), gain(sine(3520, 0.45), 0.5), gain(sine(2640, 0.45), 0.3)), 0.11), peak=0.6)
    kill = mix(env_exp(sine(880, 0.12), 0.05), env_exp(sine(1320, 0.3), 0.09), offsets=[0.0, 0.09])
    write("Feedback", "S_Kill", kill, peak=0.6)
    write("Feedback", "S_Hurt", mix(env_exp(lowpass(noise(0.3), 400), 0.06), env_exp(sine(90, 0.3, 50), 0.08)), peak=0.75)


def make_ui():
    write("UI", "S_UI_Click", env_exp(mix(sine(1800, 0.04), gain(highpass(noise(0.04), 3000), 0.3)), 0.008), peak=0.45)
    write("UI", "S_UI_Hover", env_exp(sine(1200, 0.03), 0.006), peak=0.18)


# ---------------------------------------------------------------------------
# Music: a slow ambient loop for the main menu
# ---------------------------------------------------------------------------

def make_music():
    bpm = 72.0
    bar = 4 * 60.0 / bpm              # 3.33 s
    chords = [                        # Am - F - C - G, two bars each
        [110.00, 130.81, 164.81, 220.00],
        [87.31, 130.81, 174.61, 220.00],
        [130.81, 164.81, 196.00, 261.63],
        [98.00, 146.83, 196.00, 246.94],
    ]
    chord_len = 2 * bar
    total = chord_len * len(chords)
    n = int(total * RATE)
    out = [0.0] * n

    for ci, chord in enumerate(chords):
        start = int(ci * chord_len * RATE)
        seg = int(chord_len * RATE)
        pad = [0.0] * seg
        for note in chord:
            for det in (-0.004, 0.0, 0.004):
                voice = saw(note, chord_len, det)
                for i in range(seg):
                    pad[i] += voice[i]
        pad = lowpass(pad, 700, 1100)
        # Soft swell per chord, overlapping into the next one.
        for i in range(seg):
            t = i / seg
            g = math.sin(math.pi * min(1.0, t * 1.15)) ** 1.5
            out[(start + i) % n] += pad[i] * g * 0.05

    # Gentle pulse on the root, one per beat.
    beat = 60.0 / bpm
    for b in range(int(total / beat)):
        chord = chords[int((b * beat) // chord_len) % len(chords)]
        tone = env_exp(sine(chord[0] * 2, 0.8), 0.25)
        start = int(b * beat * RATE)
        for i, x in enumerate(tone):
            out[(start + i) % n] += x * 0.12

    # Airy noise bed.
    air = lowpass(noise(total), 3000)
    for i in range(n):
        out[i] += air[i] * 0.01

    write("Music", "S_Music_Menu", out, peak=0.6)


def make_grenade():
    # v1.1 grenade. Its own random stream, so the earlier sounds stay byte-identical.
    global rng
    rng = random.Random(4242)
    boom = mix(
        env_exp(sine(62, 1.6, 26), 0.45),
        gain(env_exp(lowpass(noise(1.8), 1800, 250), 0.35), 1.2),
        gain(env_exp(highpass(noise(0.12), 2500), 0.03), 0.8),
    )
    boom = echo(soft_clip(boom, 2.6), [(0.09, 0.35), (0.21, 0.22), (0.43, 0.12)])
    write("Weapons", "S_Grenade_Explode", fade_tail(boom, 0.2), peak=0.95)
    clink = mix(resonator(env_exp(noise(0.25), 0.004), 2300, 0.9985), gain(resonator(env_exp(noise(0.25), 0.004), 3700, 0.998), 0.6))
    write("Weapons", "S_Grenade_Bounce", env_exp(clink, 0.07), peak=0.5)
    pin = mix(mechanical_click(3200, 0.012, 7000), gain(resonator(env_exp(noise(0.35), 0.003), 4100, 0.9992), 0.35), offsets=[0.0, 0.05])
    write("Weapons", "S_Grenade_Pin", pin, peak=0.55)


if __name__ == "__main__":
    make_weapons()
    make_player()
    make_impacts()
    make_feedback()
    make_ui()
    make_music()
    make_grenade()
    print("done ->", ROOT)
