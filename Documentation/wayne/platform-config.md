# Wayne platform configuration diagnostic

The original Wayne configuration inherited upstream's multi-platform ARM64
defconfig. This diagnostic retains `ARCH_QCOM` for the phone and
`ARCH_VEXPRESS` for QEMU virt validation, and disables the other platform
selectors declared in v6.12 `arch/arm64/Kconfig.platforms`. `NR_CPUS` is eight.
PL011 and its console remain built in for the QEMU smoke test.

`wayne-platforms.fragment` records the requested values. They are also included
in `wayne.fragment`, so the existing build helper checks the resolved values
after `olddefconfig`. The defconfig was regenerated using Kconfig; derived
driver dependencies are allowed to change. All 117 requested values survived
the initial resolution.

The board DT, regulator settings, USB gadget, watchdog node, diagnostic init,
boot layout and default command line are unchanged from source
`3a3d4145b57302f2723a8b700d6de3fd4a48fa2d`. Thirteen CPU power-management and
core-driver values were independently compared with a separately resolved
parent configuration and matched. In particular, this change does not disable
CPU idle or CPU frequency scaling.

Reducing unrelated platform code makes subsequent board bring-up easier to
inspect. It is not evidence that any removed platform caused the earlier
stall, and it is not a validated fix for the observed charger-screen returns.
Build and QEMU checks must pass before a separately recorded phone trial.
QEMU cannot validate Wayne's firmware, display scanout, USB or Android.
