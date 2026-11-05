# Wayne RAM-only diagnostic profile

This profile uses the genuine upstream Linux v6.12 baseline recorded in upstream.json. It is a bring-up experiment, not an Android boot image suitable for daily use.

The framebuffer-console tests proved readable kernel text on the physical Mi 6X, but did not verify PID 1. The later QCOM-only platform configuration also returned to the charger screen. Its QEMU success did not establish phone boot success. Skipping the Qualcomm watchdog probe in one earlier header comparison automatically returned to the original Android desktop; the cause remains unknown, and no PID 1 marker was observed.

The RAM probe removes the specific PMIC/RPM/SCM, SoC clock, pinctrl, storage, USB, remote processor, watchdog, CPU idle and CPU frequency drivers from the diagnostic kernel. Generic PM, input and reset cores remain selected by dependencies. It preserves the existing board DTB and firmware framebuffer mapping. USB enumeration is not expected for this profile. PL011 is retained for the QEMU virt test; it does not bind the Wayne MSM UART.

A static diagnostic /init is embedded as an uncompressed cpio inside the kernel. A duplicate external gzip ramdisk remains in the Android boot image for bootloader compatibility. The packer verifies the embedded archive against the freshly generated init and against the uncompressed ARM64 Image. The independent QEMU check must omit -initrd to show that /init is really supplied by the kernel.

Build on Linux with Clang/LLD 18 and AArch64 GCC static libc:

    JOBS=24 tools/wayne/build-ram-probe.sh boot

The default output directory is separate from the ordinary profile. The built-in cpio input path is relative to the build directory. The new profile uses maxcpus=1 and a framebuffer console; its diagnostic init asks for a reboot after 90 seconds only if PID 1 is reached. A charger-screen transition alone does not prove that timer ran.

Validation status is recorded separately from implementation: compilation, image/header validation and QEMU are prerequisites for a temporary physical fastboot boot trial, not evidence of Android support. Never flash this experiment based on these checks alone.
