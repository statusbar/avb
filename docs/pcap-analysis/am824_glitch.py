#!/usr/bin/env python3
# Extract ch0/ch1 from an AM824 pcap IN ORDER and hunt for periodic glitches
# (sample discontinuities in what should be a pure 440 Hz sine).
import struct, sys, math

PATH = sys.argv[1]
blob = open(PATH, "rb").read()
magic = struct.unpack("<I", blob[:4])[0]
end = "<" if magic in (0xA1B2C3D4, 0xA1B23C4D) else ">"
nano = magic in (0xA1B23C4D, 0x4D3CB2A1)


def s24(b):
    v = (b[0] << 16) | (b[1] << 8) | b[2]
    return v - (1 << 24) if v & 0x800000 else v


off = 24
ch = [[], []]
seqs = []
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
    seqs.append(h[2])
    sdl = struct.unpack(">H", h[20:22])[0]
    dbs = h[25]
    audio = h[32 : 32 + (sdl - 8)]
    nblk = len(audio) // (dbs * 4)
    p = 0
    for _ in range(nblk):
        for c in range(dbs):
            v = s24(audio[p + 1 : p + 4])
            p += 4
            if c < 2:
                ch[c].append(v / 8388608.0)

# sequence discontinuities (packet-level drops)
seqgaps = [i for i in range(1, len(seqs)) if (seqs[i - 1] + 1) & 0xFF != seqs[i]]
print(f"packets={len(seqs)} seq_gaps={len(seqgaps)} at pkt idx {seqgaps[:10]}")

x = ch[0]
N = len(x)
print(f"ch0 samples={N} ({N / SR:.2f}s)")
# A 440 Hz sine @96k: |x[i]-x[i-1]| <= A*2*pi*f/SR ~ 0.0288*A. Second difference
# (curvature) is tiny except at a real discontinuity. Flag big 2nd-diff outliers.
import statistics

d2 = [abs(x[i] - 2 * x[i - 1] + x[i - 2]) for i in range(2, N)]
med = statistics.median(d2)
mx = max(d2)
thr = max(0.02, med * 40)
glitch = [i + 2 for i, v in enumerate(d2) if v > thr]
# collapse adjacent indices (a click spans a few samples)
clusters = []
for g in glitch:
    if clusters and g - clusters[-1][-1] <= 8:
        clusters[-1].append(g)
    else:
        clusters.append([g])
centers = [c[len(c) // 2] for c in clusters]
print(f"2nd-diff median={med:.2e} max={mx:.2e} thr={thr:.2e}")
print(f"glitch clusters={len(clusters)} over {N / SR:.2f}s")
if len(centers) >= 2:
    gaps = [(centers[i] - centers[i - 1]) / SR for i in range(1, len(centers))]
    gaps_s = sorted(gaps)
    print(
        f"glitch spacing: min={min(gaps):.4f}s max={max(gaps):.4f}s median={statistics.median(gaps):.4f}s"
    )
    print(
        f"  -> ~{1 / statistics.median(gaps):.2f} glitches/sec  (period {statistics.median(gaps) * 1000:.1f} ms)"
    )
    print(f"  first 12 glitch times (s): {[round(c / SR, 4) for c in centers[:12]]}")
    # magnitude of a representative glitch (jump size)
    g = centers[len(centers) // 2]
    print(
        f"  sample jump at t={g / SR:.4f}s: x[{g - 1}]={x[g - 1]:+.4f} x[{g}]={x[g]:+.4f} x[{g + 1}]={x[g + 1]:+.4f}"
    )
elif centers:
    print(f"single glitch at t={centers[0] / SR:.4f}s")
else:
    print("no sample-level glitches detected above threshold")
