# STUN Client Session

![diagram](stun_client_sm.svg)


**Implements:** RFC 8489 (STUN) + private REGISTER extension for rendezvous

| Event | Idle | Registering | Waiting | Paired | Failed |
|-------|--------|--------|--------|--------|--------|
| Start | mark_send()<br/>-> Registering | -x- | -x- | -x- | -x- |
| ResponseWaiting | -x- | enter_waiting()<br/>-> Waiting | -x- | -x- | -x- |
| ResponsePaired | -x- | enter_paired()<br/>-> Paired | enter_paired()<br/>-> Paired | enter_paired()<br/>-> Paired | -x- |
| ResponseError | -x- | enter_failed()<br/>-> Failed | enter_failed()<br/>-> Failed | -x- | -x- |
| ResponseSessionExpired | -x- | mark_send()<br/>-> Registering | -x- | -x- | -x- |
| Rto | -x- | mark_retransmit()<br/>-> Registering | -x- | -x- | -x- |
| RetryBudgetExhausted | -x- | enter_failed()<br/>-> Failed | -x- | -x- | -x- |
| RefreshTick | -x- | -x- | mark_send()<br/>-> Registering | mark_send()<br/>-> Registering | -x- |
