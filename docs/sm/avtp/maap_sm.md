# MAAP (Multicast Address Acquisition Protocol)

![diagram](maap_sm.svg)


**Implements:** IEEE 1722-2025 Annex B.3

| Event | Start | Initial | Probe | Defend |
|-------|--------|--------|--------|--------|
| UCT | init()<br/>-> Initial | -x- | -x- | -x- |
| Begin | -x- | begin_acquire()<br/>-> Probe | -x- | -x- |
| Release | -x- | -x- | release()<br/>-> Initial | release()<br/>-> Initial |
| rProbe | -x- | -x- | restart_probing()<br/>-> Probe | send_defend()<br/>-> Defend |
| rDefend | -x- | -x- | restart_probing()<br/>-> Probe | restart_probing()<br/>-> Probe |
| rAnnounce | -x- | -x- | restart_probing()<br/>-> Probe | restart_probing()<br/>-> Probe |
| ProbeCount | -x- | -x- | probe_complete()<br/>-> Defend | -x- |
| AnnounceTimer | -x- | -x- | -x- | announce_tick()<br/>-> Defend |
| ProbeTimer | -x- | -x- | probe_tick()<br/>-> Probe | -x- |
| PortOperational | -x- | begin_acquire()<br/>-> Probe | restart_probing()<br/>-> Probe | restart_probing()<br/>-> Probe |
