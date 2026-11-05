#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Test the verified payload inside a packed Wayne image on QEMU virt, not phone USB."""
import argparse
import datetime
import gzip
import hashlib
import importlib.util
import json
import pathlib
import shlex
import struct
import subprocess
import zlib


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verified_payload(boot, expected_source, expected_profile, packer):
    meta = json.loads((boot / 'boot-validation.json').read_text())
    if meta['source_commit'] != expected_source or meta['profile'] != expected_profile:
        raise ValueError('Artifact source/profile does not match the pinned expectation')
    payload = (boot / 'Image.gz-dtb').read_bytes()
    ramdisk = (boot / 'ramdisk.cpio.gz').read_bytes()
    compact = (boot / 'boot-compact.img').read_bytes()
    for data, key in ((payload, 'kernel_payload_sha256'), (ramdisk, 'ramdisk_sha256'),
                      (compact, 'compact_img_sha256')):
        if digest(data) != meta[key]:
            raise ValueError('Artifact hash mismatch: ' + key)
    if len(compact) != meta['compact_img_bytes']:
        raise ValueError('Compact image size mismatch')
    spec = importlib.util.spec_from_file_location('wayne_packer', packer)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for key, value in module.UPSTREAM.items():
        if meta[key] != value:
            raise ValueError('Unexpected native source baseline: ' + key)
    cmdline = meta['header']['cmdline']
    module.check_boot(compact, payload, ramdisk, cmdline)
    padded = boot / 'boot.img'
    if padded.exists():
        data = padded.read_bytes()
        if len(data) != meta['boot_img_bytes'] or digest(data) != meta['boot_img_sha256']:
            raise ValueError('Padded image hash/size mismatch')
        module.check_boot(data, payload, ramdisk, cmdline)
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    raw = decoder.decompress(payload) + decoder.flush()
    board = decoder.unused_data
    if not decoder.eof or raw[56:60] != b'ARM\x64':
        raise ValueError('Payload has no complete ARM64 gzip Image')
    if len(board) < 8 or struct.unpack_from('>II', board) != (0xd00dfeed, len(board)):
        raise ValueError('Appended board DTB is missing or malformed')
    if digest(board) != meta['dtb_validation']['sha256']:
        raise ValueError('Appended board DTB hash mismatch')
    if meta['kernel_release'].encode() not in raw:
        raise ValueError('Kernel release does not match the tested Image')
    cpio = gzip.decompress(ramdisk)
    if (not meta['embedded_initramfs_verified'] or not cpio or cpio not in raw
            or digest(cpio) != meta['embedded_initramfs_sha256']):
        raise ValueError('Embedded initramfs does not match the packed external duplicate')
    return meta, raw


def smoke(args):
    boot, output = args.boot_out.resolve(), args.output_dir.resolve()
    if boot == output:
        raise ValueError('Use a separate directory for QEMU outputs')
    output.mkdir(parents=True, exist_ok=True)
    meta, raw = verified_payload(boot, args.expected_source, args.expected_profile, args.packer)
    report = {'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'source_commit': meta['source_commit'], 'profile': meta['profile'],
              'compact_img_sha256': meta['compact_img_sha256'],
              'kernel_payload_sha256': meta['kernel_payload_sha256'],
              'raw_image_sha256': digest(raw), 'artifact_payload_verified': True,
              'external_initrd': False, 'expectation': args.expect,
              'scope': 'QEMU virt fake RAM framebuffer/PID1/timer/blacklists; '
                       'not phone USB, display scanout, eMMC or Android'}
    if args.verify_only:
        report['verification_only'] = True
        print(json.dumps(report, indent=2))
        return 0
    image = output / 'qemu-Image'
    image.write_bytes(raw)
    dtb = output / 'qemu-virt-fb.dtb'
    common = ['qemu-system-aarch64', '-cpu', 'max', '-smp', '1', '-m', '768',
              '-nographic', '-nic', 'none']
    subprocess.run(common + ['-machine', 'virt,dumpdtb=' + str(dtb)],
                   check=True, capture_output=True, timeout=15)

    def node(path):
        subprocess.run(['fdtput', '-c', str(dtb), path], check=True)

    def prop(path, name, values=(), kind='x'):
        subprocess.run(['fdtput', '-t', kind, str(dtb), path, name,
                        *map(str, values)], check=True)

    node('/reserved-memory')
    prop('/reserved-memory', '#address-cells', ['2'])
    prop('/reserved-memory', '#size-cells', ['2'])
    prop('/reserved-memory', 'ranges')
    node('/reserved-memory/framebuffer@60000000')
    prop('/reserved-memory/framebuffer@60000000', 'reg',
         ['0', '60000000', '0', hex(1080 * 2160 * 4)[2:]])
    prop('/reserved-memory/framebuffer@60000000', 'no-map')
    node('/test-display-controller')
    prop('/test-display-controller', 'compatible', ['wayne,qemu-test-display'], 's')
    prop('/test-display-controller', 'status', ['disabled'], 's')
    prop('/test-display-controller', 'phandle', ['70000001'])
    fb = '/chosen/framebuffer@60000000'
    prop('/chosen', '#address-cells', ['2'])
    prop('/chosen', '#size-cells', ['2'])
    prop('/chosen', 'ranges')
    node(fb)
    prop(fb, 'compatible', ['simple-framebuffer'], 's')
    prop(fb, 'reg', ['0', '60000000', '0', hex(1080 * 2160 * 4)[2:]])
    for name, value in [('width', 1080), ('height', 2160), ('stride', 1080 * 4)]:
        prop(fb, name, [hex(value)[2:]])
    prop(fb, 'format', ['a8r8g8b8'], 's')
    prop(fb, 'display', ['70000001'])
    tokens = shlex.split(meta['header']['cmdline'])
    if (args.expect == 'panic') != ('wayne.test_pid1_exit=1' in tokens):
        raise ValueError('The expected panic/timer outcome must match the pinned image opt-in')
    blacklists = [name for token in tokens if token.startswith('initcall_blacklist=')
                  for name in token.split('=', 1)[1].split(',') if name]
    tokens = [token for token in tokens
              if not token.startswith(('console=', 'earlycon=', 'wayne.reboot_after='))]
    tokens += ['console=tty0', 'console=ttyAMA0', 'wayne.reboot_after=30']
    command = common + ['-machine', 'virt', '-dtb', str(dtb), '-kernel', str(image),
                        '-append', ' '.join(tokens), '-no-reboot']
    log = output / 'qemu-smoke.log'
    try:
        with log.open('wb') as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=90)
        code = result.returncode
    except subprocess.TimeoutExpired:
        code = 124
    text = log.read_text(errors='replace')
    required = ['simplefb registered!', 'Console: switching to colour frame buffer device',
                'WAYNE_INIT_STARTED pid=1', 'BRINGUP_READY']
    if args.expect == 'panic':
        required += ['WAYNE_TEST_PID1_EXIT', 'Kernel panic - not syncing: Attempted to kill init!']
    else:
        required += ['AUTO_REBOOT_AFTER=30 seconds', 'REBOOTING_AFTER_TEST']
        if 'wayne.reboot_mode=bootloader' in tokens:
            required.append('REBOOT_TARGET=bootloader')
    if 'wayne.usb_deferred=1' in tokens:
        required += ['USB_DEFERRED_START', 'USB_DEFERRED_RESULT']
    markers = {key: key in text for key in required}
    confirmed = {name: ('initcall ' + name + ' blacklisted') in text for name in blacklists}
    report.update({'exit_code': code, 'markers': markers, 'blacklists': confirmed,
                   'passed': code == 0 and all(markers.values()) and all(confirmed.values())})
    (output / 'qemu-smoke.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-out', type=pathlib.Path, required=True)
    parser.add_argument('--output-dir', type=pathlib.Path, required=True)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--expected-profile', required=True)
    parser.add_argument('--packer', type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().with_name('make-boot.py'))
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--expect', choices=('timer', 'panic'), default='timer')
    raise SystemExit(smoke(parser.parse_args()))
