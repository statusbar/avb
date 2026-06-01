# STUN Server Session

![diagram](stun_server_session_sm.svg)


**Implements:** RFC 8489 (STUN) + private REGISTER extension for rendezvous

| Event | Empty | OneRegistered | Paired | Expired |
|-------|--------|--------|--------|--------|
| FirstRegister | touch()<br/>-> OneRegistered | -x- | -x- | -x- |
| SecondRegister | -x- | touch()<br/>-> Paired | -x- | -x- |
| RefreshOne | -x- | touch()<br/>-> OneRegistered | touch()<br/>-> Paired | -x- |
| RefreshTwo | -x- | -x- | touch()<br/>-> Paired | -x- |
| DuplicateEui64 | -x- | touch()<br/>-> OneRegistered | touch()<br/>-> Paired | -x- |
| ExpireTick | -x- | mark_expired()<br/>-> Expired | mark_expired()<br/>-> Expired | -x- |
