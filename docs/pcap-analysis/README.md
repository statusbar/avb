# AVTP stream pcap analyzers

Pure-Python (no deps) decoders for tcpdump captures of AVB audio streams, used to
diagnose the GPS media-clock / the audio interface + the DSP processor interop work (see
[../GPS_MEDIA_CLOCK.md](../GPS_MEDIA_CLOCK.md)). Capture with, e.g.:

```
sudo tcpdump -i eth0 -p -s 0 -w stream.pcap "ether dst 91:e0:f0:00:fe:01"
```

They handle untagged and single-VLAN-tagged AVTP (ethertype 0x22f0).

| Script | What it does |
|--------|--------------|
| `aaf_analyze.py`   | AAF (subtype 0x02): format/rate/channels, SYT validity, seq gaps, per-channel peak/RMS/dominant-freq. |
| `am824_analyze.py` | AM824 / IEC 61883-6 (subtype 0x00): CIP DBS/FDF/SYT, labels, seq gaps, 24-bit sample stats, per-channel tone. |
| `am824_fields.py`  | Per-packet timing fields: `tv`, `mr`, `avtp_timestamp` (+delta), `DBC`, `SYT` — and delta/DBC histograms. Use to see whether the talker's avtp_timestamp is smooth. |
| `am824_residual.py`| Fits the fundamental (FULL-signal least-squares, finely resolved) and reports residual RMS + SNR + periodic energy bursts. The fine fit matters: a 0.05 Hz fit error over a multi-second record fakes a coherence loss. |
| `am824_glitch.py`  | Hunts sample-level discontinuities (drops/repeats) and reports their rate/spacing. |
| `crf_analyze.py`   | CRF (Clock Reference Format, subtype 0x04): header (type/pull/base_frequency/interval), seq gaps, and the 64-bit timestamps — checks they're monotonic and evenly spaced, and reports the media rate (gap-robust, from within-packet spacing). |

Usage: `python3 <script>.py <capture.pcap> [args]`. AM824 scripts assume 96 kHz.
