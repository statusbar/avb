# ACMP Listener

![diagram](acmp_listener_sm.svg)


**Implements:** IEEE 1722.1-2021 Clause 8.2.5 (minimal entity-side connection-state tracker)

| Event | Start | Idle | Connecting | Connected | Disconnecting |
|-------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Idle | -x- | -x- | -x- | -x- |
| ConnectReq | -x- | send_connect_tx()<br/>-> Connecting | -x- | -x- | -x- |
| ConnectOk | -x- | -x- | mark_connected()<br/>-> Connected | -x- | -x- |
| ConnectFail | -x- | -x- | mark_failed()<br/>-> Idle | -x- | -x- |
| DisconnectReq | -x- | -x- | -x- | send_disconnect_tx()<br/>-> Disconnecting | -x- |
| DisconnectOk | -x- | -x- | -x- | -x- | mark_disconnected()<br/>-> Idle |
| LinkDown | -x- | -x- | -x- | mark_disconnected()<br/>-> Idle | mark_disconnected()<br/>-> Idle |
