#!/usr/bin/env python3
# Parse a pcap of (VLAN-tagged) IEC 61883-6 AM824 AVTP frames and characterize audio.
import struct, sys, math

PATH = sys.argv[1]
MAXSAMP = int(sys.argv[2]) if len(sys.argv) > 2 else 480000
blob = open(PATH, "rb").read()
magic = struct.unpack("<I", blob[:4])[0]
end = "<" if magic in (0xA1B2C3D4, 0xA1B23C4D) else ">"
nano = magic in (0xA1B23C4D, 0x4D3CB2A1)

off = 24
chans = None
stream_id = None
samples = None
raw_min = raw_max = 0
npkt = 0
seqs = []
sdl_set = set()
labels = set()
fdf_set = set()
syt_valid = 0
syt_invalid = 0
first_ts = last_ts = None
total_blocks = 0


def s24(b):  # signed 24-bit big-endian
    v = (b[0] << 16) | (b[1] << 8) | b[2]
    return v - (1 << 24) if v & 0x800000 else v


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
        continue  # 61883/IIDC subtype
    npkt += 1
    seqs.append(h[2])
    sdl = struct.unpack(">H", h[20:22])[0]
    sdl_set.add(sdl)
    dbs = h[25]  # data block size = channels (quadlets/block)
    fdf = h[29]
    fdf_set.add(fdf)
    syt = struct.unpack(">H", h[30:32])[0]
    if syt == 0xFFFF:
        syt_invalid += 1
    else:
        syt_valid += 1
    if stream_id is None:
        stream_id = h[4:12]
        chans = dbs
        samples = [[] for _ in range(chans)]
    ts = ts_s + ts_f * (1e-9 if nano else 1e-6)
    if first_ts is None:
        first_ts = ts
    last_ts = ts
    audio = h[32 : 32 + (sdl - 8)]  # sdl covers CIP(8)+data
    if dbs == 0:
        continue
    nblk = len(audio) // (dbs * 4)
    total_blocks += nblk
    if sum(len(c) for c in samples) // max(chans, 1) >= MAXSAMP:
        continue
    p = 0
    for _ in range(nblk):
        for c in range(dbs):
            label = audio[p]
            labels.add(label)
            v = s24(audio[p + 1 : p + 4])
            p += 4
            if v < raw_min:
                raw_min = v
            if v > raw_max:
                raw_max = v
            if c < chans:
                samples[c].append(v / 8388608.0)

dur = (last_ts - first_ts) if first_ts else 0
pps = npkt / dur if dur else 0
spp = total_blocks / npkt if npkt else 0
sr = round(pps * spp)
print(f"packets         : {npkt}")
print(f"stream_id       : {stream_id.hex(':') if stream_id else '-'}")
print(f"channels (DBS)  : {chans}")
print(f"AM824 labels    : {sorted(hex(x) for x in labels)}  (0x40=MBLA 24-bit)")
print(f"FDF             : {sorted(hex(x) for x in fdf_set)}")
print(f"stream_data_len : {sorted(sdl_set)} octets  -> {spp:.1f} samples/pkt")
print(
    f"SYT             : valid={syt_valid} invalid(0xFFFF)={syt_invalid}  ({'media clock present' if syt_valid > syt_invalid else 'NO media clock!'})"
)
print(f"capture span    : {dur:.3f}s  {pps:.0f} pkt/s  -> inferred {sr} Hz")
gaps = sum(1 for i in range(1, len(seqs)) if (seqs[i - 1] + 1) & 0xFF != seqs[i])
print(f"seq gaps        : {gaps} of {len(seqs)}")
print(
    f"raw s24 range   : [{raw_min}, {raw_max}]  (|max|/2^23 = {max(abs(raw_min), abs(raw_max)) / 8388608.0:.4f})"
)
n = len(samples[0]) if samples and samples[0] else 0
print(f"analyzed/chan   : {n} samples ({n / sr:.2f}s)\n")


def goertzel(x, f, fs):
    w = 2 * math.pi * f / fs
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2 = s1
        s1 = s0
    return math.sqrt(max(s1 * s1 + s2 * s2 - c * s1 * s2, 0.0)) * 2.0 / len(x)


def dom(x, fs):
    if not x:
        return (0, 0)
    seg = x[: min(len(x), 8192)]
    best = (0, 0)
    f = 20.0
    while f < fs / 2:
        m = goertzel(seg, f, fs)
        if m > best[1]:
            best = (f, m)
        f *= 2 ** (1 / 12)
    f0 = best[0]
    bf = best
    f = f0 * 0.92
    while f < f0 * 1.09:
        m = goertzel(seg, f, fs)
        if m > bf[1]:
            bf = (f, m)
        f *= 1.004
    return bf


print(
    f"{'ch':>2} {'peak dBFS':>10} {'rms dBFS':>9} {'domFreq Hz':>11} {'tone dBFS':>10}"
)
for c in range(chans):
    x = samples[c]
    if not x:
        continue
    pk = max(abs(min(x)), abs(max(x)))
    rms = math.sqrt(sum(v * v for v in x) / len(x))
    f, mag = dom(x, sr)
    db = lambda v: 20 * math.log10(v) if v > 0 else -999
    print(f"{c:>2} {db(pk):>10.1f} {db(rms):>9.1f} {f:>11.1f} {db(mag):>10.1f}")
