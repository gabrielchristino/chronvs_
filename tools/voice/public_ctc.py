"""Variable-duration features and causal character CTC, PC research only."""
import io
import wave

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from features import mel_filters
from phonetic_ctc import CausalConv

SPEC = {"rate": 16000, "window": 400, "hop": 160, "fft": 512,
        "bands": 32, "duration": "full utterance, variable",
        "normalization": "training-valid-frames-per-band", "output": "characters"}


def wav_features(raw):
    with wave.open(io.BytesIO(raw)) as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError("Expected PCM16 mono 16 kHz")
        pcm = wav.readframes(wav.getnframes())
    signal = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768
    if len(signal) < 400:
        raise ValueError("Audio shorter than one feature window")
    frames = np.lib.stride_tricks.sliding_window_view(signal, 400)[::160]
    spectrum = np.fft.rfft(frames * np.hanning(400), n=512)
    return np.log(np.maximum((np.abs(spectrum) ** 2 / 512) @ mel_filters().T, 1e-10)).astype(np.float32)


def required_ctc_frames(target):
    return len(target) + sum(a == b for a, b in zip(target, target[1:]))


def pack_batch(items):
    features, targets = zip(*items)
    lengths = torch.tensor([len(item) for item in features], dtype=torch.long)
    target_lengths = torch.tensor([len(item) for item in targets], dtype=torch.long)
    if any(required_ctc_frames(y) > len(x) for x, y in items):
        raise ValueError("CTC target cannot align to this complete utterance")
    padded = torch.zeros(len(items), int(lengths.max()), 32)
    for i, x in enumerate(features):
        padded[i, :len(x)] = torch.as_tensor(x)
    return padded, torch.tensor([v for y in targets for v in y], dtype=torch.long), lengths, target_lengths


class CompactCharacterCTC(nn.Module):
    """257-frame (~2.585s) receptive field; no stride or future context.

    Weight size alone is not a device-memory or latency guarantee.
    """
    def __init__(self, classes, hidden=64):
        super().__init__()
        self.stem = CausalConv(32, hidden)
        self.blocks = nn.ModuleList([
            nn.Sequential(CausalConv(hidden, hidden, dilation=d, groups=hidden),
                          nn.ReLU(), nn.Conv1d(hidden, hidden, 1), nn.ReLU())
            for d in (1, 2, 4, 8, 16, 32)])
        self.head = nn.Conv1d(hidden, classes, 1)

    def forward(self, x):
        x = F.relu(self.stem(x.transpose(1, 2)))
        for block in self.blocks:
            x = x + block(x)
        return self.head(x).transpose(1, 2)
