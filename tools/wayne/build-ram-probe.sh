#!/usr/bin/env bash
# Build the RAM-only Wayne diagnostic profile in this native Linux 6.12 checkout.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
stage=${WAYNE_RAM_PROBE_STAGE:-core}
boot_profile=ram-probe
case "$stage" in
    core) name=ram-probe; defconfig=wayne_ram_probe_defconfig ;;
    scm) name=ram-scm; defconfig=wayne_ram_scm_defconfig ;;
    tlmm) name=ram-tlmm; defconfig=wayne_ram_tlmm_defconfig ;;
    rpm) name=ram-rpm; defconfig=wayne_ram_rpm_defconfig ;;
    rpmclk) name=ram-rpmclk; defconfig=wayne_ram_rpmclk_defconfig; boot_profile=ram-clocks ;;
    gcc) name=ram-gcc; defconfig=wayne_ram_gcc_defconfig; boot_profile=ram-gcc ;;
    rails) name=ram-rails; defconfig=wayne_ram_rails_defconfig; boot_profile=ram-rails ;;
    rpmpd) name=ram-rpmpd; defconfig=wayne_ram_rpmpd_defconfig; boot_profile=ram-rpmpd ;;
    interconnect) name=ram-interconnect; defconfig=wayne_ram_interconnect_defconfig; boot_profile=ram-interconnect ;;
    emmc) name=ram-emmc; defconfig=wayne_ram_emmc_defconfig; boot_profile=ram-emmc ;;
    block) name=ram-block; defconfig=wayne_ram_block_defconfig; boot_profile=ram-block ;;
    fsprobe) name=ram-block; defconfig=wayne_ram_block_defconfig; boot_profile=ram-fsprobe ;;
    sysmount) name=ram-sysmount; defconfig=wayne_ram_sysmount_defconfig; boot_profile=ram-sysmount ;;
    mmcc) name=ram-mmcc; defconfig=wayne_ram_mmcc_defconfig; boot_profile=ram-mmcc ;;
    usb2) name=ram-usb2; defconfig=wayne_ram_usb2_defconfig; boot_profile=ram-usb2 ;;
    usb2nodrv) name=ram-usb2; defconfig=wayne_ram_usb2_defconfig; boot_profile=ram-usb2-nodrv ;;
    usb2nophy) name=ram-usb2; defconfig=wayne_ram_usb2_defconfig; boot_profile=ram-usb2-nophy ;;
    usb2nodwc3) name=ram-usb2; defconfig=wayne_ram_usb2_defconfig; boot_profile=ram-usb2-nodwc3 ;;
    usb2guardnodwc3) name=ram-usb2-guard; defconfig=wayne_ram_usb2_guard_defconfig
        boot_profile=ram-usb2-guard-nodwc3 ;;
    usb2guardcore) name=ram-usb2-guard; defconfig=wayne_ram_usb2_guard_defconfig
        boot_profile=ram-usb2-guard-core ;;
    usb2guardfull) name=ram-usb2-guard; defconfig=wayne_ram_usb2_guard_defconfig
        boot_profile=ram-usb2-guard-full ;;
    usb2guardwrapper) name=ram-usb2-guard; defconfig=wayne_ram_usb2_guard_defconfig
        boot_profile=ram-usb2-guard-wrapper ;;
    bootguard) name=ram-bootguard; defconfig=wayne_ram_bootguard_defconfig; boot_profile=ram-bootguard ;;
    bootguardwdt) name=ram-bootguard; defconfig=wayne_ram_bootguard_defconfig; boot_profile=ram-bootguard-wdt ;;
    bootguarddeadline) name=ram-bootguard; defconfig=wayne_ram_bootguard_defconfig
        boot_profile=ram-bootguard-deadline ;;
    bootguardpanic) name=ram-bootguard; defconfig=wayne_ram_bootguard_defconfig; boot_profile=ram-bootguard-panic ;;
    *) echo "Unknown RAM probe stage: $stage" >&2; exit 1 ;;
esac
out=${WAYNE_RAM_PROBE_OUT:-${WAYNE_OUT:-"$root/../out-6.12-$name"}}
boot_name="ram-$stage"
[[ $stage != core ]] || boot_name=ram-probe
boot_out=${WAYNE_RAM_PROBE_BOOT_OUT:-${WAYNE_BOOT_OUT:-"$root/../boot-wayne-6.12-$boot_name"}}
mode=${1:-image}
jobs=${JOBS:-24}
export PATH="/usr/lib/llvm-18/bin:$PATH"
[[ $(uname -s) == Linux ]] || { echo 'Use Linux or VS Code Remote SSH.' >&2; exit 1; }
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive.' >&2; exit 1; }
case "$mode" in config|dtbs|image|boot) ;; *) echo "Unknown task: $mode" >&2; exit 1 ;; esac
mkdir -p "$out"
out=$(realpath "$out")
boot_out=$(realpath -m "$boot_out")
[[ $out != "$boot_out" ]] || { echo 'The RAM probe build and boot outputs must be distinct.' >&2; exit 1; }
args=(-C "$root" O="$out" ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu-)
if [[ $mode == config || ! -f $out/.config ]]; then
    make "${args[@]}" "$defconfig"
fi
make "${args[@]}" olddefconfig
python3 "$root/tools/wayne/check-config.py" "$out/.config" \
    "$root/arch/arm64/configs/wayne-$name.fragment"
[[ $mode != config ]] || exit 0
make "${args[@]}" -j"$jobs" qcom/sdm660-xiaomi-wayne.dtb
python3 "$root/tools/wayne/inspect-dtb.py" \
    "$out/arch/arm64/boot/dts/qcom/sdm660-xiaomi-wayne.dtb" \
    --output "$out/wayne-dtb-validation.json"
[[ $mode != dtbs ]] || exit 0
# A .cpio CONFIG_INITRAMFS_SOURCE bypasses Kbuild's host tool rule, so build
# gen_init_cpio ourselves before generating the archive the Image will embed.
mkdir -p "$out/usr" "$out/ram-probe"
cc -O2 -o "$out/usr/gen_init_cpio" "$root/usr/gen_init_cpio.c"
python3 - "$root" "$out" <<'PY'
import importlib.util
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("wayne_make_boot", root / "tools/wayne/make-boot.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
_, _, cpio = module.build_ramdisk(out, out / "ram-probe")
archive = out / "ram-probe/initramfs.cpio"
if not archive.exists() or archive.read_bytes() != cpio:
    archive.write_bytes(cpio)
PY
make "${args[@]}" -j"$jobs" Image.gz
sha256sum "$out/arch/arm64/boot/Image.gz"
[[ $mode == boot ]] || exit 0
packer=${WAYNE_MKBOOTIMG:-"$root/../mkbootimg-aosp-android11.py"}
[[ -f $packer ]] || { echo 'Prepare AOSP mkbootimg; see Documentation/wayne/boot.md.' >&2; exit 1; }
python3 "$root/tools/wayne/make-boot.py" --source "$root" --build-out "$out" \
    --output "$boot_out" --mkbootimg "$packer" --profile "$boot_profile"
