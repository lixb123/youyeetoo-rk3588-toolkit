#!/usr/bin/env python3
"""Run offline or UART-controlled live Youyeetoo Ethernet validation."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import importlib.util
import json
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

from protocol_codec import decode_application, decode_ethernet_ipv4, decode_ipoc_core
from random_vectors import generate

ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parent.parent
UART_PROGRAM = REPO_ROOT / "tools" / "rk-eth-uart" / "rk_eth_uart.py"
ETH_SIM = REPO_ROOT / "tools" / "eth-sim"
PAYLOAD_NEXT_HOP = "10.240.1.34"


def load_serial_port():
    spec = importlib.util.spec_from_file_location("rk_eth_uart_existing", UART_PROGRAM)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load existing UART program: {UART_PROGRAM}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.SerialPort


def powershell_json(command: str):
    completed = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command + " | ConvertTo-Json -Depth 4 -Compress"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20,
    )
    return {"returncode": completed.returncode, "stdout": completed.stdout.strip(),
            "stderr": completed.stderr.strip()}


def has_payload_route(target_ip: str) -> bool:
    check = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         f"$r=Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '{target_ip}/32' -ErrorAction SilentlyContinue | "
         f"Where-Object {{$_.InterfaceAlias -eq '以太网 3' -and $_.NextHop -eq '{PAYLOAD_NEXT_HOP}'}}; "
         "if ($r) { exit 0 } else { exit 1 }"],
        capture_output=True, timeout=15,
    )
    return check.returncode == 0


class UARTShell:
    def __init__(self, port: str, baud: int):
        self.buffer = ""
        self.lines: list[str] = []
        self.condition = threading.Condition()
        serial_type = load_serial_port()
        self.serial = serial_type(port, baud, self._receive)

    def _receive(self, data: bytes) -> None:
        with self.condition:
            self.buffer += data.decode("utf-8", errors="replace").replace("\r", "")
            while "\n" in self.buffer:
                line, self.buffer = self.buffer.split("\n", 1)
                clean = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", line).strip()
                if clean:
                    self.lines.append(clean)
            self.condition.notify_all()

    def open(self) -> None:
        self.serial.open()
        self.command("echo __AUTO_UART_READY__")
        if not self.wait_for(lambda line: "__AUTO_UART_READY__" in line, 5):
            raise RuntimeError("UART opened but no shell marker was returned; log in on COM3 first")

    def command(self, value: str) -> None:
        self.serial.command(value)

    def mark(self) -> int:
        with self.condition:
            return len(self.lines)

    def wait_for(self, predicate, timeout: float, start: int = 0) -> str | None:
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for line in self.lines[start:]:
                    if predicate(line):
                        return line
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return None
                self.condition.wait(min(remaining, 0.2))

    def run(self, command: str, timeout: float = 8) -> list[str]:
        token = f"__AUTO_DONE_{time.time_ns()}__"
        start = self.mark()
        self.command(f"{command}; printf '\\n{token}=%s\\n' $?")
        marker = self.wait_for(lambda line: token in line, timeout, start)
        if marker is None:
            raise TimeoutError(f"UART command timeout: {command[:80]}")
        with self.condition:
            return [line for line in self.lines[start:] if token not in line]

    def close(self) -> None:
        self.serial.close()


def offline_validate(row: dict) -> list[dict]:
    results = []
    if row["valid"]:
        checks = (("ethernet_fixture", lambda: decode_ethernet_ipv4(bytes.fromhex(row["ethernet_fixture_hex"]))),)
        if row.get("ipoc_core_hex"):
            checks += (("ipoc_core", lambda: decode_ipoc_core(bytes.fromhex(row["ipoc_core_hex"]))),)
        else:
            results.append(result(row, "ipoc_cross_frame", "BLOCKED", row["ipoc_core_scope"], "offline"))
        if row["payload_kind"] == "application":
            checks += (("application", lambda: decode_application(bytes.fromhex(row["payload_hex"]))),)
        for name, action in checks:
            try:
                action()
                results.append(result(row, name, "PASS", "encode/decode round trip passed", "offline"))
            except ValueError as exc:
                results.append(result(row, name, "FAIL", str(exc), "offline"))
    else:
        try:
            decode_application(bytes.fromhex(row["payload_hex"]))
            results.append(result(row, "negative_rejection", "FAIL", "invalid frame was accepted", "offline"))
        except ValueError as exc:
            results.append(result(row, "negative_rejection", "PASS", f"rejected: {exc}", "offline"))
    return results


def result(row: dict, check: str, status: str, evidence: str, layer: str) -> dict:
    return {
        "id": row["id"], "role": row["role"], "business": row["business"],
        "direction": row["direction"], "check": check, "status": status,
        "layer": layer, "source_ip": row["source_ip"], "target_ip": row["target_ip"],
        "expected_priority": row["priority"], "payload_sha256": row["payload_sha256"],
        "evidence": evidence,
    }


def board_log_match(line: str, row: dict) -> bool:
    if not line.startswith("RX iface="):
        return False
    required = (f"iface={row['board_iface']}", f"src={row['source_ip']}",
                f"dst={row['target_ip']}", f"priority={row['priority']}")
    if not all(item in line for item in required):
        return False
    # The gateway emits data_hex only for its configurable demo protocol port.
    # Other documented UDP ports still provide link/IP/priority evidence.
    if row["port"] == 47000:
        return row["payload_hex"] in line.replace("...", "")
    return f"dport={row['port']}" in line or f"sport={row['port']}" in line


def pc_to_board(row: dict, uart: UARTShell, timeout: float) -> dict:
    start = uart.mark()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        # Let Windows choose a non-reserved ephemeral port. Random test values
        # can overlap the OS excluded-port range and are not part of the ICD.
        sock.bind((row["source_ip"], 0))
        sock.sendto(bytes.fromhex(row["payload_hex"]), (row["target_ip"], row["port"]))
    line = uart.wait_for(lambda value: board_log_match(value, row), timeout, start)
    if line is None:
        # Poll the same log used by the existing rk-eth-uart GUI.
        uart.run("tail -n 160 /home/youyeetoo/youyeetoo_eth_gateway/youyeetoo_eth_gateway.log", 6)
        line = uart.wait_for(lambda value: board_log_match(value, row), 1, start)
    if line is None:
        with uart.condition:
            candidates = [value for value in uart.lines[start:]
                          if f"src={row['source_ip']}" in value or f"dst={row['target_ip']}" in value]
        detail = " | ".join(candidates[-3:]) if candidates else "no same-IP candidate RX line"
        return result(row, "live_payload_and_classification", "FAIL",
                      f"no exact board RX match; candidates: {detail}", "live")
    check = "live_payload_and_classification" if row["port"] == 47000 else "live_route_and_classification"
    return result(row, check, "PASS", line, "live")


def shell_quote(value: str) -> str:
    return "'" + value.replace("'", "'\\''") + "'"


def board_to_pc(row: dict, uart: UARTShell, timeout: float) -> dict:
    received: dict = {}
    ready = threading.Event()

    def listen() -> None:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(timeout)
                sock.bind((row["target_ip"], row["port"]))
                ready.set()
                data, peer = sock.recvfrom(65535)
                received.update(data=data, peer=peer)
        except Exception as exc:  # evidence is reported to the caller
            received["error"] = str(exc)
            ready.set()

    thread = threading.Thread(target=listen, daemon=True)
    thread.start()
    ready.wait(2)
    if "error" in received:
        return result(row, "live_wire_receive", "BLOCKED", received["error"], "live")
    raw = bytes.fromhex(row["payload_hex"])
    python = (
        "import socket,binascii; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); "
        f"s.setsockopt(socket.SOL_SOCKET,25,{(row['board_iface'] + chr(0)).encode()!r}); "
        f"s.bind(({row['source_ip']!r},{row['source_port']})); "
        f"s.sendto(binascii.unhexlify({raw.hex()!r}),({row['target_ip']!r},{row['port']})); s.close()"
    )
    iface, source = row["board_iface"], row["source_ip"]
    command = (
        f"added=0; ip -4 addr show dev {iface} | grep -qwF {source} || "
        f"{{ ip addr add {source}/32 dev {iface}; added=1; }}; "
        f"python3 -c {shell_quote(python)}; rc=$?; "
        f"[ $added -eq 0 ] || ip addr del {source}/32 dev {iface}; exit $rc"
    )
    try:
        uart.run(command, timeout)
    except Exception as exc:
        return result(row, "live_wire_receive", "FAIL", f"board UART send failed: {exc}", "live")
    thread.join(timeout + 0.5)
    if "data" not in received:
        return result(row, "live_wire_receive", "FAIL",
                      received.get("error", "PC UDP listener timed out"), "live")
    actual = received["data"]
    if actual != raw:
        return result(row, "live_wire_receive", "FAIL",
                      f"payload mismatch actual_sha256={hashlib.sha256(actual).hexdigest()}", "live")
    peer = received["peer"]
    return result(row, "live_wire_receive", "PASS",
                  f"PC received {len(actual)} bytes from {peer[0]}:{peer[1]}; exact payload match", "live")


def preflight(uart: UARTShell) -> dict:
    lines = uart.run(
        "echo __PREFLIGHT__; uname -a; ip -br addr show dev eth0; ip -br addr show dev eth1; ip -4 route; "
        "ip -s link show eth0; ip -s link show eth1; "
        "pgrep -af '^./youyeetoo_eth_gateway ' || true; echo __PREFLIGHT_END__", 12)
    if not any("youyeetoo_eth_gateway" in line for line in lines):
        uart.run("cd /home/youyeetoo/youyeetoo_eth_gateway && nohup ./youyeetoo_eth_gateway --payload-iface eth0 --x-iface eth1 --port 47000 >/dev/null 2>&1 &", 8)
    return {
        "board": lines,
        "windows_adapters": powershell_json("Get-NetAdapter | Select-Object Name,Status,LinkSpeed,InterfaceDescription,ifIndex"),
        "windows_addresses": powershell_json("Get-NetIPAddress -AddressFamily IPv4 | Select-Object InterfaceAlias,IPAddress,PrefixLength,AddressState"),
        "windows_routes": powershell_json("Get-NetRoute -AddressFamily IPv4 | Select-Object DestinationPrefix,NextHop,InterfaceAlias,RouteMetric"),
        "existing_components": {
            "rk_eth_uart": str(UART_PROGRAM), "rk_eth_uart_exists": UART_PROGRAM.exists(),
            "eth_sim": str(ETH_SIM), "eth_sim_exists": ETH_SIM.exists(),
        },
    }


def write_reports(output: Path, metadata: dict, vectors: list[dict], results: list[dict]) -> None:
    output.mkdir(parents=True, exist_ok=True)
    (output / "vectors.jsonl").write_text(
        "".join(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n" for row in vectors), encoding="utf-8")
    (output / "report.json").write_text(
        json.dumps({"metadata": metadata, "results": results}, ensure_ascii=False, indent=2), encoding="utf-8")
    columns = list(results[0]) if results else []
    with (output / "results.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(results)
    counts = {status: sum(row["status"] == status for row in results) for status in ("PASS", "FAIL", "BLOCKED")}
    lines = [
        "# 风火轮以太网自动化闭环测试报告", "",
        f"- 时间：{metadata['time']}", f"- 模式：{metadata['mode']}", f"- Seed：`{metadata['seed']}`",
        f"- 结果：PASS={counts['PASS']}，FAIL={counts['FAIL']}，BLOCKED={counts['BLOCKED']}", "",
        "## 测试结果", "",
        "| ID | 角色/业务 | 方向 | 检查 | 层级 | 结果 | 证据 |",
        "|---|---|---|---|---|---|---|",
    ]
    for row in results:
        evidence = row["evidence"].replace("|", "\\|").replace("\n", " ")
        lines.append(f"| {row['id']} | {row['role']} / {row['business']} | {row['direction']} | {row['check']} | {row['layer']} | {row['status']} | {evidence[:300]} |")
    lines += [
        "", "## 固定边界", "",
        "- 离线 Ethernet II 检查使用确定性测试 MAC 和软件计算 FCS；实际链路由标准 ARP 决定 MAC，普通 UDP socket 无法证明线上 FCS。",
        "- IPoC 检查覆盖同步字、AOS 主导头、M_PDU、ENCAP、IPE、IPv4/UDP、0xAA 填充和 CRC-16；不包含文档未给全实现参数的 LDPC/RS 128 字节编码区。",
        "- PFC/PAUSE、交换芯片 FIFO 水位、射频链路加扰和 LDPC/RS 必须由对应硬件/固件测试，不能用应用层报文代替。",
        "- UDP 端口 47000、板端载荷地址 10.240.1.34/24、板端平台交互地址 10.240.1.36/32 均为当前 Demo 配置，不是正式 ICD 冻结值。",
    ]
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("offline", "live"), default="offline")
    parser.add_argument("--seed", type=int, default=20260812)
    parser.add_argument("--count", type=int, default=14)
    parser.add_argument("--negative-count", type=int, default=3)
    parser.add_argument("--com", default="COM3")
    parser.add_argument("--baud", type=int, default=1500000)
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("--output", type=Path, default=ROOT / "reports" / dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    args = parser.parse_args()
    vectors = generate(args.seed, args.count, args.negative_count)
    results = [item for row in vectors for item in offline_validate(row)]
    metadata = {"time": dt.datetime.now().astimezone().isoformat(), "mode": args.mode,
                "seed": args.seed, "count": args.count, "negative_count": args.negative_count}
    uart = None
    if args.mode == "live":
        try:
            uart = UARTShell(args.com, args.baud)
            uart.open()
            metadata["preflight"] = preflight(uart)
            for row in vectors:
                if not row["valid"]:
                    continue
                if (row["direction"] == "pc_to_board" and row["role"] == "nb-iot"
                        and not has_payload_route(row["target_ip"])):
                    results.append(result(
                        row, "live_route_precondition", "BLOCKED",
                        f"missing {row['target_ip']}/32 via {PAYLOAD_NEXT_HOP} on 以太网 3; "
                        f"run {ETH_SIM / 'setup_interfaces.ps1'} in Administrator PowerShell",
                        "live"))
                    continue
                try:
                    live = (pc_to_board(row, uart, args.timeout) if row["direction"] == "pc_to_board"
                            else board_to_pc(row, uart, args.timeout))
                except OSError as exc:
                    live = result(row, "live_closed_loop", "BLOCKED", f"local OS/network blocked operation: {exc}", "live")
                except Exception as exc:
                    live = result(row, "live_closed_loop", "FAIL", f"runner error: {exc}", "live")
                results.append(live)
        except Exception as exc:
            metadata["live_blocker"] = str(exc)
            tested = {row["id"] for row in results if row["layer"] == "live"}
            for row in vectors:
                if row["valid"] and row["id"] not in tested:
                    results.append(result(row, "live_closed_loop", "BLOCKED", str(exc), "live"))
        finally:
            if uart is not None:
                uart.close()
    results.extend([
        {"id": "SCOPE", "role": "x-control", "business": "IPoC LDPC/RS and RF randomization", "direction": "both",
         "check": "hardware_coded_link", "status": "BLOCKED", "layer": "hardware", "source_ip": "", "target_ip": "",
         "expected_priority": "", "payload_sha256": "", "evidence": "implementation parameters/hardware capture are unavailable"},
        {"id": "SCOPE", "role": "x-control", "business": "IPoC cross-frame ENCAP reassembly", "direction": "both",
         "check": "M_PDU_cross_frame", "status": "BLOCKED", "layer": "protocol", "source_ip": "", "target_ip": "",
         "expected_priority": "", "payload_sha256": "", "evidence": "single-frame IPoC core is implemented; cross-frame reassembly requires the X-machine implementation baseline"},
        {"id": "SCOPE", "role": "x-control", "business": "PFC/PAUSE", "direction": "both",
         "check": "802.1Qbb_PFC", "status": "BLOCKED", "layer": "hardware", "source_ip": "", "target_ip": "",
         "expected_priority": "", "payload_sha256": "", "evidence": "requires switch/NIC PFC capability and queue counters; application injection is not valid evidence"},
        {"id": "SCOPE", "role": "all", "business": "live Ethernet FCS", "direction": "both",
         "check": "wire_FCS", "status": "BLOCKED", "layer": "link", "source_ip": "", "target_ip": "",
         "expected_priority": "", "payload_sha256": "", "evidence": "normal NIC capture strips FCS; no FCS-capable tap/capture path detected"},
    ])
    write_reports(args.output, metadata, vectors, results)
    failed = any(row["status"] == "FAIL" for row in results)
    print(json.dumps({"output": str(args.output), "failed": failed,
                      "pass": sum(row["status"] == "PASS" for row in results),
                      "fail": sum(row["status"] == "FAIL" for row in results),
                      "blocked": sum(row["status"] == "BLOCKED" for row in results)}, ensure_ascii=False))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
