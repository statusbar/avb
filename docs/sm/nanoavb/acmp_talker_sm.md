# ACMP Talker

![diagram](acmp_talker_sm.svg)


**Implements:** IEEE 1722.1-2021 Clause 8.2.4 (minimal entity-side connection-state tracker)

| Event | Start | Idle | Connected |
|-------|--------|--------|--------|
| UCT | init()<br/>-> Idle | -x- | -x- |
| ConnectRxOk | -x- | add_listener()<br/>-> Connected | add_listener()<br/>-> Connected |
| ConnectRxFail | -x- | reject_connect()<br/>-> Idle | -x- |
| DisconnectRx | -x- | -x- | remove_listener()<br/>-> Connected |
| LinkDown | -x- | -x- | drop_all()<br/>-> Idle |
