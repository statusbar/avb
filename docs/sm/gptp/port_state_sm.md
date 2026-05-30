# gPTP Slave-Role Port FSM

![diagram](port_state_sm.svg)


**Implements:** IEEE 802.1AS-2020 Clause 10 (slave-only lifecycle abstraction over PortSyncSyncReceive / MDPdelayReq / SiteSyncSync)

| Event | Start | Disabled | Initializing | Listening | Uncalibrated | Slave |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | a_init()<br/>-> Disabled | -x- | -x- | -x- | -x- | -x- |
| LinkUp | -x- | a_enter_initializing()<br/>-> Initializing | -x- | -x- | -x- | -x- |
| LinkDown | -x- | -x- | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled |
| AsCapableAcquired | -x- | -x- | a_enter_listening()<br/>-> Listening | a_enter_uncalibrated()<br/>-> Uncalibrated | -x- | -x- |
| AsCapableLost | -x- | -x- | -x- | -x- | a_drop_as_capable()<br/>-> Listening | a_drop_as_capable()<br/>-> Listening |
| FirstSyncLocked | -x- | -x- | -x- | -x- | a_enter_slave()<br/>-> Slave | -x- |
| SyncLost | -x- | -x- | -x- | -x- | -x- | a_lose_sync()<br/>-> Uncalibrated |
| AdministrativeDisable | -x- | -x- | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled | a_enter_disabled()<br/>-> Disabled |
