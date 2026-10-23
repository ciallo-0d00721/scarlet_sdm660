# Xiaomi Mi 6X / wayne — native Linux 6.12

This branch contains the complete Linux 6.12 source tree. Its root Makefile is VERSION=6, PATCHLEVEL=12, SUBLEVEL=0. All build targets run directly in this checkout.

## Baseline provenance

The pristine root snapshot exactly matches the tree of upstream Linux v6.12:

- Upstream commit: `adc218676eef25575469234709c2d87185ca223a`.
- Upstream tree: `ac4266ccaf1cf79e8fb22ad3e0d86deac358ffb9`.
- Imported root snapshot: `9ad5152ccb039f8392c18d423f425b435d8e2c9c`.

The root snapshot has no parents. Upstream history was not imported; the complete source contents were imported without alteration. Wayne changes are commits after this snapshot. Provenance metadata is in `Documentation/wayne/upstream.json`.

## Build in Linux / VS Code Remote SSH

Ubuntu 24.04 tools: make, Clang/LLD/LLVM 18, AArch64 GCC, bc, bison, flex, libssl-dev, libelf-dev and Python 3. Use a case-sensitive Linux filesystem for the full kernel tree.

```bash
bash tools/wayne/build.sh config
bash tools/wayne/build.sh image
bash tools/wayne/build.sh boot
```

`WAYNE_OUT` defaults to `../out-6.12`; `JOBS` defaults to 4. Boot packaging needs the AOSP mkbootimg script described in `Documentation/wayne/boot.md`; set `WAYNE_MKBOOTIMG` if it is elsewhere. VS Code tasks are supplied in `.vscode/tasks.json`.

Wayne configuration is `arch/arm64/configs/wayne_defconfig`; `wayne.fragment` records 44 required bring-up settings. The device tree is `arch/arm64/boot/dts/qcom/sdm660-xiaomi-wayne.dts`.

## Current scope

This is early mainline bring-up. The DT describes early serial, power keys, eMMC supplies and reserved memory. A standalone static debug initramfs is available; Android userspace, display, touch, USB/ADB, camera, audio and radio integration remain incomplete. Native hardware boot and complete schema validation are still pending. QEMU testing of the generic kernel/initramfs does not establish Wayne compatibility.
