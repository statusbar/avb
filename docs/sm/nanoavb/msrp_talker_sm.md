# MSRP Talker

![diagram](msrp_talker_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 35 (MSRP — Multiple Stream Reservation Protocol, minimal)

| State | Entry | Exit |
|-------|-------|------|
| Advertising | msrp_talker_advertise() | - |
| Withdrawing | msrp_talker_withdraw() | - |

| Event | Start | Idle | Advertising | Ready | Failed | Withdrawing |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Idle | -x- | -x- | -x- | -x- | -x- |
| StartAdvertise | -x- | -> Advertising | -x- | -x- | -x- | -x- |
| StopAdvertise | -x- | -x- | -x- | -> Withdrawing | -> Withdrawing | -x- |
| Ready | -x- | -x- | mark_ready()<br/>-> Ready | -x- | -x- | -x- |
| Failed | -x- | -x- | mark_failed()<br/>-> Failed | -x- | -x- | -x- |
| Lost | -x- | -x- | -x- | -> Advertising | -x- | -x- |
| Withdrawn | -x- | -x- | -x- | -x- | -x- | mark_idle()<br/>-> Idle |
