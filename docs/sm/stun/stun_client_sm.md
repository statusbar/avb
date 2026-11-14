# STUN Client Session

![diagram](stun_client_sm.svg)


**Implements:** RFC 8489 (STUN) + private REGISTER extension for rendezvous

| State | Entry | Exit |
|-------|-------|------|
| Paired | enter_paired() | - |
| Failed | enter_failed() | - |

| Event | Idle | Registering | Waiting | Paired | Failed |
|-------|--------|--------|--------|--------|--------|
| Start | mark_send()<br/>-> Registering | -x- | -x- | -x- | -x- |
| ResponseWaiting | -x- | enter_waiting()<br/>-> Waiting | -x- | -x- | -x- |
| ResponsePaired | -x- | -> Paired | -> Paired | -> Paired | -x- |
| ResponseError | -x- | -> Failed | -> Failed | -x- | -x- |
| ResponseSessionExpired | -x- | mark_send()<br/>-> Registering | -x- | -x- | -x- |
| Rto | -x- | mark_retransmit()<br/>-> Registering | -x- | -x- | -x- |
| RetryBudgetExhausted | -x- | -> Failed | -x- | -x- | -x- |
| RefreshTick | -x- | -x- | mark_send()<br/>-> Registering | mark_send()<br/>-> Registering | -x- |
