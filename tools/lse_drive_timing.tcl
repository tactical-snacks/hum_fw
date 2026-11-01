# Time LSE (32.768 kHz) startup at each drive level, then cold-reset the LSE and reboot.
# usage: openocd -c "reset_config srst_only srst_nogate connect_assert_srst" -f interface/stlink.cfg -c "transport select swd" -f target/stm32u0x.cfg -f tools/lse_drive_timing.tcl
# Needs backup domain write access (PWR_CR1.DBP), which the firmware leaves enabled.
# RCC_BDCR = 0x40021090: LSEON bit 0, LSERDY bit 1, LSEDRV bits 4:3, BDRST bit 16

proc lse_time {drv name} {
    mww 0x40021090 0x00
    sleep 2000
    mww 0x40021090 [expr {$drv << 3}]
    set t0 [clock milliseconds]
    mww 0x40021090 [expr {($drv << 3) | 1}]
    while {[expr {[read_memory 0x40021090 32 1] & 2}] == 0} {
        if {[clock milliseconds] - $t0 > 15000} {
            echo "$name: no start in 15 s"
            return
        }
        sleep 20
    }
    echo "$name: ready in [expr {[clock milliseconds] - $t0}] ms"
}

init
halt
foreach run {1 2} {
    lse_time 0 "low       #$run"
    lse_time 1 "med-low   #$run"
    lse_time 2 "med-high  #$run"
    lse_time 3 "high      #$run"
}
foreach run {1 2} {
    lse_time 3 "high      #$run"
    lse_time 2 "med-high  #$run"
    lse_time 1 "med-low   #$run"
    lse_time 0 "low       #$run"
}

# backup domain reset stops the LSE, so the firmware sees a cold start
mww 0x40021090 0x10000
mww 0x40021090 0x0
reset run
shutdown
