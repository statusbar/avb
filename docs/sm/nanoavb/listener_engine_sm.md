# Listener Engine

![diagram](listener_engine_sm.svg)


**Implements:** (no standard — application-level audio RX pipeline lifecycle)

| Event | Start | Off | Listening | Syncing | Playing | Muted |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Off | -x- | -x- | -x- | -x- | -x- |
| GateListen | -x- | enable_rx_filter()<br/>-> Listening | -x- | -x- | -x- | -x- |
| FirstPacket | -x- | -x- | start_sync()<br/>-> Syncing | -x- | -x- | -x- |
| Synced | -x- | -x- | -x- | start_audio_sink()<br/>-> Playing | -x- | -x- |
| GateStop | -x- | -x- | stop_all()<br/>-> Off | stop_all()<br/>-> Off | stop_all()<br/>-> Off | stop_all()<br/>-> Off |
| PacketGap | -x- | -x- | -x- | resync()<br/>-> Syncing | resync()<br/>-> Syncing | resync()<br/>-> Syncing |
| Underrun | -x- | -x- | -x- | -x- | mute_out()<br/>-> Muted | -x- |
| Recovered | -x- | -x- | -x- | -x- | -x- | unmute_out()<br/>-> Playing |
