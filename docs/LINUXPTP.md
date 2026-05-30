<!-- Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com> -->


# linuxptp config and command line for raspberry pi 5


    ptp4l -f /etc/linuxptp/gptp-slave.conf -i eth0 -m



    [global]
    gmCapable              1
    priority1              248
    priority2              248
    domainNumber           0
    logAnnounceInterval    0
    logSyncInterval        -3
    logMinPdelayReqInterval 0
    syncReceiptTimeout     3
    neighborPropDelayThresh 800
    min_neighbor_prop_delay -20000000
    clock_servo            pi
    step_threshold         0.0
    time_stamping          hardware
    assume_two_step        1
    path_trace_enabled     1
    follow_up_info         1
    transportSpecific      0x1
    ptp_dst_mac            01:80:C2:00:00:0E
    network_transport      L2
    delay_mechanism        P2P
    summary_interval       0

