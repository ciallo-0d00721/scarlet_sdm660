# Wayne hardware observations, 2026-10-27

All times are Asia/Shanghai (UTC+08). These observations concern the genuine
upstream Linux v6.12 source at
`3a3d4145b57302f2723a8b700d6de3fd4a48fa2d`, not the repository's vendor 4.19
default branch. Each image below was accepted by temporary `fastboot boot`.
No boot or recovery partition was flashed. Each header was verified
against its stated parent comparison and intended parameters;
the kernel, DTB and RAM init payload bytes were identical.

## Established boundary

The first framebuffer trial displayed readable kernel text on the physical
Mi 6X. A user-supplied photo showed the visible tail near
`udma_driver_init ... returned 0`. This establishes that the firmware
framebuffer mapping is usable for console text in that boot. It does not
establish native panel/DSI/GPU support or identify the last displayed driver
as the fault. No PID 1 marker, working USB console or Android boot has been
verified.

## Header comparisons

| Start | Comparison | Observed result |
| --- | --- | --- |
| 21:25:21 | UART + framebuffer baseline; panic=15, init reboot timer=90 | Readable text; apparent stall near the DMA initialization tail. |
| 21:32:17 | Baseline plus cpuidle.off=1 | User reported the same apparent stall. |
| 21:38:09 | Baseline with earlycon and ttyMSM0 console removed; default CPU idle retained | User reported a return to the charger screen after roughly ten-plus seconds. |
| 21:46:57 | No-UART comparison with panic=0 instead of panic=15 | User again reported a return to the charger screen. |
| 21:53:13 | Panic-hold comparison plus regulator_ignore_unused | User reported the same charger-screen outcome. |

The no-UART, panic-hold and regulator comparisons do not establish successful
PID 1 execution. The charger-screen transition must not be described as the
diagnostic init's planned 90-second reboot without a corresponding marker or
other evidence. The configured panic timeout is not sufficient to explain
the outcome because it also occurred with panic=0.

Completed Windows captures for these trials observed zero CDC-ACM serial
connections. Post-recovery pstore was either empty or contained the original
vendor kernel's charger-mode records. Those vendor records are not 6.12
failure logs. A recent Android LAST_KMSG archive likewise contained a vendor
charger record; an older archive contained a previous mainline trial.

At 21:57:19 a further comparison added only
`initcall_blacklist=qcom_watchdog_driver_init` to the regulator-hold command
line. Its screen/recovery interpretation is pending confirmation. The actual
initcall name was checked against the compiled kernel symbol table. No
watchdog-related root cause is claimed.

Original boot/recovery hashes continued matching their read-only backups
during each preflight. Raw phone logs and the photo are retained locally;
they are not included in this source tree.
