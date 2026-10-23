#!/usr/bin/env bash
# Build directly in this native Linux 6.12 checkout.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out=${WAYNE_OUT:-"$root/../out-6.12"}
boot_out=${WAYNE_BOOT_OUT:-"$root/../boot-wayne-6.12"}
mode=${1:-image}
jobs=${JOBS:-4}
export PATH="/usr/lib/llvm-18/bin:$PATH"
[[ $(uname -s) == Linux ]] || { echo 'Use Linux or VS Code Remote SSH.' >&2; exit 1; }
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'JOBS must be positive.' >&2; exit 1; }
case "$mode" in config|dtbs|image|boot) ;; *) echo "Unknown task: $mode" >&2; exit 1 ;; esac
mkdir -p "$out"
out=$(realpath "$out")
args=(-C "$root" O="$out" ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu-)
if [[ $mode == config || ! -f $out/.config ]]; then
    make "${args[@]}" wayne_defconfig
fi
make "${args[@]}" olddefconfig
python3 "$root/tools/wayne/check-config.py" "$out/.config" \
    "$root/arch/arm64/configs/wayne.fragment"
[[ $mode != config ]] || exit 0
make "${args[@]}" -j"$jobs" qcom/sdm660-xiaomi-wayne.dtb
python3 "$root/tools/wayne/inspect-dtb.py" \
    "$out/arch/arm64/boot/dts/qcom/sdm660-xiaomi-wayne.dtb" \
    --output "$out/wayne-dtb-validation.json"
[[ $mode != dtbs ]] || exit 0
make "${args[@]}" -j"$jobs" Image.gz
sha256sum "$out/arch/arm64/boot/Image.gz"
[[ $mode == boot ]] || exit 0
packer=${WAYNE_MKBOOTIMG:-"$root/../mkbootimg-aosp-android11.py"}
[[ -f $packer ]] || { echo 'Prepare AOSP mkbootimg; see Documentation/wayne/boot.md.' >&2; exit 1; }
python3 "$root/tools/wayne/make-boot.py" --source "$root" --build-out "$out" \
    --output "$boot_out" --mkbootimg "$packer"
