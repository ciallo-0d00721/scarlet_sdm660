# Experimental Wayne USB2 serial console

This diagnostic enables the primary USB2 PHY and DWC3 peripheral with the
three board supply rails: PM660L L1, PM660 L10, and PM660L L7. The 1.8 V PLL
supply is explicit. It does not guess an ID GPIO, extcon, Type-C or charging
policy, and does not enable the SuperSpeed PHY.

The PHY, controller, legacy CDC-ACM serial gadget and ttyGS0 console are built
into the kernel. The standalone initramfs does not load modules. The boot
command line keeps the UART and adds ttyGS0 plus a 1 MiB printk buffer. The
Linux serial gadget defaults to VID 0525 / PID a4a7; its appearance and console
output must be tested on the phone. It is not Android ADB.

The Wayne diagnostic config disables Layerscape/DPAA selection, which is not
hardware on this Qualcomm board. That removes unrelated portal initcalls;
their role in the earlier stall has not been proved. The watchdog candidate
is retained without assuming it explains that stall.

For a bounded test, append `wayne.reboot_after=60`. Once debug init reaches
PID 1, it logs its readiness, waits at least the requested 30..300 seconds,
then requests reboot. The timer is disabled by default and does not recover
a kernel that fails before init. `--self-test` checks timer parsing without
mounting or rebooting anything. No system, vendor or data partition is mounted.

Real USB enumeration, serial output, reboot and Android compatibility remain
unverified until a recorded device test establishes each of them.
