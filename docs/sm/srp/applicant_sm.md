# MRP Applicant

![diagram](applicant_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 10.7.7

| Event | Start | Vo | Vp | Vn | An | Aa | Qa | La | Ao | Qo | Ap | Qp | Lo |
|-------|--------|--------|--------|--------|--------|--------|--------|--------|--------|--------|--------|--------|--------|
| UCT | a_init()<br/>-> Vo | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- |
| New | -x- | -> Vn | -> Vn | -x- | -x- | -> Vn | -> Vn | -> Vn | -> Vn | -> Vn | -> Vn | -> Vn | -> Vn |
| Join | -x- | -> Vp | -x- | -x- | -x- | -x- | -x- | -> Aa | -> Ap | -> Qp | -x- | -x- | -> Vp |
| Leave | -x- | -x- | -> Vo | -> La | -> La | -> La | -> La | -x- | -x- | -x- | -> Ao | -> Qo | -x- |
| TxRegistrarIn | -x- | a_tx_in_optional()<br/>-> Vo | a_tx_join_required()<br/>-> Aa | a_tx_new()<br/>-> An | a_tx_new()<br/>-> Qa | a_tx_join_required()<br/>-> Qa | a_tx_join_optional()<br/>-> Qa | a_tx_leave()<br/>-> Vo | a_tx_in_optional()<br/>-> Ao | a_tx_in_optional()<br/>-> Qo | a_tx_join_required()<br/>-> Qa | a_tx_in_optional()<br/>-> Qp | a_tx_in_required()<br/>-> Vo |
| TxRegistrarMt | -x- | a_tx_in_optional()<br/>-> Vo | a_tx_join_required()<br/>-> Aa | a_tx_new()<br/>-> An | a_tx_new()<br/>-> Aa | a_tx_join_required()<br/>-> Qa | a_tx_join_optional()<br/>-> Qa | a_tx_leave()<br/>-> Vo | a_tx_in_optional()<br/>-> Ao | a_tx_in_optional()<br/>-> Qo | a_tx_join_required()<br/>-> Qa | a_tx_in_optional()<br/>-> Qp | a_tx_in_required()<br/>-> Vo |
| TxLeaveAll | -x- | a_tx_in_optional()<br/>-> Lo | a_tx_in_required()<br/>-> Aa | a_tx_new()<br/>-> An | a_tx_new()<br/>-> Qa | a_tx_join_required()<br/>-> Qa | a_tx_join_required()<br/>-> Qa | a_tx_in_optional()<br/>-> Lo | a_tx_in_optional()<br/>-> Lo | a_tx_in_optional()<br/>-> Lo | a_tx_join_required()<br/>-> Qa | a_tx_join_required()<br/>-> Qa | a_tx_in_optional()<br/>-> Lo |
| TxLeaveAllFull | -x- | -> Lo | -x- | -x- | -> Vn | -> Vp | -> Vp | -> Lo | -> Lo | -> Lo | -> Vp | -> Vp | -x- |
| RNew | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- | -x- |
| RJoinIn | -x- | -x- | -x- | -x- | -x- | -> Qa | -x- | -x- | -> Qo | -x- | -> Qp | -x- | -x- |
| RIn | -x- | -x- | -x- | -x- | -x- | -> Qa | -x- | -x- | -x- | -x- | -x- | -x- | -x- |
| RJoinMt | -x- | -x- | -x- | -x- | -x- | -x- | -> Aa | -x- | -x- | -> Ao | -x- | -> Ap | -> Vo |
| RMt | -x- | -x- | -x- | -x- | -x- | -x- | -> Aa | -x- | -x- | -> Ao | -x- | -> Ap | -> Vo |
| RLeave | -x- | -> Lo | -x- | -x- | -> Vn | -> Vp | -> Vp | -x- | -> Lo | -> Lo | -> Vp | -> Vp | -x- |
| RLeaveAll | -x- | -> Lo | -x- | -x- | -> Vn | -> Vp | -> Vp | -x- | -> Lo | -> Lo | -> Vp | -> Vp | -x- |
| Redeclare | -x- | -> Lo | -x- | -x- | -> Vn | -> Vp | -> Vp | -x- | -> Lo | -> Lo | -> Vp | -> Vp | -x- |
| Periodic | -x- | -x- | -x- | -x- | -x- | -x- | -> Aa | -x- | -x- | -x- | -x- | -> Ap | -x- |
