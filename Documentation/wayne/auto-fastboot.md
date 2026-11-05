# Automatic Fastboot recovery for temporary Wayne kernels

This is an opt-in recovery feature for bring-up images, based on the previously
validated MMCC configuration. It does not repair USB itself or boot Android.
The feature is disabled in other configurations and requires wayne.bootguard=1.

The board restart protocol comes from the Wayne vendor source: IMEM at
0x146bf000, restart-reason offset 0x65c, bootloader magic 0x77665500,
normal magic 0x77665501 and recovery magic 0x77665502. The native board DTS
exposes that protocol through syscon-reboot-mode. A chosen phandle and the
packer DT validator check the resource, offset, values and reference before
an image can be packaged for a guarded test.

The built-in Wayne guard runs at arch initcall level 3, pre-arms the bootloader
reason, and rewrites it from panic/restart notifiers. The opt-in Qualcomm
watchdog registers at subsystem level 4, ahead of USB device initcalls at
level 6. Its boot_timeout parameter starts the hardware watchdog even when
firmware did not leave it running. The normal watchdog driver/module path is
unchanged when QCOM_WAYNE_BOOTGUARD is disabled; both branches were compiled.
On the first phone trial, both IMEM and the watchdog armed and the timer
rebooted to the original OS. The vendor restart path also records the
bootloader reason in PM660 PON. Use the native SPMI and PMIC drivers and
pre-arm the existing PM660 PON reboot mode after its device probes. The
guard does not change PMIC power-off settings. The host can fall back to
ADB reboot bootloader when the original OS reappears.

The diagnostic init supports the exact token wayne.reboot_mode=bootloader.
Its timer uses Linux RESTART2 with the bootloader argument. The default
non-guarded init reboot behavior is preserved. The explicit test-only token
wayne.test_pid1_exit=1 exits PID 1 after at least five seconds to test the
kernel panic recovery path. It is absent from normal guarded tests.

## Build and verify

    JOBS=24 WAYNE_RAM_PROBE_STAGE=bootguard tools/wayne/build-ram-probe.sh boot
    JOBS=24 WAYNE_RAM_PROBE_STAGE=bootguardwdt tools/wayne/build-ram-probe.sh boot
    JOBS=24 WAYNE_RAM_PROBE_STAGE=bootguardpanic tools/wayne/build-ram-probe.sh boot

All three use the same kernel configuration. Their packed images
have separate default directories and differ in the diagnostic command line.
An unchanged embedded cpio no longer forces an Image relink between profiles.

| Stage | Software deadline | Hardware configuration | Intended result |
| --- | --- | --- | --- |
| bootguard | 60 seconds | 30-second watchdog; kernel open window 120 seconds | Timer restarts to Fastboot. |
| bootguardwdt | 180 seconds | 30-second watchdog; kernel open window 30 seconds | Watchdog expires before the software timer. |
| bootguardpanic | PID 1 exits after 5 loop seconds; panic timeout 5 seconds | Same watchdog fallback as bootguard | Panic restarts to Fastboot. |

The open window allows kernel watchdog keepalives without a userspace daemon.
A hard lockup after arming stops those keepalives; a live kernel stalled during
initialization eventually exhausts the open window. These are configured
recovery paths, not a claim that every possible failure is recoverable. A fault
before the guard/watchdog arms, a stopped watchdog clock, or a bootloader failure
can still require physical intervention.

Use tools/wayne/qemu-boot-smoke.py with a pinned source and profile to test the
actual packed Image.gz-dtb. It checks both boot images, their section hashes,
the appended DTB and the embedded cpio; it does not read a mutable build Image.
It preserves requested blacklists and verifies they actually fired. Use
--expect panic only for the intentional PID 1 exit profile. QEMU tests PID 1,
software reboot and panic behavior, not the phone IMEM or watchdog hardware.

Physical validation of these three recovery stages is pending. A successful
recovery must be confirmed with the Fastboot protocol on the original Wayne
serial, not inferred from a Qualcomm USB PID. The host can then temporarily
boot the verified stock backup to collect pstore without flashing a partition.
