# gPTP Timebase

![diagram](gptp_sm.svg)


**Implements:** IEEE 802.1AS-2020 Clause 10 (minimal slave-port lifecycle)

| State | Entry | Exit |
|-------|-------|------|
| Acquiring | start_servo() | - |

| Event | Start | Unlocked | Acquiring | Locked |
|-------|--------|--------|--------|--------|
| UCT | init()<br/>-> Unlocked | -x- | -x- | -x- |
| AsCapableUp | -x- | -> Acquiring | -x- | -x- |
| AsCapableDown | -x- | -x- | report_unlocked()<br/>-> Unlocked | report_unlocked()<br/>-> Unlocked |
| LockedStable | -x- | -x- | report_locked()<br/>-> Locked | -x- |
| LockLost | -x- | -x- | -x- | -> Acquiring |
