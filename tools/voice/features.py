"""Version 1: 2 s PCM -> 32 temporal means x 32 log-mel bands (float32).

No speech segmentation: the training input is one prompted word per capture.
These exact transforms must be ported and compared before deploying on ESP32.
"""
import numpy as np
from dataset import RATE, SAMPLES

SPEC = {"version": 1, "sample_rate": RATE, "samples": SAMPLES,
        "window": 400, "hop": 160, "fft": 512, "mel_bins": 32,
        "temporal_bins": 32, "low_hz": 80, "high_hz": 7600,
        "log_floor": 1e-10, "window_type": "numpy.hanning-symmetric",
        "power": "abs-rfft-squared/512", "order": "time-major"}
DIM = 1024


def mel_filters():
    def mel(hz):
        return 2595 * np.log10(1 + hz / 700)
    points = 700 * (10 ** (np.linspace(mel(80), mel(7600), 34) / 2595) - 1)
    frequencies = np.fft.rfftfreq(512, 1 / RATE)
    filters = []
    for left, center, right in zip(points[:-2], points[1:-1], points[2:]):
        filters.append(np.maximum(0, np.minimum((frequencies - left) / (center - left),
                                               (right - frequencies) / (right - center))))
    return np.asarray(filters, dtype=np.float32)


def extract(pcm):
    signal = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768
    if signal.shape != (SAMPLES,):
        raise ValueError("Expected exactly 2 s of mono PCM16")
    frames = np.lib.stride_tricks.sliding_window_view(signal, 400)[::160]
    spectrum = np.fft.rfft(frames * np.hanning(400), n=512)
    power = np.abs(spectrum) ** 2 / 512
    logmel = np.log(np.maximum(power @ mel_filters().T, 1e-10))
    boundaries = np.linspace(0, len(logmel), 33, dtype=int)
    return np.stack([logmel[a:b].mean(axis=0)
                     for a, b in zip(boundaries[:-1], boundaries[1:])]).astype(np.float32)
