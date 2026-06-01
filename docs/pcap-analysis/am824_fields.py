#!/usr/bin/env python3
# Dump AM824/61883 timing fields (tv, avtp_timestamp, mr, DBC, SYT) per packet.
import struct, sys, statistics

PATH = sys.argv[1]
N0 = int(sys.argv[2]) if len(sys.argv) > 2 else 16
blob = open(PATH, "rb").read()
magic = struct.unpack("<I", blob[:4])[0]
end = "<" if magic in (0xA1B2C3D4, 0xA1B23C4D) else ">"
off = 24
rows = []
ts_list = []
dbc_list = []
tvset = 0
tvclr = 0
mrset = 0
sytvalid = 0
sytinval = 0
sid = None
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
    if len(h) < 32 or h[0] != 0x00:
        continue
    if sid is None:
        sid = h[4:12]
    seq = h[2]
    tv = h[1] & 0x01
    mr = (h[1] >> 3) & 1
    ats = struct.unpack(">I", h[12:16])[0]
    dbc = h[27]
    syt = struct.unpack(">H", h[30:32])[0]
    tvset += tv
    tvclr += 1 - tv
    mrset += mr
    if syt == 0xFFFF:
        sytinval += 1
    else:
        sytvalid += 1
    if len(rows) < N0:
        rows.append((seq, tv, mr, ats, dbc, syt))
    ts_list.append(ats)
    dbc_list.append(dbc)
print(f"file: {PATH.split('/')[-1]}")
print(f"stream_id={sid.hex(':') if sid else '-'}  packets={len(ts_list)}")
print(
    f"tv: set={tvset} clear={tvclr}    mr(media-reset) set={mrset}    SYT valid={sytvalid} invalid(0xFFFF)={sytinval}"
)
print(
    f"{'seq':>4} {'tv':>2} {'mr':>2} {'avtp_timestamp':>14} {'+dt(ns)':>9} {'DBC':>4} {'SYT':>6}"
)
prev = None
for seq, tv, mr, ats, dbc, syt in rows:
    dt = ((ats - prev) & 0xFFFFFFFF) if prev is not None else 0
    print(f"{seq:>4} {tv:>2} {mr:>2} {ats:>14} {dt:>9} {dbc:>4} 0x{syt:04x}")
    prev = ats
# avtp_timestamp delta stats over whole file (mod 2^32)
deltas = [(ts_list[i] - ts_list[i - 1]) & 0xFFFFFFFF for i in range(1, len(ts_list))]
# drop wrap outliers (>1e9)
d = [x for x in deltas if x < 1_000_000_000]
if d:
    print(
        f"\navtp_timestamp delta: median={statistics.median(d):.0f} ns  mean={statistics.mean(d):.1f} ns  "
        f"min={min(d)} max={max(d)}  stdev={statistics.pstdev(d):.1f} ns"
    )
    print(f"  (expect ~125000 ns/pkt for 8000 pkt/s; jitter=stdev)")
dd = [(dbc_list[i] - dbc_list[i - 1]) & 0xFF for i in range(1, len(dbc_list))]
from collections import Counter

print(f"DBC delta histogram: {dict(sorted(Counter(dd).items()))}  (expect 12/pkt)")
