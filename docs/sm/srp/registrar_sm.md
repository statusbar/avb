# MRP Registrar

![diagram](registrar_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 10.7.8 Table 10-4

| Event | Start | In | Lv | Mt |
|-------|--------|--------|--------|--------|
| UCT | a_init()<br/>-> Mt | -x- | -x- | -x- |
| RNew | -x- | a_notify_new()<br/>-> In | a_notify_new()<br/>-> In | a_notify_new()<br/>-> In |
| RJoinIn | -x- | -x- | a_notify_join()<br/>-> In | a_notify_join()<br/>-> In |
| RJoinMt | -x- | -x- | a_notify_join()<br/>-> In | a_notify_join()<br/>-> In |
| RLeave | -x- | a_notify_lv_and_start_lvtimer()<br/>-> Lv | a_notify_lv()<br/>-> Lv | a_notify_lv()<br/>-> Mt |
| RLeaveAll | -x- | a_start_lvtimer()<br/>-> Lv | -x- | -x- |
| TxLeaveAll | -x- | a_start_lvtimer()<br/>-> Lv | -x- | -x- |
| Redeclare | -x- | a_start_lvtimer()<br/>-> Lv | -x- | -x- |
| LvTimer | -x- | -x- | a_notify_lv()<br/>-> Mt | -x- |
| Flush | -x- | a_notify_lv()<br/>-> Mt | a_notify_lv()<br/>-> Mt | a_notify_lv()<br/>-> Mt |
