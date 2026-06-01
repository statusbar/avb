#!/usr/bin/env python3
# Parse a pcap of VLAN-tagged AAF (AVTP audio) frames and characterize the audio.
import struct, sys, math

PATH = sys.argv[1]
MAXSAMP = int(sys.argv[2]) if len(sys.argv) > 2 else 480000  # per-channel cap

with open(PATH, "rb") as f:
    blob = f.read()

magic = struct.unpack("<I", blob[:4])[0]
if magic in (0xA1B2C3D4, 0xA1B23C4D):
    end = "<"
elif magic in (0xD4C3B2A1, 0x4D3CB2A1):
    end = ">"
else:
    raise SystemExit(f"not a pcap (magic {magic:#x})")
nano = magic in (0xA1B23C4D, 0x4D3CB2A1)

off = 24  # global header
chans = None
fmt_byte = nsr = bit_depth = None
stream_id = None
samples = None  # list of per-channel lists (normalized -1..1)
raw_min = raw_max = 0
npkt = 0
seqs = []
sdl_set = set()
first_ts = last_ts = None


def be_i32(b):
    v = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]
    return v - (1 << 32) if v & 0x80000000 else v


while off + 16 <= len(blob):
    ts_s, ts_f, incl, orig = struct.unpack(end + "IIII", blob[off : off + 16])
    off += 16
    pkt = blob[off : off + incl]
    off += incl
    if len(pkt) < 14:
        continue
    eth = struct.unpack(">H", pkt[12:14])[0]
    a = 14
    if eth == 0x8100:
        eth = struct.unpack(">H", pkt[16:18])[0]
        a = 18
    if eth != 0x22F0:
        continue
    h = pkt[a:]
    if len(h) < 24 or h[0] != 0x02:  # AAF subtype
        continue
    npkt += 1
    seqs.append(h[2])
    if stream_id is None:
        stream_id = h[4:12]
        fmt_byte = h[16]
        b17 = h[17]
        nsr = b17 >> 4
        chans = ((b17 & 0x03) << 8) | h[18]
        bit_depth = h[19]
        samples = [[] for _ in range(chans)]
    sdl = struct.unpack(">H", h[20:22])[0]
    sdl_set.add(sdl)
    ts = ts_s + ts_f * (1e-9 if nano else 1e-6)
    if first_ts is None:
        first_ts = ts
    last_ts = ts
    payload = h[24 : 24 + sdl]
    if chans == 0:
        continue
    bytes_per = 4
    nframe = len(payload) // (chans * bytes_per)
    if sum(len(c) for c in samples) // max(chans, 1) >= MAXSAMP:
        continue
    p = 0
    for _ in range(nframe):
        for c in range(chans):
            v = be_i32(payload[p : p + 4])
            p += 4
            if v < raw_min:
                raw_min = v
            if v > raw_max:
                raw_max = v
            samples[c].append(v / 2147483648.0)

# --- report ---
SR = {
    1: 8000,
    2: 16000,
    3: 32000,
    4: 44100,
    5: 48000,
    6: 88200,
    7: 96000,
    8: 176400,
    9: 192000,
    10: 24000,
}.get(nsr, nsr)
FMT = {0: "user", 1: "float32", 2: "int32", 3: "int24", 4: "int16", 5: "aes3"}.get(
    fmt_byte, "?"
)
dur = (last_ts - first_ts) if first_ts else 0
print(f"packets         : {npkt}")
print(f"stream_id       : {stream_id.hex(':') if stream_id else '-'}")
print(f"format          : {FMT} (0x{fmt_byte:02x}), bit_depth={bit_depth}")
print(f"sample rate     : {SR} Hz (nsr={nsr})")
print(f"channels        : {chans}")
print(
    f"stream_data_len : {sorted(sdl_set)} octets  -> {sorted(sdl_set)[0] // (chans * 4) if chans else 0} frames/pkt"
)
print(f"capture span    : {dur:.3f} s  ({npkt / dur:.0f} pkt/s)" if dur else "")
# sequence-number gaps
gaps = 0
for i in range(1, len(seqs)):
    if (seqs[i - 1] + 1) & 0xFF != seqs[i]:
        gaps += 1
print(f"seq gaps        : {gaps} (of {len(seqs)} pkts)")
print(
    f"raw int32 range : [{raw_min}, {raw_max}]  (|max|/2^31 = {max(abs(raw_min), abs(raw_max)) / 2147483648.0:.4f})"
)
n = len(samples[0]) if samples and samples[0] else 0
print(f"analyzed/chan   : {n} samples ({n / SR:.2f} s)\n")


def goertzel(x, f, sr):
    w = 2 * math.pi * f / sr
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2 = s1
        s1 = s0
    p = s1 * s1 + s2 * s2 - c * s1 * s2
    return math.sqrt(max(p, 0.0)) * 2.0 / len(x)


# coarse 1/12-octave scan 20Hz..Nyquist for the dominant tone per channel
def dom_freq(x, sr):
    if not x:
        return (0, 0)
    N = min(len(x), 8192)
    seg = x[:N]
    best = (0, 0)
    f = 20.0
    nyq = sr / 2
    while f < nyq:
        m = goertzel(seg, f, sr)
        if m > best[1]:
            best = (f, m)
        f *= 2 ** (1 / 12)
    # refine around best
    f0 = best[0]
    bf = best
    f = f0 * 0.92
    while f < f0 * 1.09:
        m = goertzel(seg, f, sr)
        if m > bf[1]:
            bf = (f, m)
        f *= 1.005
    return bf


print(
    f"{'ch':>2} {'peak dBFS':>10} {'rms dBFS':>9} {'DC':>9} {'domFreq Hz':>11} {'tone dBFS':>10}"
)
for c in range(chans):
    x = samples[c]
    if not x:
        continue
    pk = max(abs(min(x)), abs(max(x)))
    rms = math.sqrt(sum(v * v for v in x) / len(x))
    dc = sum(x) / len(x)
    f, mag = dom_freq(x, SR)
    pk_db = 20 * math.log10(pk) if pk > 0 else -999
    rms_db = 20 * math.log10(rms) if rms > 0 else -999
    mag_db = 20 * math.log10(mag) if mag > 0 else -999
    print(f"{c:>2} {pk_db:>10.1f} {rms_db:>9.1f} {dc:>9.5f} {f:>11.1f} {mag_db:>10.1f}")
