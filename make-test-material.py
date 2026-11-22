"""生成 Bessel 的验证素材。

每个文件对应一种跟踪情形，用法见 test-material/README.md。
采样率 48 kHz，16 bit，双声道同相（mono 内容，任何宿主都吃得下）。
"""

import math
import os
import struct
import wave

import numpy as np

SR = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test-material")
os.makedirs(OUT, exist_ok=True)


def write_wav(name: str, signal: np.ndarray, peak_db: float = -6.0) -> None:
    """归一化到指定峰值，写成 16 bit 立体声。"""
    peak = float(np.max(np.abs(signal))) or 1.0
    target = 10.0 ** (peak_db / 20.0)
    data = signal / peak * target
    pcm = np.clip(data * 32767.0, -32768, 32767).astype("<i2")

    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        interleaved = np.empty(pcm.size * 2, dtype="<i2")
        interleaved[0::2] = pcm
        interleaved[1::2] = pcm
        w.writeframes(interleaved.tobytes())

    seconds = len(signal) / SR
    print(f"  {name:32s} {seconds:4.1f}s  peak {peak_db:+.0f} dBFS  "
          f"max |x| = {np.max(np.abs(data)):.3f}")


def fade(x: np.ndarray, ms: float = 10.0) -> np.ndarray:
    """首尾淡入淡出，避免播放时的爆音被误认为插件问题。"""
    n = int(SR * ms / 1000.0)
    ramp = np.linspace(0.0, 1.0, n)
    x = x.copy()
    x[:n] *= ramp
    x[-n:] *= ramp[::-1]
    return x


def tone(hz: float, seconds: float, amp: float = 1.0) -> np.ndarray:
    t = np.arange(int(SR * seconds)) / SR
    return amp * np.sin(2.0 * math.pi * hz * t)


def harmonic_tone(hz: float, seconds: float, count: int = 6) -> np.ndarray:
    t = np.arange(int(SR * seconds)) / SR
    out = np.zeros_like(t)
    for h in range(1, count + 1):
        out += np.sin(2.0 * math.pi * hz * h * t) / h
    return out


def glide(start_hz: float, end_hz: float, seconds: float) -> np.ndarray:
    """指数滑音（等音程），相位用累加保证连续。"""
    n = int(SR * seconds)
    freqs = start_hz * (end_hz / start_hz) ** (np.arange(n) / n)
    phase = np.cumsum(2.0 * math.pi * freqs / SR)
    return np.sin(phase)


def noise(seconds: float, seed: int = 7) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return rng.standard_normal(int(SR * seconds))


def chord(root: float, seconds: float) -> np.ndarray:
    t = np.arange(int(SR * seconds)) / SR
    return (np.sin(2.0 * math.pi * root * t)
            + np.sin(2.0 * math.pi * root * 1.259921 * t))


print(f"写入 {OUT}\n")

# 1) 基准：220 Hz 纯音，跟踪应该稳定在 220
write_wav("01-220hz-sine.wav", fade(tone(220.0, 2.0)))

# 2) 低音：110 Hz
write_wav("02-110hz-sine.wav", fade(tone(110.0, 2.0)))

# 3) 下限：55 Hz（跟踪范围的边上）
write_wav("03-55hz-sine.wav", fade(tone(55.0, 2.0)))

# 4) 八度陷阱：基频 220 + 6 次谐波。
#    过零率法会数成 440 甚至 660，YIN 必须给 220。
write_wav("04-harmonic-220.wav", fade(harmonic_tone(220.0, 2.0)))

# 5) 滑音：110 → 220，3 秒一个八度。听"滑过去"而不是"跳过去"。
write_wav("05-glide-110-to-220.wav", fade(glide(110.0, 220.0, 3.0)))

# 6) 无音高：白噪声。跟踪应该丢失，载波滑回 Carrier 旋钮值。
write_wav("06-noise.wav", fade(noise(2.0)))

# 7) 复音：A3 + C#4 两个音同时。单声道跟踪器会犹豫，这是已知限制。
write_wav("07-chord-two-notes.wav", fade(chord(220.0, 2.0)))

print("\n完成。")
