# ACMP Listener

![diagram](acmp_listener_sm.svg)


**Implements:** IEEE 1722.1-2021 Clause 8.2.5

| Event | Start | Waiting | ConnectTxCmd | DisconnectTxCmd | ConnectTxResp | DisconnectTxResp | GetState |
|-------|--------|--------|--------|--------|--------|--------|--------|
| UCT | -> Waiting | -x- | send_connect_tx()<br/>-> ConnectTxResp | send_disconnect_tx()<br/>-> DisconnectTxResp | -x- | -x- | handle_get_rx_state()<br/>-> Waiting |
| RcvdConnectRx | -x- | -> ConnectTxCmd | -x- | -x- | -x- | -x- | -x- |
| RcvdDisconnectRx | -x- | -> DisconnectTxCmd | -x- | -x- | -x- | -x- | -x- |
| RcvdGetRxState | -x- | -> GetState | -x- | -x- | -x- | -x- | -x- |
| RcvdConnectTxResp | -x- | handle_connect_tx_response()<br/>-> Waiting | -x- | -x- | handle_connect_tx_response()<br/>-> Waiting | -x- | -x- |
| RcvdDisconnectTxResp | -x- | -x- | -x- | -x- | -x- | handle_disconnect_tx_response()<br/>-> Waiting | -x- |
| TxTimeout | -x- | handle_timeout()<br/>-> Waiting | -x- | -x- | handle_timeout()<br/>-> Waiting | handle_timeout()<br/>-> Waiting | -x- |
