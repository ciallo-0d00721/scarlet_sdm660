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
Its outputs use out-6.12-ram-gcc. At commit
57d62638310da2b1ec012e2aade07d3b90c04baf, the physical phone displayed
BRINGUP_READY and a 30-second PID 1 heartbeat. Pstore recorded the GCC
clock-controller probe returning 0 and a later shutdown callback around
92 seconds, after which original Android was reachable. Some pstore bytes
were corrupted; a clean REBOOTING_AFTER_TEST string was not recovered in
this trial, so the directly observed evidence is readiness, heartbeat,
successful GCC probe, shutdown and return to Android.

## VS Code

Open this native checkout through VS Code Remote SSH, then run the task
Wayne 6.12: RAM diagnostic boot.img (24 threads) and select the desired stage.
The default is the verified core stage; consult the phase-specific physical
results above before choosing another stage. Each stage uses its own output
directory, including when the standard tasks configure WAYNE_OUT. Existing
standard tasks also set JOBS=24. No task flashes or reboots a phone.

## RPM regulator isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=rails tools/wayne/build-ram-probe.sh boot

This stage adds the regulator core, fixed voltage provider and QCOM_SMD_RPM
regulator driver to the GCC stage in a 155-value checked fragment. The board
DTB, diagnostic initramfs and requested regulator constraints remain unchanged.
The PM660 L13 and PM660L L1 voltage ranges were cross-checked against the
Wayne vendor regulator data before enabling the provider.

The ram-rails packer profile requires those regulator drivers and appends
regulator_ignore_unused to the retained-clock/domain command line. This
prevents unused-rail cleanup but deliberately does not bypass regulator
registration or its initial constraints. SPMI, storage and USB remain off.
Outputs use out-6.12-ram-rails.

Physical validation passed on 2026-11-05 at commit
8525e8612111187bdad6a019ac8d0a38b9e5de3c. The 64 MiB image (SHA-256
f72be6c0ca1fb0632c1a91adcb121077993778dffcc7f1413d84e3bba8d0537b) and its
compact form (fd97e949562af32a6d600b793ebe8746503a4f0aed33b210718976f1900ba6dd)
reached PID 1 on the physical phone. Pstore recorded both RPM regulator groups
probing successfully (regulators-0 and regulators-1 returned 0), the
"Not disabling unused regulators" message from regulator_ignore_unused, PID 1
heartbeats at about 32 and 62 seconds, REBOOTING_AFTER_TEST at about 92 seconds,
shutdown callbacks for both qcom_rpm_smd_regulator devices, and
"reboot: Restarting system". No panic, SError or failed probe was present.
The phone returned to the original Android system; boot and recovery hashes
still matched their read-only backups. This establishes provider registration
and initial constraints in the RAM-only profile. The eMMC and USB consumers
remain disabled, so it does not establish regulator behavior under load.

## RPM power domain isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=rpmpd tools/wayne/build-ram-probe.sh boot

This stage adds QCOM_RPMPD to the rails configuration in a 156-value checked
fragment, using separate out-6.12-ram-rpmpd outputs. It keeps the board DTB,
diagnostic initramfs and the retained-clock/domain/rail command line unchanged.
The provider registers the SDM660 CX and MX RPM power domains, but no eMMC, USB,
display or multimedia consumer is enabled. The driver's boot-time sync_state
walks every RPM power domain; on this platform that write path hangs before
the pstore console is available. This branch therefore keeps provider
registration and OPP parsing but makes the sync_state callback a no-op.
This is a bring-up workaround, not a claim that the RPM power-domain votes
work. The driver's genpd registration parses its OPP table, so QCOM_RPMPD
now selects PM_OPP; without that selection dev_pm_opp_of_add_table_indexed()
is a stub returning -EOPNOTSUPP and the probe fails.

Physical validation passed on 2026-11-06 at commit
cd3f8e67cb3630ea2f8b5c927b55d69a9d882526. The 64 MiB image (SHA-256
c7780ec2df15d8a5f2693300e26d5c2419ddd024f6d9b90322aade4d2d2c1045) and its
compact form (b423e677ad0d6a4eb951597daa5fe2f8a0f6e8edf88a5223487a31f8de556aae)
reached PID 1 on the physical phone. Pstore recorded probe of
power-controller returning 0, PID 1 heartbeats at about 32, 62 and 92
seconds, REBOOTING_AFTER_TEST at about 92 seconds, the qcom-rpmpd shutdown
callback and "reboot: Restarting system". No panic, SError or failed probe
was present. The phone returned to the original Android system; boot and
recovery hashes still matched their read-only backups. This establishes
provider registration and OPP parsing in the RAM-only profile. Because
sync_state is a no-op in this branch, it does not establish boot-time RPM
power-domain votes.

## Interconnect provider isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=interconnect tools/wayne/build-ram-probe.sh boot

This stage adds the Qualcomm interconnect core, the SDM660 NoC driver and the
SMD RPM interconnect provider to the rpmpd configuration in a 159-value
checked fragment. It registers the CNOC/SNOC/A2NOC/MNOC/GNOC/BIMC providers
with no MMC, USB or display consumers; the sdhc_1 and USB interconnect
references stay unbound.

Physical validation passed with one deferred provider on 2026-11-06 at
commit 6a393fa8d86d5cce9d3da6c231e8bcfce0ba5cad. The 64 MiB image
(SHA-256 c71e79c197a77e7052f58075d02ae23f4fd5b7f2321d6dbfc320ba91a6acef44)
and its compact form
(3f57b4a5769bf97abfc3f812f2c9c7d2ed0c9f59bdab0dd6f3dac01070933c13)
reached PID 1 on the physical phone. Pstore recorded the qnoc-sdm660
deferred-probe retry: bimc, cnoc, snoc, a2noc and gnoc returned 0, while
mnoc returned -EPROBE_DEFER because its mmcc AHB clock provider is not
enabled in this profile. icc_smd_rpm probed successfully. No MMC, USB or
display consumer was enabled, so no interconnect bandwidth vote was
exercised. The sdhc_1 path's a2noc/gnoc/bimc/cnoc providers are bound;
mnoc remains deferred for a later MMCC stage. Boot and recovery hashes
still matched their read-only backups, and the phone returned to the
original Android system. Some pstore bytes were corrupted.

## eMMC card-detection isolation stage

    JOBS=24 WAYNE_RAM_PROBE_STAGE=emmc tools/wayne/build-ram-probe.sh boot

This stage adds MMC, SDHCI, the SDHCI platform helper and SDHCI MSM to the
interconnect configuration in a 164-value checked fragment. CONFIG_BLOCK and
MMC_BLOCK remain disabled, so no /dev/mmcblk device or partition scan is
created; the driver still initialises the non-removable eMMC on sdhc_1 and may
read CID/CSD/EXT_CSD. The L4/L8 rails and the a2noc/gnoc/bimc/cnoc
interconnects are the first real consumers. Physical validation is pending.
