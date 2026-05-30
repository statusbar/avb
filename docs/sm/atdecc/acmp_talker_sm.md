# ACMP Talker

![diagram](acmp_talker_sm.svg)


**Implements:** IEEE 1722.1-2021 Clause 8.2.4

| Event | Start | Waiting | Connect | Disconnect | GetState | GetConnection |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | -> Waiting | -x- | handle_connect_tx()<br/>-> Waiting | handle_disconnect_tx()<br/>-> Waiting | handle_get_tx_state()<br/>-> Waiting | handle_get_tx_connection()<br/>-> Waiting |
| RcvdConnectTx | -x- | -> Connect | -x- | -x- | -x- | -x- |
| RcvdDisconnectTx | -x- | -> Disconnect | -x- | -x- | -x- | -x- |
| RcvdGetTxState | -x- | -> GetState | -x- | -x- | -x- | -x- |
| RcvdGetTxConnection | -x- | -> GetConnection | -x- | -x- | -x- | -x- |
