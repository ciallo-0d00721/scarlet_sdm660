# Provisional Wayne firmware framebuffer console

This diagnostic extends the native Linux v6.12 USB-console candidate
`173a03812c5c43da0905b33fb30f65a682bac24b`. It uses upstream `simplefb` and
`fbcon` to attempt visible kernel logs on the display already configured by
the bootloader. It does not enable native MDP/DSI, panel, touch or GPU drivers.

## Evidence and assumptions

Wayne's vendor board reserves `0x9d400000..0x9f7ff000` for the framebuffer.
The running Android kernel reports 1080x2160, 32 bits per pixel, a 4352-byte
line stride, and a 1080x4320 virtual surface. These Android runtime values
do not establish the bootloader's actual scanout state.

The related [Mi A2 community 6.12 device tree](https://github.com/sdm660-mainline/linux/blob/ed75697b208799a14609e29a713208b3a9004cc7/arch/arm64/boot/dts/qcom/sdm660-xiaomi-jasmine.dts)
describes a bootloader surface at the same physical address with 1080x2160,
4320-byte lines and `a8r8g8b8` format. This diagnostic uses those provisional
bootloader parameters. Their suitability for Wayne is **not yet verified**;
Android's 4352-byte stride must not be mistaken for confirmed LK data.

The 9,331,200-byte mapped surface fits inside the existing 37,744,640-byte
`no-map` reservation. The DTB validator checks geometry, format, MDSS phandle
and the enclosing reservation. These are static consistency checks, not proof
that firmware actually scans out this surface. A retained or garbled image
remains inconclusive, and can require a separate stride/layout comparison.

## Behavior

`FB_SIMPLE`, `FRAMEBUFFER_CONSOLE`, `VT_CONSOLE` and the Terminus 16x32 font
are built in. Deferred framebuffer takeover is disabled for this diagnostic.
The boot command line adds `console=tty0 fbcon=font:TER16x32` while retaining
UART and USB serial consoles. The existing `clk_ignore_unused` and
`pd_ignore_unused` keep unclaimed bootloader resources from being shut down
by the usual unused-resource cleanup. No firmware clock or voltage settings
are inferred from display metadata.

The kernel and RAM init still come directly from the 6.12 source checkout.
The existing PID 1 marker, heartbeat and optional `wayne.reboot_after=N`
timer can appear in printk on every working console. That timer only runs
after init starts; it cannot recover a kernel stalled before PID 1.

## Validation boundaries

Build with `bash tools/wayne/build.sh boot`. The build checks the resolved
Wayne configuration, compiled DTB and Android boot image layout. A QEMU virt
smoke test can check simplefb console registration using a RAM-backed fake
surface and the same kernel/initramfs. QEMU cannot validate Wayne's display
hardware, scanout parameters, USB enumeration or Android compatibility.

Use only a separately recorded temporary `fastboot boot` trial for hardware
validation. An accepted image transfer is not proof of kernel execution.
No hardware success is claimed by this source change.
