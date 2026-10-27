#!/usr/bin/env python3
"""Validate DTB structure and the initial Wayne board properties without libfdt."""
import argparse
import hashlib
import json
import pathlib
import struct


def inspect(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 40:
        raise ValueError("Truncated DTB header")
    magic, total, off_struct, off_strings, off_reserve, version, last, cpu, strings_len, struct_len = struct.unpack(
        ">10I", data[:40]
    )
    if magic != 0xD00DFEED or total != len(data) or version != 17:
        raise ValueError("Invalid magic, total size or DTB version")
    if not (40 <= off_reserve < off_struct < off_strings <= total):
        raise ValueError("Invalid DTB section offsets")
    if off_struct + struct_len != off_strings or off_strings + strings_len > total:
        raise ValueError("DTB section sizes exceed file bounds")
    position, end = off_struct, off_struct + struct_len
    nodes, stack = {}, []
    while position < end:
        if position + 4 > end:
            raise ValueError("Truncated structure token")
        token = struct.unpack_from(">I", data, position)[0]
        position += 4
        if token == 1:
            terminator = data.index(b"\0", position, end)
            stack.append(data[position:terminator].decode("ascii"))
            position = (terminator + 4) & ~3
            node = "/" + "/".join(stack[1:])
            if node in nodes:
                raise ValueError(f"Duplicate node {node}")
            nodes[node] = {}
        elif token == 2:
            if not stack:
                raise ValueError("Unbalanced end-node")
            stack.pop()
        elif token == 3:
            if not stack or position + 8 > end:
                raise ValueError("Invalid property token")
            length, nameoff = struct.unpack_from(">2I", data, position)
            position += 8
            if nameoff >= strings_len or position + length > end:
                raise ValueError("Property exceeds section bounds")
            start = off_strings + nameoff
            terminator = data.index(b"\0", start, off_strings + strings_len)
            name = data[start:terminator].decode("ascii")
            props = nodes["/" + "/".join(stack[1:])]
            if name in props:
                raise ValueError(f"Duplicate property {name}")
            props[name] = data[position:position + length]
            position = (position + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack or position != end:
                raise ValueError("Invalid end-of-tree")
            break
        else:
            raise ValueError(f"Unknown structure token {token}")
    else:
        raise ValueError("Missing end-of-tree token")
    # Check the complete inherited reservation map, not only selected board values.
    reserved = nodes.get("/reserved-memory", {})
    if reserved.get("#address-cells") != struct.pack(">I", 2) or \
            reserved.get("#size-cells") != struct.pack(">I", 2):
        raise ValueError("Expected 64-bit reserved-memory address and size cells")
    ranges = []
    for node, props in nodes.items():
        if not node.startswith("/reserved-memory/") or node.count("/") != 2 or "reg" not in props:
            continue
        if props.get("status", b"okay\0") not in (b"okay\0", b"ok\0"):
            continue
        reg = props["reg"]
        if not reg or len(reg) % 16:
            raise ValueError(f"Invalid reserved-memory reg: {node}")
        for offset in range(0, len(reg), 16):
            address, size = struct.unpack_from(">QQ", reg, offset)
            if not size or address + size > 1 << 64:
                raise ValueError(f"Invalid reserved-memory range: {node}")
            ranges.append((address, address + size, node))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise ValueError(f"Reserved-memory overlap: {previous[2]} and {current[2]}")
    expected = {
        ("/", "compatible"): b"xiaomi,wayne\0qcom,sdm660\0",
        ("/", "qcom,msm-id"): struct.pack(">2I", 317, 0),
        ("/chosen", "stdout-path"): b"serial0:115200n8\0",
        ("/soc@0/pinctrl@3100000", "gpio-reserved-ranges"): struct.pack(">2I", 8, 4),
        ("/soc@0/watchdog@17817000", "compatible"): b"qcom,apss-wdt-sdm660\0qcom,kpss-wdt\0",
        ("/soc@0/watchdog@17817000", "reg"): struct.pack(">2I", 0x17817000, 0x1000),
        ("/soc@0/watchdog@17817000", "clock-names"): b"sleep\0",
        ("/soc@0/phy@c012000", "status"): b"okay\0",
        ("/soc@0/usb@a8f8800", "status"): b"okay\0",
        ("/soc@0/usb@a8f8800", "qcom,select-utmi-as-pipe-clk"): b"",
        ("/soc@0/usb@a8f8800/usb@a800000", "dr_mode"): b"peripheral\0",
        ("/soc@0/usb@a8f8800/usb@a800000", "maximum-speed"): b"high-speed\0",
        ("/soc@0/usb@a8f8800/usb@a800000", "phy-names"): b"usb2-phy\0",
        ("/soc@0/mmc@c0c4000", "status"): b"okay\0",
        ("/reserved-memory/ramoops@a0000000", "reg"): struct.pack(">4I", 0, 0xa0000000, 0, 0x400000),
        ("/reserved-memory/tzbuffer@94a00000", "reg"): struct.pack(">4I", 0, 0x94a00000, 0, 0x600000),
    }
    for (node, prop), value in expected.items():
        if nodes.get(node, {}).get(prop) != value:
            raise ValueError(f"Unexpected Wayne property: {node}:{prop}")
    sleep = nodes.get("/clocks/sleep-clk", {})
    clock = nodes["/soc@0/watchdog@17817000"].get("clocks", b"")
    if len(clock) != 4 or sleep.get("compatible") != b"fixed-clock\0" or \
            clock != sleep.get("phandle"):
        raise ValueError("Watchdog must use the inherited fixed sleep clock")
    providers = {}
    for node, props in nodes.items():
        for name in ("pm660l", "pm660"):
            if props.get("compatible") == f"qcom,rpm-{name}-regulators\0".encode():
                if name in providers:
                    raise ValueError(f"Duplicate {name} regulator provider")
                providers[name] = node
    if set(providers) != {"pm660l", "pm660"}:
        raise ValueError("Missing USB regulator providers")
    phy = nodes["/soc@0/phy@c012000"]
    for supply, provider, rail in (("vdd-supply", "pm660l", "l1"),
                                    ("vdda-pll-supply", "pm660", "l10"),
                                    ("vdda-phy-dpdm-supply", "pm660l", "l7")):
        rail_props = nodes.get(providers[provider] + "/" + rail, {})
        reference = phy.get(supply, b"")
        if len(reference) != 4 or reference != rail_props.get("phandle"):
            raise ValueError(f"Wrong USB PHY rail: {supply}")
    usb = nodes["/soc@0/usb@a8f8800/usb@a800000"]
    if usb.get("status", b"okay\0") not in (b"okay\0", b"ok\0"):
        raise ValueError("USB peripheral node is disabled")
    if len(usb.get("phys", b"")) != 4 or usb["phys"] != phy.get("phandle"):
        raise ValueError("USB peripheral must use only the primary USB2 PHY")
    if "extcon" in usb or "usb-role-switch" in usb:
        raise ValueError("This diagnostic uses fixed peripheral mode")
    for provider, rail, low, high in (("pm660l", "l1", 800000, 925000),
                                       ("pm660", "l10", 1800000, 1800000),
                                       ("pm660l", "l7", 2700000, 3125000),
                                       ("pm660l", "s2", 1050000, 1050000)):
        props = nodes.get(providers[provider] + "/" + rail, {})
        if props.get("regulator-min-microvolt") != struct.pack(">I", low) or \
                props.get("regulator-max-microvolt") != struct.pack(">I", high):
            raise ValueError(f"Unexpected USB regulator voltage: {provider}/{rail}")
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": total,
            "nodes": len(nodes), "board_checks_passed": len(expected),
            "static_reserved_ranges_checked": len(ranges),
            "watchdog_clock_reference_checked": True,
            "usb_phy_and_supply_references_checked": True,
            "validation_scope": "dtb structure, board properties and static reservations",
            "boot_tested": False, "schema_validated": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()
    result = json.dumps(inspect(arguments.dtb), indent=2) + "\n"
    if arguments.output:
        arguments.output.write_text(result, encoding="utf-8")
    print(result, end="")
