"""Sound effects and a music loop, synthesized from scratch (sines, noise and envelopes), so the
project ships no third-party audio. 16-bit mono PCM WAV at 22050 Hz."""
import math
import os
import wave

import numpy as np

RATE = 22050


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def env(n, attack=0.005, release=0.05, curve=2.0):
    """Attack/release envelope over n samples."""
    e = np.ones(n)
    a, r = int(attack * RATE), int(release * RATE)
    if a > 0:
        e[:a] = np.linspace(0, 1, a)
    if r > 0:
        e[-r:] *= np.linspace(1, 0, r) ** curve
    return e


def expdecay(t, rate):
    return np.exp(-t * rate)


def sine(freq, t, phase=0.0):
    return np.sin(2 * np.pi * freq * t + phase)


def lowpass(x, cutoff):
    """One-pole low-pass."""
    a = math.exp(-2 * math.pi * cutoff / RATE)
    out = np.empty_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        out[i] = acc
    return out


def noise(n, seed=1):
    return np.random.default_rng(seed).uniform(-1, 1, n)


def save(path, samples, gain=0.8):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = max(1e-9, float(np.max(np.abs(samples))))
    data = np.clip(samples / peak * gain, -1, 1)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((data * 32767).astype("<i2").tobytes())


def jump():
    t = t_axis(0.16)
    freq = 260 + 520 * (t / t[-1])
    return np.sin(2 * np.pi * np.cumsum(freq) / RATE) * env(len(t), 0.004, 0.09) * 0.8


def land():
    t = t_axis(0.12)
    thump = sine(90 * expdecay(t, 6) + 45, t) * expdecay(t, 28)
    grit = lowpass(noise(len(t), 3), 1400) * expdecay(t, 40) * 0.6
    return (thump + grit) * env(len(t), 0.002, 0.04)


def step():
    t = t_axis(0.06)
    return lowpass(noise(len(t), 5), 2200) * expdecay(t, 60) * env(len(t), 0.001, 0.02)


def collect():
    parts = []
    for i, f in enumerate((880.0, 1174.7, 1568.0)):
        t = t_axis(0.16)
        tone = (sine(f, t) + 0.35 * sine(f * 2, t) + 0.15 * sine(f * 3, t)) * expdecay(t, 9)
        parts.append(np.pad(tone * env(len(t), 0.003, 0.05), (int(i * 0.055 * RATE), 0)))
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def death():
    t = t_axis(0.55)
    freq = 420 * np.exp(-t * 2.6) + 60
    saw = 2 * ((np.cumsum(freq) / RATE) % 1.0) - 1
    wob = 1 + 0.3 * sine(14, t)
    return lowpass(saw * wob, 1800) * env(len(t), 0.003, 0.3)


def respawn():
    t = t_axis(0.5)
    out = np.zeros(len(t))
    for i, f in enumerate((523.3, 659.3, 784.0, 1046.5)):
        start = int(i * 0.06 * RATE)
        tt = t[: len(t) - start]
        out[start:] += (sine(f, tt) + 0.3 * sine(f * 2, tt)) * expdecay(tt, 7) * 0.7
    return out * env(len(t), 0.003, 0.15)


def lever():
    t = t_axis(0.2)
    click = lowpass(noise(len(t), 8), 3500) * expdecay(t, 90)
    clunk = sine(150, t) * expdecay(t, 30) * 0.9
    late = np.pad(sine(110, t[: len(t) - 1500]) * expdecay(t[: len(t) - 1500], 35) * 0.7, (1500, 0))
    return (click + clunk + late) * env(len(t), 0.001, 0.05)


def plate_down():
    t = t_axis(0.12)
    return (sine(210, t) * expdecay(t, 30) + lowpass(noise(len(t), 11), 1800) * expdecay(t, 60) * 0.5) * env(len(t), 0.001, 0.03)


def plate_up():
    t = t_axis(0.1)
    return (sine(310, t) * expdecay(t, 40) * 0.8 + lowpass(noise(len(t), 12), 2500) * expdecay(t, 70) * 0.4) * env(len(t), 0.001, 0.03)


def gate_open():
    t = t_axis(0.8)
    rumble = lowpass(noise(len(t), 21), 240) * (0.6 + 0.4 * sine(9, t))
    grind = sine(70 + 25 * (t / t[-1]), t) * 0.5
    return (rumble + grind) * env(len(t), 0.08, 0.25)


def gate_close():
    t = t_axis(0.6)
    rumble = lowpass(noise(len(t), 22), 220) * (0.6 + 0.4 * sine(11, t))
    grind = sine(95 - 30 * (t / t[-1]), t) * 0.5
    slam = np.pad(sine(60, t[: len(t) - 12000]) * expdecay(t[: len(t) - 12000], 18), (12000, 0)) * 1.2
    return (rumble * 0.6 + grind * 0.6 + slam) * env(len(t), 0.05, 0.12)


def exit_reached():
    t = t_axis(0.4)
    out = np.zeros(len(t))
    for f in (392.0, 493.9, 587.3):
        out += sine(f, t) * 0.5
    return out * expdecay(t, 4) * env(len(t), 0.01, 0.12)


def checkpoint():
    t = t_axis(0.45)
    tone = sine(987.8, t) + 0.4 * sine(1975.6, t) + 0.6 * sine(1318.5, t) * (t > 0.09)
    return tone * expdecay(t, 6) * env(len(t), 0.003, 0.15)


def complete():
    notes = [(523.3, 0.0), (659.3, 0.14), (784.0, 0.28), (1046.5, 0.42)]
    total = 1.4
    out = np.zeros(int(total * RATE))
    for f, at in notes:
        t = t_axis(total - at - 0.05)
        tone = (sine(f, t) + 0.4 * sine(f * 2, t) + 0.2 * sine(f * 3, t)) * expdecay(t, 3.2)
        start = int(at * RATE)
        out[start:start + len(tone)] += tone * env(len(tone), 0.004, 0.2)
    return out


def fail():
    t = t_axis(0.5)
    return lowpass(2 * ((np.cumsum(180 * np.exp(-t * 1.5) + 70) / RATE) % 1.0) - 1, 900) * env(len(t), 0.004, 0.25)


def ambience():
    """A slow, low cave drone that loops seamlessly (every component repeats within 8 s)."""
    seconds = 8.0
    t = t_axis(seconds)
    base = 0.55 * sine(55, t) + 0.35 * sine(82.5, t + 0.3) + 0.2 * sine(110, t)
    swell = 0.6 + 0.4 * sine(1 / seconds * 2, t)
    hiss = lowpass(noise(len(t), 31), 500) * (0.25 + 0.15 * sine(1 / seconds, t))
    drip_track = np.zeros(len(t))
    for at, f in ((1.3, 1500), (4.9, 1250), (6.6, 1750)):
        start = int(at * RATE)
        tt = t_axis(0.25)
        drip = sine(f, tt) * expdecay(tt, 22) * 0.25
        end = min(len(t), start + len(drip))
        drip_track[start:end] += drip[: end - start]
    return base * swell * 0.5 + hiss + drip_track


def music():
    """A calm, slightly mysterious loop: a soft pad and a plucked pentatonic melody (16 bars)."""
    bpm = 84
    beat = 60.0 / bpm
    bars = 8
    total = bars * 4 * beat
    n = int(total * RATE)
    out = np.zeros(n)
    chords = [(220.0, 261.6, 329.6), (174.6, 220.0, 261.6), (196.0, 246.9, 293.7), (164.8, 207.7, 246.9)]
    for bar in range(bars):
        chord = chords[bar % 4]
        start = int(bar * 4 * beat * RATE)
        length = int(4 * beat * RATE)
        t = np.arange(length) / RATE
        pad = sum(sine(f, t) + 0.5 * sine(f * 2.005, t) for f in chord) / len(chord)
        pad = pad * (0.5 - 0.5 * np.cos(2 * np.pi * t / (4 * beat)))  # Swells in and out of each bar.
        out[start:start + length] += pad * 0.35
    scale = [440.0, 493.9, 587.3, 659.3, 784.0, 880.0]
    pattern = [0, 2, 4, 2, 3, 1, 2, 0, 4, 3, 2, 1, 0, 2, 1, 3]
    step_len = 2 * beat
    for i in range(int(total / step_len)):
        if (i * 7) % 5 == 4:
            continue  # A rest now and then.
        f = scale[pattern[i % len(pattern)]] * 0.5
        t = t_axis(min(1.6, total - i * step_len))
        pluck = (sine(f, t) + 0.35 * sine(f * 2, t) + 0.15 * sine(f * 3, t)) * expdecay(t, 3.2)
        start = int(i * step_len * RATE)
        end = min(n, start + len(pluck))
        out[start:end] += pluck[: end - start] * 0.28 * env(len(pluck), 0.004, 0.05)[: end - start]
    # Fade the last moments into the first so the loop point is smooth.
    fade = int(0.25 * RATE)
    out[:fade] *= np.linspace(0.0, 1.0, fade)
    out[-fade:] *= np.linspace(1.0, 0.0, fade)
    return out


SOUNDS = {
    "jump": (jump, 0.55), "land": (land, 0.6), "step": (step, 0.3), "collect": (collect, 0.6),
    "death": (death, 0.7), "respawn": (respawn, 0.55), "lever": (lever, 0.7), "plate_down": (plate_down, 0.6),
    "plate_up": (plate_up, 0.5), "gate_open": (gate_open, 0.6), "gate_close": (gate_close, 0.7),
    "exit": (exit_reached, 0.6), "checkpoint": (checkpoint, 0.55), "complete": (complete, 0.7),
    "fail": (fail, 0.6), "ambience": (ambience, 0.5), "music": (music, 0.55),
}


def generate(out_dir):
    d = os.path.join(out_dir, "audio")
    for name, (fn, gain) in SOUNDS.items():
        save(os.path.join(d, name + ".wav"), fn(), gain)


if __name__ == "__main__":
    import sys
    generate(sys.argv[1] if len(sys.argv) > 1 else "out")
