# Standalone mainline boot image

The native build uses this source root and its Kbuild output directly. `make-boot.py` verifies the root Makefile version, pristine baseline tree and baseline ancestry before packaging. It records the actual source commit alongside the upstream tag identity.

The debug init is statically cross-compiled from `tools/wayne/debug-init.c`. It mounts RAM/pseudo filesystems, logs diagnostic information and stays alive as PID 1. It does not start Android or mount system/vendor/data. Display, touch and USB/ADB support is incomplete.

## Packaging tool

Download the AOSP `android-11.0.0_r1` mkbootimg script from:

<https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/tags/android-11.0.0_r1/mkbootimg.py?format=TEXT>

Base64-decode it to `../mkbootimg-aosp-android11.py` (or set WAYNE_MKBOOTIMG). The decoded file SHA256 must be:

```text
5579fb6bcb9e89e790a70fb9ccf3c00cf56e3e46aef74d84fcb0964887e5576e
```

Run `bash tools/wayne/build.sh boot` after configuring. Output defaults to `../boot-wayne-6.12/`: `boot.img` is 64 MiB with zero padding; `boot-compact.img` has the same sections without that padding. The AOSP tool, header, section, SHA1 ID, ELF and reserved-memory checks are described in `boot-validation.json` produced beside the images.

The experimental header uses v1, 4096-byte pages, kernel 0x80000000, tags 0x83e00000 and ramdisk 0x84000000. The geometry satisfies the compiled ARM64 Image's placement/size constraints, but Wayne bootloader loading and appended-DTB handling remain untested. No Android vendor compatibility is claimed.
