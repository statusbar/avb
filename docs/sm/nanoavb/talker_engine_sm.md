# Talker Engine

![diagram](talker_engine_sm.svg)


**Implements:** (no standard — application-level audio TX pipeline lifecycle)

| Event | Start | Off | Priming | Armed | Running | Muted |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> Off | -x- | -x- | -x- | -x- | -x- |
| AudioReady | -x- | start_audio_source()<br/>-> Priming | -x- | -x- | -x- | -x- |
| Primed | -x- | -x- | arm_stream()<br/>-> Armed | -x- | -x- | -x- |
| GateGo | -x- | -x- | -x- | start_tx()<br/>-> Running | -x- | -x- |
| GateStop | -x- | -x- | -x- | -x- | stop_tx()<br/>-> Armed | stop_tx()<br/>-> Armed |
| Underrun | -x- | -x- | -x- | -x- | mute_tx()<br/>-> Muted | -x- |
| Recovered | -x- | -x- | -x- | -x- | -x- | unmute_tx()<br/>-> Running |
| Fatal | -x- | stop_all()<br/>-> Off | stop_all()<br/>-> Off | stop_all()<br/>-> Off | stop_all()<br/>-> Off | stop_all()<br/>-> Off |
