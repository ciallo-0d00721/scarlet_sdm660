# Untested watchdog takeover diagnostic

This branch follows the GPIO reservation fix at accd63f8331d. It is a
diagnostic candidate, not a confirmed fix for the remaining early-boot stall.

The downstream Wayne watchdog resource is 0x17817000, size 0x1000. Its
reset, enable, status, bark and bite offsets are 0x04, 0x08, 0x0c, 0x10 and
0x14. They match the upstream qcom,kpss-wdt layout. The new compatible uses
that fallback and the inherited fixed sleep clock.

Build the watchdog into the standalone kernel: its ramdisk loads no modules.
WATCHDOG_HANDLE_BOOT_ENABLED lets the watchdog core keep an already armed
device alive until userspace takes over. The upstream driver checks whether
the hardware is running before restarting it; a stopped watchdog is not
deliberately armed by this diagnostic. WATCHDOG_OPEN_TIMEOUT remains zero.

The optional bark IRQ is omitted for this first takeover diagnostic. Neither
the hardware's running state nor its role in the observed stall has been
established. Matching register offsets and a successful build do not prove
hardware compatibility or a working boot.

This candidate keeps the default SMP setup, FSL driver configuration, pstore
layout and ramdisk unchanged. It does not use the temporary initcall blacklist.
