# Device Supervisor

![diagram](supervisor_sm.svg)


**Implements:** (no standard — application-level orchestrator over the protocol SMs below)

| Event | Start | Down | Init | WaitVlanBase | Ready | Degraded |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init_iface()<br/>-> Down | -x- | -x- | -x- | -x- | -x- |
| LinkUp | -x- | start_protocols()<br/>-> Init | -x- | -x- | -x- | -x- |
| LinkDown | -x- | -> Down | stop_all()<br/>-> Down | stop_all()<br/>-> Down | stop_all()<br/>-> Down | stop_all()<br/>-> Down |
| GptpLocked | -x- | -x- | enter_wait_vlan()<br/>-> WaitVlanBase | -x- | -x- | enter_wait_vlan()<br/>-> WaitVlanBase |
| GptpLost | -x- | -x- | -x- | degrade_stop_streams()<br/>-> Degraded | degrade_stop_streams()<br/>-> Degraded | -x- |
| VlanBaseReady | -x- | -x- | -x- | enter_ready()<br/>-> Ready | -x- | -x- |
| Timeout | -x- | -x- | timeout_gptp()<br/>-> Down | timeout_vlan()<br/>-> Degraded | -x- | -x- |
