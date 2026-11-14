# MVRP VLAN

![diagram](mvrp_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 11 (MVRP — Multiple VLAN Registration Protocol)

| State | Entry | Exit |
|-------|-------|------|
| Error | mark_error() | - |

| Event | Start | NotJoined | Joining | Joined | Leaving | Error |
|-------|--------|--------|--------|--------|--------|--------|
| UCT | init()<br/>-> NotJoined | -x- | -x- | -x- | -x- | -x- |
| Acquire | -x- | send_join()<br/>-> Joining | -x- | -x- | -x- | -x- |
| ReleaseLast | -x- | -x- | -x- | send_leave()<br/>-> Leaving | -x- | -x- |
| JoinOk | -x- | -x- | mark_joined()<br/>-> Joined | -x- | -x- | -x- |
| JoinFail | -x- | -x- | -> Error | -x- | -x- | -x- |
| LeaveOk | -x- | -x- | -x- | -x- | mark_left()<br/>-> NotJoined | -x- |
| LeaveFail | -x- | -x- | -x- | -x- | -> Error | -x- |
| Reset | -x- | -x- | -x- | -x- | -x- | reset()<br/>-> NotJoined |
