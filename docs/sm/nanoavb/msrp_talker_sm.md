# MSRP Talker

![diagram](msrp_talker_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 35 (MSRP — Multiple Stream Reservation Protocol, minimal)

| Event | Start | Idle | Advertising | Ready | Failed | Withdrawing |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Idle | -x- | -x- | -x- | -x- | -x- |
| StartAdvertise | -x- | msrp_talker_advertise()<br/>-> Advertising | -x- | -x- | -x- | -x- |
| StopAdvertise | -x- | -x- | -x- | msrp_talker_withdraw()<br/>-> Withdrawing | msrp_talker_withdraw()<br/>-> Withdrawing | -x- |
| Ready | -x- | -x- | mark_ready()<br/>-> Ready | -x- | -x- | -x- |
| Failed | -x- | -x- | mark_failed()<br/>-> Failed | -x- | -x- | -x- |
| Lost | -x- | -x- | -x- | msrp_talker_advertise()<br/>-> Advertising | -x- | -x- |
| Withdrawn | -x- | -x- | -x- | -x- | -x- | mark_idle()<br/>-> Idle |
