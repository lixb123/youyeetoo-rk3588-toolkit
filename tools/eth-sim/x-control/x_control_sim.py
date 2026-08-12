#!/usr/bin/env python3
"""X-band TT&C simulator for PC Ethernet port 1."""

from __future__ import annotations

import argparse
import ipaddress
import queue
import socket
import sys
import threading
import time
from pathlib import Path

COMMON_DIR = Path(__file__).resolve().parents[1] / "common"
sys.path.insert(0, str(COMMON_DIR))

from youyeetoo_protocol import (  # noqa: E402
    PROTO_PORT,
    command,
    decode,
    file_packet,
    hex_packet,
)


COMMANDS = {
    "链路检测 (001D)": 0x001D,
    "固件版本查询 (0101)": 0x0101,
    "启动固件重构 (01AF)": 0x01AF,
    "重构状态查询 (01C5)": 0x01C5,
    "FTP 写入查询 (018C)": 0x018C,
    "FTP 更新通知 (01D1)": 0x01D1,
    "FTP 结果查询 (01DA)": 0x01DA,
    "自主重构准备 (3403)": 0x3403,
    "自主重构开始 (340A)": 0x340A,
    "自主重构查询 (340E)": 0x340E,
    "软件版本查询 (3503)": 0x3503,
    "星历分发 (0001)": 0x0001,
}


def endpoint(ip_text: str, port_text: str | int) -> tuple[str, int]:
    ipaddress.ip_address(ip_text)
    port = int(port_text)
    if not 1 <= port <= 65535:
        raise ValueError("端口必须在 1..65535 之间")
    return ip_text, port


def send_one(source_ip: str, target_ip: str, port: int, packet: bytes,
             wait_reply: bool, timeout: float, report=print) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.bind((source_ip, 0))
        sock.sendto(packet, (target_ip, port))
        report(f"TX {source_ip} -> {target_ip}:{port} bytes={len(packet)}")
        report(hex_packet(packet))
        if not wait_reply:
            return
        try:
            reply, peer = sock.recvfrom(65535)
        except TimeoutError:
            report("RX timeout（板端监视程序默认只打印，不返回应答）")
            return
        report(f"RX {peer[0]}:{peer[1]} bytes={len(reply)}")
        report(str(decode(reply)))


def packet_summary(data: bytes) -> str:
    try:
        return str(decode(data))
    except ValueError:
        return hex_packet(data[:128])


class XControlApp:
    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import filedialog, scrolledtext, ttk

        self.tk = tk
        self.ttk = ttk
        self.filedialog = filedialog
        self.root = tk.Tk()
        self.root.title("网口1 - X 测控机模拟器")
        self.root.geometry("960x720")
        self.root.minsize(800, 620)
        self.events: queue.Queue[tuple[str, str | int]] = queue.Queue()
        self.listener_stop = threading.Event()

        style = ttk.Style(self.root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 17, "bold"))
        style.configure("Role.TLabel", foreground="#9A3412", font=("Microsoft YaHei UI", 10, "bold"))

        outer = ttk.Frame(self.root, padding=16)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 12))
        ttk.Label(header, text="X 测控机模拟器", style="Title.TLabel").pack(side="left")
        ttk.Label(header, text="网口1 / 开发板 eth1", style="Role.TLabel").pack(side="right")

        settings = ttk.LabelFrame(outer, text="链路设置", padding=12)
        settings.pack(fill="x")
        self.source_ip = tk.StringVar(value="10.240.1.2")
        self.target_ip = tk.StringVar(value="10.240.1.36")
        self.port = tk.StringVar(value=str(PROTO_PORT))
        for column, (label, variable, width) in enumerate((
            ("源 IP", self.source_ip, 20),
            ("目标 IP", self.target_ip, 20),
            ("UDP 端口", self.port, 12),
        )):
            ttk.Label(settings, text=label).grid(row=0, column=column, sticky="w")
            ttk.Entry(settings, textvariable=variable, width=width).grid(
                row=1, column=column, sticky="ew", padx=(0, 10 if column < 2 else 0)
            )
            settings.columnconfigure(column, weight=1)

        tabs = ttk.Notebook(outer)
        tabs.pack(fill="x", pady=12)
        command_tab = ttk.Frame(tabs, padding=12)
        file_tab = ttk.Frame(tabs, padding=12)
        listen_tab = ttk.Frame(tabs, padding=12)
        tabs.add(command_tab, text="交互指令")
        tabs.add(file_tab, text="文件分段")
        tabs.add(listen_tab, text="接收监听")

        self.command_name = tk.StringVar(value=next(iter(COMMANDS)))
        self.params = tk.StringVar()
        self.sequence = tk.StringVar(value="0")
        self.wait_reply = tk.BooleanVar(value=False)
        ttk.Label(command_tab, text="指令").grid(row=0, column=0, sticky="w")
        ttk.Combobox(command_tab, textvariable=self.command_name, values=list(COMMANDS),
                     state="readonly", width=30).grid(row=1, column=0, sticky="ew", padx=(0, 10))
        ttk.Label(command_tab, text="参数（十六进制）").grid(row=0, column=1, sticky="w")
        ttk.Entry(command_tab, textvariable=self.params).grid(row=1, column=1, sticky="ew", padx=(0, 10))
        ttk.Label(command_tab, text="序列号").grid(row=0, column=2, sticky="w")
        ttk.Spinbox(command_tab, from_=0, to=16383, textvariable=self.sequence, width=9).grid(row=1, column=2, sticky="ew")
        ttk.Checkbutton(command_tab, text="等待应答", variable=self.wait_reply).grid(row=2, column=0, sticky="w", pady=(10, 0))
        self.command_button = ttk.Button(command_tab, text="发送指令", command=self._send_command)
        self.command_button.grid(row=2, column=2, sticky="e", pady=(10, 0))
        command_tab.columnconfigure(0, weight=2)
        command_tab.columnconfigure(1, weight=3)
        command_tab.columnconfigure(2, weight=1)

        self.file_path = tk.StringVar()
        self.file_interval = tk.StringVar(value="0.0")
        ttk.Label(file_tab, text="待发送文件").grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Entry(file_tab, textvariable=self.file_path).grid(row=1, column=0, sticky="ew", padx=(0, 8))
        ttk.Button(file_tab, text="浏览...", command=self._choose_file).grid(row=1, column=1)
        ttk.Label(file_tab, text="分段间隔(秒)").grid(row=2, column=0, sticky="w", pady=(10, 0))
        interval_row = ttk.Frame(file_tab)
        interval_row.grid(row=3, column=0, columnspan=2, sticky="ew")
        ttk.Entry(interval_row, textvariable=self.file_interval, width=10).pack(side="left")
        self.file_button = ttk.Button(interval_row, text="发送文件", command=self._send_file)
        self.file_button.pack(side="right")
        self.file_progress = ttk.Progressbar(file_tab, mode="determinate")
        self.file_progress.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        file_tab.columnconfigure(0, weight=1)

        self.bind_ip = tk.StringVar(value="10.240.1.2")
        ttk.Label(listen_tab, text="绑定 IP").pack(side="left")
        ttk.Entry(listen_tab, textvariable=self.bind_ip, width=20).pack(side="left", padx=(6, 12))
        self.listen_button = ttk.Button(listen_tab, text="开始监听", command=self._toggle_listener)
        self.listen_button.pack(side="left")

        log_frame = ttk.LabelFrame(outer, text="测试记录", padding=8)
        log_frame.pack(fill="both", expand=True)
        top_actions = ttk.Frame(log_frame)
        top_actions.pack(fill="x", pady=(0, 6))
        ttk.Button(top_actions, text="清空记录", command=self._clear_log).pack(side="right")
        self.log = scrolledtext.ScrolledText(log_frame, height=15, state="disabled",
                                             wrap="word", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        self.status = tk.StringVar(value="就绪")
        ttk.Label(outer, textvariable=self.status, anchor="w").pack(fill="x", pady=(8, 0))

        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(100, self._drain_events)

    def _emit(self, kind: str, value: str | int) -> None:
        self.events.put((kind, value))

    def _clear_log(self) -> None:
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _drain_events(self) -> None:
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "log":
                    timestamp = time.strftime("%H:%M:%S")
                    self.log.configure(state="normal")
                    self.log.insert("end", f"[{timestamp}] {value}\n")
                    self.log.see("end")
                    self.log.configure(state="disabled")
                elif kind == "status":
                    self.status.set(str(value))
                elif kind == "command_done":
                    self.command_button.configure(state="normal")
                    self.status.set(str(value))
                elif kind == "file_progress":
                    self.file_progress["value"] = int(value)
                elif kind == "file_done":
                    self.file_button.configure(state="normal")
                    self.status.set(str(value))
                elif kind == "listen_done":
                    self.listen_button.configure(text="开始监听")
                    self.status.set(str(value))
        except queue.Empty:
            pass
        self.root.after(100, self._drain_events)

    def _settings(self) -> tuple[str, str, int]:
        source, _ = endpoint(self.source_ip.get().strip(), self.port.get())
        target, port = endpoint(self.target_ip.get().strip(), self.port.get())
        return source, target, port

    def _send_command(self) -> None:
        try:
            source, target, port = self._settings()
            params = bytes.fromhex(self.params.get()) if self.params.get().strip() else b""
            sequence = int(self.sequence.get())
            packet = command(COMMANDS[self.command_name.get()], params=params, sequence=sequence)
        except (ValueError, KeyError) as exc:
            self._emit("log", f"参数错误: {exc}")
            return
        self.command_button.configure(state="disabled")
        self.status.set("正在发送指令...")
        wait_reply = self.wait_reply.get()

        def worker() -> None:
            try:
                send_one(source, target, port, packet, wait_reply, 2.0,
                         lambda line: self._emit("log", line))
                self._emit("command_done", "指令发送完成")
            except (OSError, ValueError) as exc:
                self._emit("log", f"发送失败: {exc}")
                self._emit("command_done", "发送失败")

        threading.Thread(target=worker, daemon=True).start()

    def _choose_file(self) -> None:
        selected = self.filedialog.askopenfilename(title="选择上注文件")
        if selected:
            self.file_path.set(selected)

    def _send_file(self) -> None:
        try:
            source, target, port = self._settings()
            path = Path(self.file_path.get())
            data = path.read_bytes()
            interval = float(self.file_interval.get())
            if len(data) > 30 * 1024 * 1024:
                raise ValueError("单个上注文件不能超过 30 MiB")
            if interval < 0:
                raise ValueError("分段间隔不能为负数")
        except (OSError, ValueError) as exc:
            self._emit("log", f"文件参数错误: {exc}")
            return
        chunks = [data[index:index + 1000] for index in range(0, len(data), 1000)] or [b""]
        self.file_progress.configure(maximum=len(chunks), value=0)
        self.file_button.configure(state="disabled")
        self.status.set(f"正在发送 {len(chunks)} 个文件分段...")

        def worker() -> None:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.bind((source, 0))
                    for block, chunk in enumerate(chunks):
                        if len(chunks) == 1:
                            grouping = 3
                        elif block == 0:
                            grouping = 1
                        elif block == len(chunks) - 1:
                            grouping = 2
                        else:
                            grouping = 0
                        packet = file_packet(block, chunk, grouping=grouping, sequence=block)
                        sock.sendto(packet, (target, port))
                        self._emit("log", f"TX file block={block} grouping={grouping} data={len(chunk)}")
                        self._emit("file_progress", block + 1)
                        if interval and block + 1 < len(chunks):
                            time.sleep(interval)
                self._emit("file_done", f"文件发送完成，共 {len(chunks)} 段")
            except (OSError, ValueError) as exc:
                self._emit("log", f"文件发送失败: {exc}")
                self._emit("file_done", "文件发送失败")

        threading.Thread(target=worker, daemon=True).start()

    def _toggle_listener(self) -> None:
        if not self.listener_stop.is_set() and self.listen_button.cget("text") == "停止监听":
            self.listener_stop.set()
            self.status.set("正在停止监听...")
            return
        try:
            bind_ip, port = endpoint(self.bind_ip.get().strip(), self.port.get())
        except ValueError as exc:
            self._emit("log", f"参数错误: {exc}")
            return
        self.listener_stop.clear()
        self.listen_button.configure(text="停止监听")
        self.status.set(f"监听 {bind_ip}:{port}")

        def worker() -> None:
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                    sock.settimeout(0.25)
                    sock.bind((bind_ip, port))
                    self._emit("log", f"LISTEN {bind_ip}:{port}")
                    while not self.listener_stop.is_set():
                        try:
                            data, peer = sock.recvfrom(65535)
                        except TimeoutError:
                            continue
                        self._emit("log", f"RX {peer[0]}:{peer[1]} bytes={len(data)} {packet_summary(data)}")
                self._emit("listen_done", "监听已停止")
            except OSError as exc:
                self._emit("log", f"监听失败: {exc}")
                self._emit("listen_done", "监听失败")

        threading.Thread(target=worker, daemon=True).start()

    def _close(self) -> None:
        self.listener_stop.set()
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode")
    sender = subparsers.add_parser("command", help="发送交互指令")
    sender.add_argument("opcode", help="十六进制指令码，例如 001D")
    sender.add_argument("--params", default="")
    sender.add_argument("--sequence", type=int, default=0)
    sender.add_argument("--source-ip", default="10.240.1.2")
    sender.add_argument("--target-ip", default="10.240.1.36")
    sender.add_argument("--port", type=int, default=PROTO_PORT)
    sender.add_argument("--wait", action="store_true")
    file_sender = subparsers.add_parser("file", help="发送分段文件")
    file_sender.add_argument("path", type=Path)
    file_sender.add_argument("--source-ip", default="10.240.1.2")
    file_sender.add_argument("--target-ip", default="10.240.1.36")
    file_sender.add_argument("--port", type=int, default=PROTO_PORT)
    file_sender.add_argument("--interval", type=float, default=0.0)
    listener = subparsers.add_parser("listen", help="监听应答")
    listener.add_argument("--bind-ip", default="10.240.1.2")
    listener.add_argument("--port", type=int, default=PROTO_PORT)
    listener.add_argument("--count", type=int, default=0)
    listener.add_argument("--timeout", type=float, default=5.0)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.mode is None:
        XControlApp().run()
        return 0
    try:
        if args.mode == "command":
            packet = command(int(args.opcode, 16), bytes.fromhex(args.params), args.sequence)
            send_one(args.source_ip, args.target_ip, args.port, packet, args.wait, 2.0)
        elif args.mode == "file":
            data = args.path.read_bytes()
            if len(data) > 30 * 1024 * 1024:
                raise ValueError("single upload exceeds 30 MiB")
            chunks = [data[index:index + 1000] for index in range(0, len(data), 1000)] or [b""]
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind((args.source_ip, 0))
                for block, chunk in enumerate(chunks):
                    grouping = 3 if len(chunks) == 1 else 1 if block == 0 else 2 if block == len(chunks) - 1 else 0
                    packet = file_packet(block, chunk, grouping=grouping, sequence=block)
                    sock.sendto(packet, (args.target_ip, args.port))
                    print(f"TX file block={block} grouping={grouping} bytes={len(chunk)}")
                    if args.interval:
                        time.sleep(args.interval)
        else:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(args.timeout)
                sock.bind((args.bind_ip, args.port))
                print(f"LISTEN {args.bind_ip}:{args.port}")
                received = 0
                while args.count == 0 or received < args.count:
                    data, peer = sock.recvfrom(65535)
                    received += 1
                    print(f"RX #{received} {peer[0]}:{peer[1]} {packet_summary(data)}")
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
