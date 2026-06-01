#!/usr/bin/env python3
# Decode IEEE 1722 CRF (Clock Reference Format, subtype 0x04) packets from a pcap
# and verify the media clock: header fields + evenly-spaced, monotonic, GPS-rate
# timestamps. Handles untagged and single-VLAN-tagged frames.
import struct, sys, statistics

PATH = sys.argv[1]
blob = open(PATH, "rb").read()
magic = struct.unpack("<I", blob[:4])[0]
end = "<" if magic in (0xA1B2C3D4, 0xA1B23C4D) else ">"

off = 24
npkt = 0
seqs = []
ts_all = []  # every timestamp in order
within_deltas = []  # deltas between consecutive timestamps inside a packet
hdr = None
ndata = set()
while off + 16 <= len(blob):
    s, f, incl, orig = struct.unpack(end + "IIII", blob[off : off + 16])
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
    if len(h) < 20 or h[0] != 0x04:  # CRF subtype
        continue
    npkt += 1
    seq = h[2]
    ctype = h[3]
    sid = h[4:12]
    pbf = struct.unpack(">I", h[12:16])[0]
    base_freq = pbf & 0x1FFFFFFF
    pull = (pbf >> 29) & 0x7
    crf_len = struct.unpack(">H", h[16:18])[0]
    ts_interval = struct.unpack(">H", h[18:20])[0]
    n_ts = crf_len // 8
    ndata.add(n_ts)
    if hdr is None:
        hdr = (sid, ctype, base_freq, pull, ts_interval)
    seqs.append(seq)
    pkt_ts = []
    for i in range(n_ts):
        v = struct.unpack(">Q", h[20 + i * 8 : 28 + i * 8])[0]
        pkt_ts.append(v)
    for i in range(1, len(pkt_ts)):
        within_deltas.append(pkt_ts[i] - pkt_ts[i - 1])
    ts_all.extend(pkt_ts)

if npkt == 0:
    print("no CRF packets found")
    sys.exit(0)
sid, ctype, base_freq, pull, ts_interval = hdr
CTYPE = {
    0: "user",
    1: "audio_sample",
    2: "video_frame",
    3: "video_line",
    4: "machine_cycle",
}.get(ctype, ctype)
PULL = {0: "x1.0", 1: "x1/1.001", 2: "x1.001", 3: "x24/25", 4: "x25/24", 5: "x1/8"}.get(
    pull, pull
)
print(f"CRF packets     : {npkt}")
print(f"stream_id       : {sid.hex(':')}")
print(f"type            : {CTYPE}   pull {PULL}   base_frequency {base_freq} Hz")
print(
    f"timestamp_interval={ts_interval}  timestamps/pkt={sorted(ndata)}  -> events/pkt={ts_interval * (sorted(ndata)[0])}"
)
gaps = sum(1 for i in range(1, len(seqs)) if (seqs[i - 1] + 1) & 0xFF != seqs[i])
print(f"seq gaps        : {gaps} of {len(seqs)}")

# Expected timestamp spacing = (1e9/base_freq) * interval. The talker stamps in
# the gPTP timebase, so for a GPS-locked media clock the REAL spacing is
# nominal * r (r = local-gPTP/GPS); the media rate expressed in gPTP time is
# base_freq / r, which shows as a small ppm offset.
nominal = (1e9 / base_freq) * ts_interval if base_freq else 0
mono = all(
    ((ts_all[i] - ts_all[i - 1]) & 0xFFFFFFFFFFFFFFFF) < (1 << 63)
    for i in range(1, len(ts_all))
)
print(f"timestamps      : {len(ts_all)}  monotonic={mono}")
if within_deltas:
    # Within-packet spacing is gap-robust (a dropped CRF AVTPDU only perturbs a
    # cross-packet delta, never a within-packet one). Use it for the rate.
    med = statistics.median(within_deltas)
    print(
        f"within-pkt spacing: median={med:.0f} ns  (nominal {nominal:.1f} ns)  "
        f"min={min(within_deltas)} max={max(within_deltas)} stdev={statistics.pstdev(within_deltas):.1f}"
    )
    per_event = med / ts_interval
    eff_rate = 1e9 / per_event if per_event else 0
    ppm = (eff_rate / base_freq - 1) * 1e6 if base_freq else 0
    print(
        f"media rate (gPTP): {eff_rate:.3f} Hz  ({ppm:+.1f} ppm vs base)  -- the GPS clock seen in gPTP time"
    )
print(f"first 4 timestamps (ns): {ts_all[:4]}")
