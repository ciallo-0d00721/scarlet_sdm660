#!/usr/bin/env python3
"""Build an unsigned Wayne bring-up boot.img with a standalone static init."""
import argparse
import gzip
import hashlib
import importlib.util
import io
import json
import pathlib
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
CMDLINE = ("earlycon=msm_serial_dm,0xc170000 console=ttyMSM0,115200n8 "
           "rdinit=/init loglevel=8 ignore_loglevel initcall_debug printk.devkmsg=on nokaslr "
           "clk_ignore_unused pd_ignore_unused panic=0")
BASE = 0x80000000
RAMDISK_ADDRESS = 0x84000000
TAGS_ADDRESS = 0x83E00000
PAGE = 4096
PARTITION_SIZE = 0x04000000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def check_boot(data, kernel, ramdisk):
    if data[:8] != b"ANDROID!":
        raise ValueError("Invalid Android boot magic")
    fields = struct.unpack_from("<10I", data, 8)
    ks, ka, rs, ra, ss, sa, ta, page, version, os_version = fields
    if (ks, ka, rs, ra, ss, ta, page, version, os_version) != (
            len(kernel), BASE, len(ramdisk), RAMDISK_ADDRESS, 0,
            TAGS_ADDRESS, PAGE, 1, 0):
        raise ValueError("Unexpected boot header values")
    if struct.unpack_from("<IQI", data, 1632) != (0, 0, 1648):
        raise ValueError("Unexpected v1 extended header")
    command = (data[64:576].split(b"\0", 1)[0] +
               data[608:1632].split(b"\0", 1)[0]).decode()
    if command != CMDLINE:
        raise ValueError("Unexpected kernel command line")
    ramdisk_offset = (PAGE + ks + PAGE - 1) // PAGE * PAGE
    end = (ramdisk_offset + rs + PAGE - 1) // PAGE * PAGE
    if len(data) not in (end, PARTITION_SIZE) or len(data) > PARTITION_SIZE:
        raise ValueError("Invalid image size or partition overflow")
    if any(data[end:]):
        raise ValueError("Unexpected nonzero data after the boot sections")
    if data[PAGE:PAGE + ks] != kernel or data[ramdisk_offset:ramdisk_offset + rs] != ramdisk:
        raise ValueError("Boot sections differ from their inputs")
    sha1 = hashlib.sha1()
    for part in (kernel, ramdisk, b"", b""):
        sha1.update(part)
        sha1.update(struct.pack("<I", len(part)))
    if data[576:608] != sha1.digest() + bytes(12):
        raise ValueError("Boot header image ID mismatch")
    return {"header_version": version, "page_size": page,
            "kernel_address": hex(ka), "ramdisk_address": hex(ra),
            "tags_address": hex(ta), "cmdline": command,
            "sha1_id_verified": True, "sections_verified": True,
            "zero_padding_verified": True}


def build(work, output, mkbootimg):
    output.mkdir(parents=True, exist_ok=True)
    out = work / "out-6.12"
    source = work / "linux-6.12"
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if commit != "adc218676eef25575469234709c2d87185ca223a":
        raise ValueError("Expected the pinned upstream Linux v6.12 source")
    config = (out / ".config").read_text()
    for symbol in ("BLK_DEV_INITRD", "RD_GZIP", "BINFMT_ELF", "DEVTMPFS", "PROC_FS", "SYSFS",
                   "TMPFS", "SERIAL_MSM_CONSOLE", "SERIAL_EARLYCON", "PSTORE_RAM"):
        if f"CONFIG_{symbol}=y" not in config.splitlines():
            raise ValueError(f"Missing built-in requirement: {symbol}")
    image_gz = (out / "arch/arm64/boot/Image.gz").read_bytes()
    raw = gzip.decompress(image_gz)
    if raw[56:60] != b"ARM\x64":
        raise ValueError("Not an ARM64 Image")
    text_offset, image_size = struct.unpack_from("<QQ", raw, 8)
    if text_offset != 0 or not image_size or BASE + image_size > TAGS_ADDRESS:
        raise ValueError("Kernel placement needs review; would overlap the DTB window")
    dtb_path = out / "arch/arm64/boot/dts/qcom/sdm660-xiaomi-wayne.dtb"
    spec = importlib.util.spec_from_file_location("wayne_dtb", HERE / "inspect-dtb.py")
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    dtb_result = validator.inspect(dtb_path)
    dtb = dtb_path.read_bytes()
    if len(dtb) > RAMDISK_ADDRESS - TAGS_ADDRESS:
        raise ValueError("DTB overlaps ramdisk window")
    kernel = image_gz + dtb
    if BASE + len(kernel) > TAGS_ADDRESS:
        raise ValueError("Compressed kernel payload overlaps DTB window")
    kernel_path = output / "Image.gz-dtb"
    kernel_path.write_bytes(kernel)
    init = output / "init"
    subprocess.run(["aarch64-linux-gnu-gcc", "-static", "-Os", "-std=c11",
                    "-Wall", "-Wextra", "-Werror", "-Wl,--build-id=none",
                    "-o", str(init), str(HERE / "debug-init.c")], check=True)
    init_data = init.read_bytes()
    if init_data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", init_data, 18)[0] != 183:
        raise ValueError("Init is not an AArch64 ELF")
    phoff, = struct.unpack_from("<Q", init_data, 32)
    phsize, phnum = struct.unpack_from("<HH", init_data, 54)
    if any(struct.unpack_from("<I", init_data, phoff + i * phsize)[0] == 3 for i in range(phnum)):
        raise ValueError("Init unexpectedly requires a dynamic loader")
    if any(char.isspace() for char in str(init)):
        raise ValueError("gen_init_cpio input paths must not contain whitespace")
    entries = [f"dir /{name} 0755 0 0" for name in ("dev", "proc", "sys", "run", "tmp")]
    entries += ["nod /dev/console 0600 0 0 c 5 1", "nod /dev/null 0666 0 0 c 1 3",
                f"file /init {init} 0755 0 0"]
    listing = output / "initramfs.list"
    listing.write_text("\n".join(entries) + "\n")
    cpio = subprocess.check_output([str(out / "usr/gen_init_cpio"), "-t", "0", str(listing)])
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", filename="", mtime=0) as archive:
        archive.write(cpio)
    ramdisk = buffer.getvalue()
    if RAMDISK_ADDRESS + len(ramdisk) >= 0x85600000:
        raise ValueError("Ramdisk would overlap the first reserved-memory window")
    ramdisk_path = output / "ramdisk.cpio.gz"
    ramdisk_path.write_bytes(ramdisk)
    compact = output / "boot-compact.img"
    boot = output / "boot.img"
    packer = [sys.executable, str(mkbootimg)] if mkbootimg.endswith(".py") else [mkbootimg]
    subprocess.run(packer + ["--kernel", str(kernel_path), "--ramdisk", str(ramdisk_path),
                    "--header_version", "1", "--pagesize", str(PAGE), "--base", hex(BASE),
                    "--kernel_offset", "0", "--ramdisk_offset", hex(RAMDISK_ADDRESS - BASE),
                    "--tags_offset", hex(TAGS_ADDRESS - BASE), "--board", "wayne-mainline",
                    "--cmdline", CMDLINE, "-o", str(compact)], check=True)
    compact_data = compact.read_bytes()
    check_boot(compact_data, kernel, ramdisk)
    boot.write_bytes(compact_data + bytes(PARTITION_SIZE - len(compact_data)))
    data = boot.read_bytes()
    header = check_boot(data, kernel, ramdisk)
    report = {"purpose": "standalone mainline bring-up; not Android userspace",
              "upstream_commit": commit, "kernel_release": (out / "include/config/kernel.release").read_text().strip(),
              "boot_img_bytes": len(data), "boot_img_sha256": digest(data),
              "compact_img_bytes": len(compact_data), "compact_img_sha256": digest(compact_data),
              "kernel_payload_sha256": digest(kernel), "ramdisk_sha256": digest(ramdisk),
              "init_sha256": digest(init_data), "init_statically_linked": True,
              "mkbootimg_sha256": digest(pathlib.Path(mkbootimg).read_bytes()) if pathlib.Path(mkbootimg).is_file() else None,
              "header": header, "dtb_validation": dtb_result,
              "device_boot_tested": False, "android14_vendor_compatible": False,
              "signed": False, "qemu_smoke_tested": False,
              "address_note": "ARM64-aligned RAM placement; Wayne bootloader handling remains untested"}
    (output / "boot-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-root", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--mkbootimg", default="mkbootimg")
    args = parser.parse_args()
    build(args.work_root.resolve(), args.output.resolve(), args.mkbootimg)
