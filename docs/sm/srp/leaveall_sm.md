# MRP LeaveAll

![diagram](leaveall_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 10.7.5.22

| Event | Start | Passive | Active |
|-------|--------|--------|--------|
| UCT | a_init()<br/>-> Passive | -x- | -x- |
| Tx | -x- | -x- | a_tx_leaveall()<br/>-> Passive |
| RLeaveAll | -x- | a_restart_timer()<br/>-> Passive | a_restart_timer()<br/>-> Passive |
| LvaTimer | -x- | a_restart_timer()<br/>-> Active | a_restart_timer()<br/>-> Active |
