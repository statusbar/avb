# MSRP Listener

![diagram](msrp_listener_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 35 (MSRP — Multiple Stream Reservation Protocol, minimal)

| Event | Start | Idle | Joining | Ready | Failed | Leaving |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Idle | -x- | -x- | -x- | -x- | -x- |
| StartJoin | -x- | msrp_listener_ready()<br/>-> Joining | -x- | -x- | -x- | -x- |
| StopJoin | -x- | -x- | -x- | msrp_listener_leave()<br/>-> Leaving | msrp_listener_leave()<br/>-> Leaving | -x- |
| Ready | -x- | -x- | mark_ready()<br/>-> Ready | -x- | -x- | -x- |
| Failed | -x- | -x- | mark_failed()<br/>-> Failed | -x- | -x- | -x- |
| Lost | -x- | -x- | -x- | msrp_listener_ready()<br/>-> Joining | -x- | -x- |
| Left | -x- | -x- | -x- | -x- | -x- | mark_idle()<br/>-> Idle |
