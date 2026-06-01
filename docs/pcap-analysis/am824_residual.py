#!/usr/bin/env python3
# Fit the 440 Hz fundamental in ch0 of an AM824 pcap, subtract it, and characterize
# the residual (SNR, periodic energy bursts => the audible "repetitive glitch").
import struct, sys, math, statistics

PATH = sys.argv[1]
blob = open(PATH, "rb").read()
magic = struct.unpack("<I", blob[:4])[0]
end = "<" if magic in (0xA1B2C3D4, 0xA1B23C4D) else ">"


def s24(b):
    v = (b[0] << 16) | (b[1] << 8) | b[2]
    return v - (1 << 24) if v & 0x800000 else v


off = 24
x = []
SR = 96000
while off + 16 <= len(blob):
    ts_s, ts_f, incl, orig = struct.unpack(end + "IIII", blob[off : off + 16])
    off += 16
    pkt = blob[off : off + incl]
    off += incl
    if len(pkt) < 18:
        continue
    eth = struct.unpack(">H", pkt[12:14])[0]
    a = 14
    if eth == 0x8100:
        eth = struct.unpack(">H", pkt[16:18])[0]
        a = 18
    if eth != 0x22F0:
        continue
    h = pkt[a:]
    if len(h) < 32 or h[0] != 0x00:
        continue
    sdl = struct.unpack(">H", h[20:22])[0]
    dbs = h[25]
    audio = h[32 : 32 + (sdl - 8)]
    p = 0
    for _ in range(len(audio) // (dbs * 4)):
        x.append(s24(audio[p + 1 : p + 4]) / 8388608.0)
        p += dbs * 4  # ch0 only
N = len(x)
print(f"ch0 samples={N} ({N / SR:.2f}s)")


# Resolve the fundamental FINELY. Over a multi-second record even a 0.05 Hz fit
# error spreads ~1 radian and fakes a coherence (SNR) loss, so a coarse Goertzel
# peak on a short segment is not enough. Coarse-scan to the bin, then refine
# against the FULL-signal least-squares amplitude (which peaks sharply at the
# true frequency) in two narrowing passes.
def goertzel(seg, f):
    w = 2 * math.pi * f / SR
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for v in seg:
        s0 = v + c * s1 - s2
        s2 = s1
        s1 = s0
    return s1 * s1 + s2 * s2 - c * s1 * s2


def ls_fit(f):
    w = 2 * math.pi * f / SR
    cc = ssq = cs = sx_c = sx_s = 0.0
    for i, v in enumerate(x):
        c = math.cos(w * i)
        s = math.sin(w * i)
        cc += c * c
        ssq += s * s
        cs += c * s
        sx_c += v * c
        sx_s += v * s
    det = cc * ssq - cs * cs
    A = (sx_c * ssq - sx_s * cs) / det
    B = (sx_s * cc - sx_c * cs) / det
    return A, B, math.hypot(A, B)


seg = x[:8192]
f = 430.0
best = (0, -1.0)
while f < 450:
    m = goertzel(seg, f)
    if m > best[1]:
        best = (f, m)
    f += 0.05
f0 = best[0]
for span, step in ((0.15, 0.005), (0.008, 0.0004)):  # full-signal LS refine
    bf = (f0, -1.0)
    f = f0 - span
    while f < f0 + span:
        _, _, amp = ls_fit(f)
        if amp > bf[1]:
            bf = (f, amp)
        f += step
    f0 = bf[0]
w = 2 * math.pi * f0 / SR
A, B, amp = ls_fit(f0)
print(
    f"fundamental     : {f0:.4f} Hz, amplitude {amp:.4f} ({20 * math.log10(amp):.1f} dBFS)"
)

# residual
res = [x[i] - (A * math.cos(w * i) + B * math.sin(w * i)) for i in range(N)]
sig_rms = math.sqrt(sum(v * v for v in x) / N)
res_rms = math.sqrt(sum(v * v for v in res) / N)
print(
    f"residual RMS    : {20 * math.log10(res_rms):.1f} dBFS   SNR(fund/residual)={20 * math.log10(amp / (res_rms * math.sqrt(2))):.1f} dB"
)

# windowed residual energy envelope (1 ms windows)
W = 96
env = []
for i in range(0, N - W, W):
    seg = res[i : i + W]
    env.append(max(abs(v) for v in seg))
me = statistics.median(env)
thr = me * 4
bursts = [i for i, v in enumerate(env) if v > thr]
# cluster bursts
clusters = []
for b in bursts:
    if clusters and b - clusters[-1][-1] <= 3:
        clusters[-1].append(b)
    else:
        clusters.append([b])
centers = [c[len(c) // 2] * W / SR for c in clusters]
print(
    f"residual env    : median={me:.4f} max={max(env):.4f}  burst windows>{thr:.3f}: {len(clusters)} clusters"
)
if len(centers) >= 2:
    gaps = [centers[i] - centers[i - 1] for i in range(1, len(centers))]
    print(
        f"burst spacing   : median={statistics.median(gaps) * 1000:.1f} ms -> {1 / statistics.median(gaps):.2f} /s"
    )
    print(f"  burst times(s): {[round(c, 3) for c in centers[:15]]}")
else:
    print("  (residual is broadband, no strong periodic bursts)")

# also: spectrum of residual to see harmonics vs noise
print("residual tones (Goertzel, top spurs):")
spurs = []
fr = 50.0
while fr < SR / 2:
    if abs(fr - f0) > 5:
        m = math.sqrt(max(goertzel(res[:16384], fr), 0)) * 2 / 16384
        spurs.append((m, fr))
    fr *= 2 ** (1 / 24)
spurs.sort(reverse=True)
for m, fr in spurs[:8]:
    print(f"   {fr:8.1f} Hz  {20 * math.log10(m) if m > 0 else -999:6.1f} dBFS")
