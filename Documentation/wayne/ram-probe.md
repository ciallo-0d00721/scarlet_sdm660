# Wayne RAM-only diagnostic profile

This profile uses the genuine upstream Linux v6.12 baseline recorded in upstream.json. It is a bring-up experiment, not an Android boot image suitable for daily use.

The framebuffer-console tests proved readable kernel text on the physical Mi 6X, but did not verify PID 1. The later QCOM-only platform configuration also returned to the charger screen. Its QEMU success did not establish phone boot success. Skipping the Qualcomm watchdog probe in one earlier header comparison automatically returned to the original Android desktop; the cause remains unknown, and no PID 1 marker was observed.

The RAM probe removes the specific PMIC/RPM/SCM, SoC clock, pinctrl, storage, USB, remote processor, watchdog, CPU idle and CPU frequency drivers from the diagnostic kernel. Generic PM, input and reset cores remain selected by dependencies. It preserves the existing board DTB and firmware framebuffer mapping. USB enumeration is not expected for this profile. PL011 is retained for the QEMU virt test; it does not bind the Wayne MSM UART.

A static diagnostic /init is embedded as an uncompressed cpio inside the kernel. A duplicate external gzip ramdisk remains in the Android boot image for bootloader compatibility. The packer verifies the embedded archive against the freshly generated init and against the uncompressed ARM64 Image. The independent QEMU check must omit -initrd to show that /init is really supplied by the kernel.

Build on Linux with Clang/LLD 18 and AArch64 GCC static libc:

    JOBS=24 tools/wayne/build-ram-probe.sh boot

The default output directory is separate from the ordinary profile. The built-in cpio input path is relative to the build directory. The new profile uses maxcpus=1 and a framebuffer console; its diagnostic init asks for a reboot after 90 seconds only if PID 1 is reached. A charger-screen transition alone does not prove that timer ran.

Validation status is recorded separately from implementation: compilation, image/header validation and QEMU are prerequisites for a temporary physical fastboot boot trial, not evidence of Android support. Never flash this experiment based on these checks alone.

## Physical validation, 2026-11-05

The RAM-core image built from commit 03b81c279504795ea180e4f58dafefe8883e52c1
(SHA-256 d80632d90b36e136a70d49ca65c95b3eb05ca0b05c958513c06babb5d87909a4,
padded to 64 MiB) reached standalone PID 1 on the physical Mi 6X. The screen
showed BRINGUP_READY and a PID 1 heartbeat at approximately 32 seconds.
After the phone returned to its original Android system, pstore contained the
6.12 RAM-probe version, WAYNE_INIT_STARTED pid=1 and REBOOTING_AFTER_TEST at
about 92 seconds. Some pstore bytes were corrupted; the framebuffer photo
independently confirms the readiness marker. Boot and recovery were not flashed.
This establishes RAM-only userspace, not Android, eMMC, radio or USB support.

## SCM isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=scm tools/wayne/build-ram-probe.sh boot

The SCM stage uses separate out-6.12-ram-scm outputs. It enables the generic
Qualcomm pinctrl core to select the otherwise hidden QCOM_SCM option, together
with its generic TrustZone memory allocator dependencies. No SoC-specific pin
controller driver (including SDM660) is selected, so the TLMM hardware probe
remains absent. The generic pinctrl core alone has no platform-driver binding.
All other RAM-profile requirements remain checked. The initial SCM-stage
configuration contains 140 requested requirements. It keeps the identical board
DTB and diagnostic initramfs and the same one-CPU, framebuffer-only command line.

The SCM-stage image at e278e209737a3f94a4b29e129a82bee6d1f8c9f2
(SHA-256 b1f9d20b99afc2db52a642500949a4937a78fea42010f13825f08167f346033e)
also reached PID 1 and a 30-second heartbeat on the physical phone. Its pstore
reported qcom_scm convention smc arm 64, the PID 1 start, the planned reboot
at about 92 seconds and the SCM device shutdown callback. Android was then
reachable again. This demonstrates the SCM probe in this isolated profile,
not every secure call or the later peripheral drivers.

## TLMM isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=tlmm tools/wayne/build-ram-probe.sh boot

This stage uses separate out-6.12-ram-tlmm outputs and the matching 140-value
fragment. Relative to the SCM-stage .config, only PINCTRL_SDM660 changes from
n to y, besides the descriptive LOCALVERSION suffix. The board DTB retains
the GPIO 8-11 firmware ownership reservation already checked by inspect-dtb.
The kernel and diagnostic userspace use the same unchanged RAM-only boot
parameters. The TLMM-stage image at
334a02c219b68e4fb3f0f29271258fe844186774 reached PID 1 on the physical
phone, and pstore recorded probe of 3100000.pinctrl returned 0, the PID 1
marker, and REBOOTING_AFTER_TEST at approximately 92 seconds. The phone
returned to original Android; some pstore bytes were corrupted. This tests
probe and continued RAM userspace, not the electrical behavior of every GPIO.

## RPM transport isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=rpm tools/wayne/build-ram-probe.sh boot

This stage restores MAILBOX, the Qualcomm APCS IPC mailbox, GLINK RPM and
QCOM_SMD_RPM transport. It has 146 checked requirements and separate
out-6.12-ram-rpm outputs. QCOM_SMEM, RPM clock/regulator/power-domain clients,
GCC, USB and storage remain disabled. Merely registering the transport does
not establish the correctness of later voltage/frequency votes.

The APCS IPC driver calls devm_regmap_init_mmio, but the v6.12 Kconfig option
did not select REGMAP_MMIO. In this minimal profile, olddefconfig dropped
both requested REGMAP and REGMAP_MMIO before the fix. Adding the missing
REGMAP_MMIO selection makes all 146 requirements survive. This dependency
fix applies only when QCOM_APCS_IPC is selected and adds no unrelated hardware
driver. The RPM-stage image at
fc113cbbf966bf6d31dd632fdcdd942f4e8fbad9 reached PID 1 and a 30-second
heartbeat on the physical phone. Pstore recorded successful probes of the
APCS mailbox, GLINK RPM edge and rpm_requests endpoint, plus PID 1 and the
planned reboot at about 92 seconds. This tests transport registration and
channel setup; it does not test the later clock/regulator requests.

## RPM clock isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=rpmclk tools/wayne/build-ram-probe.sh boot

This stage adds QCOM_CLK_SMD_RPM and the generic Qualcomm clock/interconnect/
reset dependencies in a 151-value checked fragment, using separate
out-6.12-ram-rpmclk outputs. GCC, regulator and power-domain clients remain
disabled. The new ram-clocks packer profile retains all embedded-initramfs
and framebuffer checks, additionally requires the RPM clock driver, and adds
clk_ignore_unused to the prior RAM-probe command line. This deliberately
retains firmware clocks while the RPM clock provider is first handed over.
Unlike the previous SCM/TLMM steps, this stage changes that command-line
parameter as well as the stated clock configuration. Physical validation
of the RPM-clock stage passed on the physical phone at commit
ae4fd62728bc12971b73d7ccd295b2273d126bea: the screen showed BRINGUP_READY,
and pstore recorded the RPM clock-controller probe returning 0, the expected
Not disabling unused clocks message, a later PID 1 heartbeat, and the planned
reboot at about 92 seconds. The phone returned to original Android. Some
pstore bytes were corrupted, so only intact markers are treated as evidence.

## GCC isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=gcc tools/wayne/build-ram-probe.sh boot

This stage adds SDM_GCC_660 and its QCOM_GDSC support to the RPM-clock
configuration. All 154 requested requirements are checked. The ram-gcc
packer profile additionally requires these drivers and generic power domains,
and appends pd_ignore_unused to the existing retained-clock command line.
The phase preserves unused clock and power-domain state while GCC is first
registered; regulator, storage, USB and multimedia clock drivers remain off.
Its outputs use out-6.12-ram-gcc. GCC-stage physical validation is pending.

## VS Code

Open this native checkout through VS Code Remote SSH, then run the task
Wayne 6.12: RAM diagnostic boot.img (24 threads) and select the desired stage.
The default is the verified core stage; consult the phase-specific physical
results above before choosing another stage. Each stage uses its own output
directory, including when the standard tasks configure WAYNE_OUT. Existing
standard tasks also set JOBS=24. No task flashes or reboots a phone.
