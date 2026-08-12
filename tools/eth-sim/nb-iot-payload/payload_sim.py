#!/usr/bin/env python3
"""NB-IoT payload simulator for PC Ethernet port 0."""

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

from youyeetoo_protocol import PROTO_PORT, hex_packet  # noqa: E402


CHANNELS = {
    "N6 用户业务": ("10.240.1.38", "10.240.1.50"),
    "基站管理/日志": ("10.240.1.35", "10.240.1.51"),
    "核心网管理/日志": ("10.240.1.39", "10.240.1.52"),
    "S1-C / S1-U": ("10.240.1.37", "10.240.1.40"),
}


def endpoint(ip_text: str, port_text: str) -> tuple[str, int]:
    ipaddress.ip_address(ip_text)
    port = int(port_text)
    if not 1 <= port <= 65535:
        raise ValueError("端口必须在 1..65535 之间")
    return ip_text, port


def payload_bytes(text: str, as_hex: bool) -> bytes:
    data = bytes.fromhex(text) if as_hex else text.encode("utf-8")
    if len(data) > 65507:
        raise ValueError("UDP 数据不能超过 65507 字节")
    return data


def send_datagrams(source_ip: str, target_ip: str, port: int, data: bytes,
                   count: int, interval: float, report=print) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((source_ip, 0))
        for index in range(count):
            sock.sendto(data, (target_ip, port))
            report(
                f"TX #{index + 1} {source_ip} -> {target_ip}:{port} "
                f"bytes={len(data)} data={hex_packet(data[:64])}"
            )
            if interval and index + 1 < count:
                time.sleep(interval)


class PayloadApp:
    def __init__(self) -> None:
        import tkinter as tk
        from tkinter import scrolledtext, ttk

        self.tk = tk
        self.ttk = ttk
        self.root = tk.Tk()
        self.root.title("网口0 - 窄带物联网载荷模拟器")
        self.root.geometry("920x680")
        self.root.minsize(760, 580)
        self.events: queue.Queue[tuple[str, str]] = queue.Queue()
        self.listener_stop = threading.Event()

        style = ttk.Style(self.root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 17, "bold"))
        style.configure("Role.TLabel", foreground="#166534", font=("Microsoft YaHei UI", 10, "bold"))

        outer = ttk.Frame(self.root, padding=16)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 12))
        ttk.Label(header, text="窄带物联网载荷模拟器", style="Title.TLabel").pack(side="left")
        ttk.Label(header, text="网口0 / 开发板 eth0", style="Role.TLabel").pack(side="right")

        settings = ttk.LabelFrame(outer, text="链路与业务", padding=12)
        settings.pack(fill="x")
        self.channel = tk.StringVar(value=next(iter(CHANNELS)))
        self.source_ip = tk.StringVar(value=CHANNELS[self.channel.get()][0])
        self.target_ip = tk.StringVar(value=CHANNELS[self.channel.get()][1])
        self.port = tk.StringVar(value=str(PROTO_PORT))
        ttk.Label(settings, text="业务通道").grid(row=0, column=0, sticky="w")
        channel_box = ttk.Combobox(settings, textvariable=self.channel,
                                   values=list(CHANNELS), state="readonly", width=22)
        channel_box.grid(row=1, column=0, sticky="ew", padx=(0, 10))
        channel_box.bind("<<ComboboxSelected>>", self._channel_changed)
        ttk.Label(settings, text="源 IP").grid(row=0, column=1, sticky="w")
        ttk.Entry(settings, textvariable=self.source_ip, width=18).grid(row=1, column=1, sticky="ew", padx=(0, 10))
        ttk.Label(settings, text="目标 IP").grid(row=0, column=2, sticky="w")
        ttk.Entry(settings, textvariable=self.target_ip, width=18).grid(row=1, column=2, sticky="ew", padx=(0, 10))
        ttk.Label(settings, text="UDP 端口").grid(row=0, column=3, sticky="w")
        ttk.Entry(settings, textvariable=self.port, width=10).grid(row=1, column=3, sticky="ew")
        for column in range(4):
            settings.columnconfigure(column, weight=1)

        send_box = ttk.LabelFrame(outer, text="发送测试数据", padding=12)
        send_box.pack(fill="x", pady=12)
        self.data_mode = tk.StringVar(value="text")
        mode_row = ttk.Frame(send_box)
        mode_row.pack(fill="x", pady=(0, 6))
        ttk.Radiobutton(mode_row, text="文本", variable=self.data_mode, value="text").pack(side="left")
        ttk.Radiobutton(mode_row, text="十六进制", variable=self.data_mode, value="hex").pack(side="left", padx=12)
        self.message = scrolledtext.ScrolledText(send_box, height=5, wrap="word", font=("Consolas", 10))
        self.message.pack(fill="x")
        self.message.insert("1.0", "NB-IoT N6 business test")
        controls = ttk.Frame(send_box)
        controls.pack(fill="x", pady=(10, 0))
        self.count = tk.StringVar(value="1")
        self.interval = tk.StringVar(value="0.0")
        ttk.Label(controls, text="次数").pack(side="left")
        ttk.Spinbox(controls, from_=1, to=100000, textvariable=self.count, width=8).pack(side="left", padx=(6, 16))
        ttk.Label(controls, text="间隔(秒)").pack(side="left")
        ttk.Entry(controls, textvariable=self.interval, width=8).pack(side="left", padx=(6, 16))
        self.send_button = ttk.Button(controls, text="发送", command=self._send)
        self.send_button.pack(side="right")

        receive = ttk.LabelFrame(outer, text="接收监听", padding=12)
        receive.pack(fill="x")
        self.bind_ip = tk.StringVar(value=self.source_ip.get())
        ttk.Label(receive, text="绑定 IP").pack(side="left")
        ttk.Entry(receive, textvariable=self.bind_ip, width=18).pack(side="left", padx=(6, 12))
        self.listen_button = ttk.Button(receive, text="开始监听", command=self._toggle_listener)
        self.listen_button.pack(side="left")
        ttk.Button(receive, text="清空记录", command=self._clear_log).pack(side="right")

        log_frame = ttk.LabelFrame(outer, text="测试记录", padding=8)
        log_frame.pack(fill="both", expand=True, pady=(12, 0))
        self.log = scrolledtext.ScrolledText(log_frame, height=12, state="disabled",
                                             wrap="word", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        self.status = tk.StringVar(value="就绪")
        ttk.Label(outer, textvariable=self.status, anchor="w").pack(fill="x", pady=(8, 0))

        self.root.protocol("WM_DELETE_WINDOW", self._close)
        self.root.after(100, self._drain_events)

    def _channel_changed(self, _event=None) -> None:
        source, target = CHANNELS[self.channel.get()]
        self.source_ip.set(source)
        self.target_ip.set(target)
        self.bind_ip.set(source)

    def _emit(self, kind: str, message: str) -> None:
        self.events.put((kind, message))

    def _clear_log(self) -> None:
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _drain_events(self) -> None:
        try:
            while True:
                kind, message = self.events.get_nowait()
                if kind == "log":
                    timestamp = time.strftime("%H:%M:%S")
                    self.log.configure(state="normal")
                    self.log.insert("end", f"[{timestamp}] {message}\n")
                    self.log.see("end")
                    self.log.configure(state="disabled")
                elif kind == "status":
                    self.status.set(message)
                elif kind == "send_done":
                    self.send_button.configure(state="normal")
                    self.status.set(message)
                elif kind == "listen_done":
                    self.listen_button.configure(text="开始监听")
                    self.status.set(message)
        except queue.Empty:
            pass
        self.root.after(100, self._drain_events)

    def _send(self) -> None:
        try:
            source, _ = endpoint(self.source_ip.get().strip(), self.port.get())
            target, port = endpoint(self.target_ip.get().strip(), self.port.get())
            count = int(self.count.get())
            interval = float(self.interval.get())
            if count < 1 or interval < 0:
                raise ValueError("次数至少为 1，间隔不能为负数")
            data = payload_bytes(self.message.get("1.0", "end-1c"), self.data_mode.get() == "hex")
        except (ValueError, OSError) as exc:
            self._emit("log", f"参数错误: {exc}")
            return
        self.send_button.configure(state="disabled")
        self.status.set("正在发送...")

        def worker() -> None:
            try:
                send_datagrams(source, target, port, data, count, interval,
                               lambda line: self._emit("log", line))
                self._emit("send_done", f"发送完成，共 {count} 个报文")
            except OSError as exc:
                self._emit("log", f"发送失败: {exc}")
                self._emit("send_done", "发送失败")

        threading.Thread(target=worker, daemon=True).start()

    def _toggle_listener(self) -> None:
        if self.listener_stop.is_set() is False and self.listen_button.cget("text") == "停止监听":
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
                        self._emit("log", f"RX {peer[0]}:{peer[1]} bytes={len(data)} data={hex_packet(data[:64])}")
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
    sender = subparsers.add_parser("send", help="发送载荷 UDP 数据")
    sender.add_argument("--source-ip", default="10.240.1.38")
    sender.add_argument("--target-ip", default="10.240.1.50")
    sender.add_argument("--port", type=int, default=PROTO_PORT)
    sender.add_argument("--text", default="NB-IoT N6 business test")
    sender.add_argument("--hex-data")
    sender.add_argument("--count", type=int, default=1)
    sender.add_argument("--interval", type=float, default=0.0)
    listener = subparsers.add_parser("listen", help="监听载荷 UDP 数据")
    listener.add_argument("--bind-ip", default="10.240.1.38")
    listener.add_argument("--port", type=int, default=PROTO_PORT)
    listener.add_argument("--count", type=int, default=0, help="0 表示持续监听")
    listener.add_argument("--timeout", type=float, default=5.0)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.mode is None:
        PayloadApp().run()
        return 0
    try:
        if args.mode == "send":
            data = payload_bytes(args.hex_data if args.hex_data is not None else args.text,
                                 args.hex_data is not None)
            send_datagrams(args.source_ip, args.target_ip, args.port, data,
                           args.count, args.interval)
        else:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(args.timeout)
                sock.bind((args.bind_ip, args.port))
                print(f"LISTEN {args.bind_ip}:{args.port}")
                received = 0
                while args.count == 0 or received < args.count:
                    data, peer = sock.recvfrom(65535)
                    received += 1
                    print(f"RX #{received} {peer[0]}:{peer[1]} bytes={len(data)} {hex_packet(data[:64])}")
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
