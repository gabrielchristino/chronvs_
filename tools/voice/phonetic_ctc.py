"""CPU research prototype: causal acoustic network and exact-word CTC scoring.

No serial, firmware, word acceptance threshold or note actions. Generated IPA
labels are hypotheses; this code does not verify a speaker's pronunciation.
"""
import math
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from dataset import RATE, SAMPLES
from features import mel_filters

SEQUENCE_SPEC = {"version": 1, "rate": RATE, "samples": SAMPLES, "window": 400,
                 "hop": 160, "fft": 512, "bands": 32, "low_hz": 80, "high_hz": 7600,
                 "window_type": "numpy.hanning-symmetric", "log_floor": 1e-10,
                 "temporal_pooling": False, "normalization": "train-only-per-band"}
GAIN_NORMALIZED_SPEC = {**SEQUENCE_SPEC, "version": 2,
                        "frame_gain_normalization": "subtract-per-frame-logmel-mean"}


def sequence_features(pcm, gain_normalize=False):
    signal = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768
    if signal.shape != (SAMPLES,):
        raise ValueError("Expected 2 s of mono PCM16")
    frames = np.lib.stride_tricks.sliding_window_view(signal, 400)[::160]
    fft = np.fft.rfft(frames * np.hanning(400), n=512)
    logmel = np.log(np.maximum((np.abs(fft) ** 2 / 512) @ mel_filters().T, 1e-10))
    if gain_normalize:
        logmel -= logmel.mean(axis=1, keepdims=True)
    return logmel.astype(np.float32)


class CausalConv(nn.Module):
    def __init__(self, inputs, outputs, kernel=5, dilation=1, groups=1):
        super().__init__()
        self.left = (kernel - 1) * dilation
        self.conv = nn.Conv1d(inputs, outputs, kernel, dilation=dilation, groups=groups)

    def forward(self, x):
        return self.conv(F.pad(x, (self.left, 0)))


class SmallPhoneticCTC(nn.Module):
    """29-frame receptive field, fixed work per frame; unvalidated on ESP32."""
    def __init__(self, classes, hidden=64):
        super().__init__()
        self.stem = CausalConv(32, hidden)
        self.blocks = nn.ModuleList([
            nn.Sequential(CausalConv(hidden, hidden, dilation=d, groups=hidden),
                          nn.ReLU(), nn.Conv1d(hidden, hidden, 1), nn.ReLU())
            for d in (2, 4)])
        self.head = nn.Conv1d(hidden, classes, 1)

    def forward(self, frames):
        x = F.relu(self.stem(frames.transpose(1, 2)))
        for block in self.blocks:
            x = block(x)
        return self.head(x).transpose(1, 2)


def greedy_ctc(probabilities, blank=0):
    result, previous = [], blank
    for token in np.argmax(probabilities, axis=1):
        token = int(token)
        if token != blank and token != previous:
            result.append(token)
        previous = token
    return result


def ctc_log_score(log_probabilities, target, blank=0):
    """Sum all CTC alignments in log space; repeated units need a blank."""
    p = np.asarray(log_probabilities, dtype=np.float64)
    if p.ndim != 2 or np.isnan(p).any() or np.isposinf(p).any():
        raise ValueError("Expected finite/-inf [time, classes] log probabilities")
    if any(token == blank or token < 0 or token >= p.shape[1] for token in target):
        raise ValueError("Invalid CTC target token")
    if not len(p):
        return 0.0 if not target else -math.inf
    if not target:
        return float(p[:, blank].sum())
    extended = np.full(2 * len(target) + 1, blank, dtype=int)
    extended[1::2] = target
    alpha = np.full(len(extended), -math.inf)
    alpha[0] = p[0, blank]
    alpha[1] = p[0, target[0]]
    skip = np.zeros(len(extended), dtype=bool)
    skip[2:] = (extended[2:] != blank) & (extended[2:] != extended[:-2])
    for frame in p[1:]:
        step = np.full_like(alpha, -math.inf)
        step[1:] = alpha[:-1]
        jump = np.full_like(alpha, -math.inf)
        jump[2:] = alpha[:-2]
        jump[~skip] = -math.inf
        alpha = np.logaddexp(np.logaddexp(alpha, step), jump) + frame[extended]
    return float(np.logaddexp(alpha[-1], alpha[-2]))


def edit_distance(reference, hypothesis):
    previous = list(range(len(hypothesis) + 1))
    for i, expected in enumerate(reference, 1):
        current = [i]
        for j, actual in enumerate(hypothesis, 1):
            current.append(min(current[-1] + 1, previous[j] + 1,
                               previous[j - 1] + (expected != actual)))
        previous = current
    return previous[-1]


def rank_words(log_probabilities, entries, tokens, limit=5):
    """Rank lexicon words, preserving equal scores for homophones.

    These scores are not calibrated acceptance probabilities. Noise will also
    have a best-ranked word; never automatically turn it into a note.
    """
    indices = {token: i for i, token in enumerate(tokens)}
    scores, seen = {}, set()
    for entry in entries:
        target = tuple(indices[unit] for unit in entry["units"])
        identity = (entry["text"], target)
        if identity in seen:
            continue
        seen.add(identity)
        value = ctc_log_score(log_probabilities, target)
        scores[entry["text"]] = float(np.logaddexp(scores.get(entry["text"], -math.inf), value))
    return sorted(scores.items(), key=lambda item: (-item[1], item[0]))[:limit]
