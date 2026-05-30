# MRP Periodic

![diagram](periodic_sm.svg)


**Implements:** IEEE 802.1Q-2014 Clause 10.7.5.23

| Event | Start | Passive | Active |
|-------|--------|--------|--------|
| UCT | a_init()<br/>-> Active | -x- | -x- |
| Periodic | -x- | -x- | a_restart_timer()<br/>-> Active |
| PeriodicEnable | -x- | a_restart_timer()<br/>-> Active | -x- |
| PeriodicDisable | -x- | -x- | -> Passive |
