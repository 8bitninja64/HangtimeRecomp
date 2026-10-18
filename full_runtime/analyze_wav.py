"""Objective checks of a `hangtime.exe --wav` capture (the exact stream handed to SDL).

Per one-second window: RMS level, peak, clipping, and a roughness ratio
sum((x[n]-x[n-1])^2) / sum(x^2). Natural audio at 22 kHz is dominated by low frequencies
(ratio well below 1); white noise gives ~2, and wrongly ordered bytes or words look like noise.
This does not replace listening; it rejects silence, garbage and gross format errors.
Usage: analyze_wav.py capture.wav [--min-audible SECONDS]
"""
import math, struct, sys, wave

path = sys.argv[1]
min_audible = float(sys.argv[sys.argv.index('--min-audible') + 1]) if '--min-audible' in sys.argv else 5.0
with wave.open(path, 'rb') as w:
    rate, channels, width, frames = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(frames)
assert channels == 2 and width == 2, (channels, width)
samples = struct.unpack(f'<{len(raw) // 2}h', raw)
left, right = samples[0::2], samples[1::2]
print(f'{path}: {rate} Hz stereo, {frames} frames = {frames / rate:.1f} s')

audible = clipped = 0
rough = []
for start in range(0, frames - rate + 1, rate):
    l = left[start:start + rate]; r = right[start:start + rate]
    energy = sum(x * x for x in l) + sum(x * x for x in r)
    rms = math.sqrt(energy / (2 * rate))
    peak = max(max(map(abs, l)), max(map(abs, r)))
    clipped += sum(1 for x in l if abs(x) >= 32767) + sum(1 for x in r if abs(x) >= 32767)
    if rms >= 100:   # about -50 dBFS
        audible += 1
        diff = sum((l[i] - l[i - 1]) ** 2 + (r[i] - r[i - 1]) ** 2 for i in range(1, rate))
        rough.append(diff / energy)
    sec = start // rate
    if sec % 10 == 0:
        db = 20 * math.log10(rms / 32768) if rms else -120
        print(f'  t={sec:4d}s rms={db:6.1f} dBFS peak={peak:5d} roughness={(rough[-1] if rms >= 100 else 0):.3f}')
windows = frames // rate
print(f'audible seconds: {audible}/{windows}; clipped samples: {clipped}; '
      f'roughness median {sorted(rough)[len(rough) // 2] if rough else 0:.3f} max {max(rough) if rough else 0:.3f}')
assert rate == 22047, f'unexpected DAC rate {rate}'
assert audible >= min_audible, 'audio is (nearly) silent'
assert clipped <= frames * 2 // 1000, 'more than 0.1% clipped samples'
assert sorted(rough)[len(rough) // 2] < 0.5, 'audio is noise-like (wrong sample format or garbage)'
print('PASS: audio stream is audible, unclipped and has natural (non-noise) spectral balance')
