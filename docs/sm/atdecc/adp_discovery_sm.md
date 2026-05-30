# ADP Discovery

![diagram](adp_discovery_sm.svg)


**Implements:** IEEE 1722.1-2021 Clause 6.2.4

| Event | Start | Waiting |
|-------|--------|--------|
| UCT | -> Waiting | -x- |
| RcvdAvailable | -x- | handle_available()<br/>-> Waiting |
| RcvdDeparting | -x- | handle_departing()<br/>-> Waiting |
| DoDiscover | -x- | send_discover()<br/>-> Waiting |
