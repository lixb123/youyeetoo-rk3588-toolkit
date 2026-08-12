from __future__ import annotations

import base64
import ctypes
from ctypes import wintypes
import ipaddress
import queue
import re
import struct
import threading
import time
try:
    import tkinter as tk
    from tkinter import messagebox, scrolledtext, ttk
except ModuleNotFoundError:  # Protocol helpers remain usable on headless Python installs.
    tk = None
    messagebox = scrolledtext = ttk = None


# Windows serial constants. The UART is the control path; Ethernet payloads
# are sent by a short Python command executed on the RK3588 shell.
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
NOPARITY = 0
ONESTOPBIT = 0
EVENPARITY = 2

COMMAND_MARKER = 0xEB90
RESPONSE_MARKER = 0x1ACF
APID_DEVICE = 0x3D0
APID_FILE = 0x3DF
DEFAULT_PORT = 47000

CHANNELS = {
    "X 测控 / 平台应答": ("10.240.1.36", "10.240.1.2", "eth1", 0),
    "N6 用户业务": ("10.240.1.50", "10.240.1.38", "eth0", 1),
    "S1-C / S1-U": ("10.240.1.40", "10.240.1.37", "eth0", 1),
    "基站管理 / 日志": ("10.240.1.51", "10.240.1.35", "eth0", 2),
    "核心网管理 / 日志": ("10.240.1.52", "10.240.1.39", "eth0", 2),
}


def checksum(value: bytes) -> int:
    return (~sum(value[2:])) & 0xFFFF


def packet(marker: int, apid: int, grouping: int, sequence: int, data: bytes) -> bytes:
    if marker not in (COMMAND_MARKER, RESPONSE_MARKER):
        raise ValueError("标识符必须为 EB90 或 1ACF")
    if not data or len(data) > 0x10000:
        raise ValueError("协议数据不能为空，且不能超过 65536 字节")
    if not 0 <= sequence <= 0x3FFF or not 0 <= grouping <= 3:
        raise ValueError("序列号或分组标志超出范围")
    word1 = apid & 0x7FF
    word2 = ((grouping & 3) << 14) | sequence
    primary = struct.pack(">HHHH", marker, word1, word2, len(data) - 1)
    body = primary + data
    return body + struct.pack(">H", checksum(body))


def command_packet(opcode_text: str, params_text: str, sequence_text: str,
                   marker: int = COMMAND_MARKER) -> bytes:
    opcode = int(opcode_text.strip(), 16)
    params = bytes.fromhex(params_text.strip()) if params_text.strip() else b""
    sequence = int(sequence_text.strip() or "0")
    if not 0 <= opcode <= 0xFFFF:
        raise ValueError("指令码必须是 0000-FFFF")
    return packet(marker, APID_DEVICE, 3, sequence,
                  struct.pack(">H", opcode) + params)


def file_packet(block_text: str, data_text: str, grouping_text: str, sequence_text: str) -> bytes:
    block = int(block_text.strip(), 0)
    grouping = int(grouping_text.strip(), 0)
    sequence = int(sequence_text.strip() or "0")
    data = bytes.fromhex(data_text.strip()) if data_text.strip() else b""
    if not 0 <= block <= 0xFFFF or len(data) > 1000:
        raise ValueError("文件块号需为 0-65535，数据最多 1000 字节")
    return packet(COMMAND_MARKER, APID_FILE, grouping, sequence,
                  struct.pack(">H", block) + data)


def hex_dump(value: bytes) -> str:
    return value.hex(" ").upper()


class DCB(ctypes.Structure):
    _fields_ = [
        ("DCBlength", wintypes.DWORD), ("BaudRate", wintypes.DWORD),
        ("Flags", wintypes.DWORD), ("wReserved", wintypes.WORD),
        ("XonLim", wintypes.WORD), ("XoffLim", wintypes.WORD),
        ("ByteSize", wintypes.BYTE), ("Parity", wintypes.BYTE),
        ("StopBits", wintypes.BYTE), ("XonChar", wintypes.CHAR),
        ("XoffChar", wintypes.CHAR), ("ErrorChar", wintypes.CHAR),
        ("EofChar", wintypes.CHAR), ("EvtChar", wintypes.CHAR),
        ("wReserved1", wintypes.WORD),
    ]


class COMMTIMEOUTS(ctypes.Structure):
    _fields_ = [("ReadIntervalTimeout", wintypes.DWORD),
                ("ReadTotalTimeoutMultiplier", wintypes.DWORD),
                ("ReadTotalTimeoutConstant", wintypes.DWORD),
                ("WriteTotalTimeoutMultiplier", wintypes.DWORD),
                ("WriteTotalTimeoutConstant", wintypes.DWORD)]


class SerialPort:
    def __init__(self, name: str, baud: int, on_bytes):
        self.name = name
        self.baud = baud
        self.on_bytes = on_bytes
        self.handle = None
        self.stop_event = threading.Event()
        self.thread = None
        self.write_lock = threading.Lock()

    def open(self):
        if self.handle is not None:
            return
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
                                         wintypes.DWORD, wintypes.LPVOID,
                                         wintypes.DWORD, wintypes.DWORD,
                                         wintypes.HANDLE]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        path = f"\\\\.\\{self.name}"
        handle = kernel32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, None,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None)
        if handle == INVALID_HANDLE_VALUE:
            raise OSError(ctypes.get_last_error(), f"无法打开 {self.name}")
        self.handle = handle
        kernel32.SetupComm(handle, 4096, 4096)
        state = DCB()
        state.DCBlength = ctypes.sizeof(DCB)
        if not kernel32.GetCommState(handle, ctypes.byref(state)):
            self.close()
            raise OSError("读取串口参数失败")
        state.BaudRate = self.baud
        state.ByteSize = 8
        state.Parity = NOPARITY
        state.StopBits = ONESTOPBIT
        state.Flags = 0x00000001  # fBinary, no parity check, no flow control
        if not kernel32.SetCommState(handle, ctypes.byref(state)):
            self.close()
            raise OSError("设置串口参数失败")
        timeouts = COMMTIMEOUTS(50, 0, 50, 0, 100)
        kernel32.SetCommTimeouts(handle, ctypes.byref(timeouts))
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        while not self.stop_event.is_set() and self.handle is not None:
            buffer = ctypes.create_string_buffer(4096)
            read = wintypes.DWORD()
            ok = kernel32.ReadFile(self.handle, buffer, len(buffer), ctypes.byref(read), None)
            if ok and read.value:
                self.on_bytes(buffer.raw[:read.value])
            elif not ok:
                break

    def write(self, value: bytes):
        if self.handle is None:
            raise RuntimeError("UART 尚未连接")
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        written = wintypes.DWORD()
        with self.write_lock:
            if not kernel32.WriteFile(self.handle, value, len(value), ctypes.byref(written), None):
                raise OSError("UART 写入失败")

    def command(self, text: str):
        self.write((text.rstrip("\r\n") + "\n").encode("utf-8"))

    def close(self):
        self.stop_event.set()
        if self.handle is not None:
            ctypes.WinDLL("kernel32", use_last_error=True).CloseHandle(self.handle)
            self.handle = None
        self.thread = None


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("RK3588 以太网 UART 联调工具")
        self.root.geometry("1280x820")
        self.root.minsize(1080, 680)
        self.serial = None
        self.rx_bytes = queue.Queue()
        self.rx_text = ""
        self.seen = set()
        self.last_poll = 0.0
        self.build_ui()
        self.root.after(80, self.process_serial)
        self.root.after(500, self.poll_board_log)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def build_ui(self):
        style = ttk.Style()
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass
        outer = ttk.Frame(self.root, padding=12)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 10))
        ttk.Label(header, text="RK3588 以太网 UART 联调", font=("Segoe UI", 18, "bold")).pack(side="left")
        self.status = tk.StringVar(value="未连接")
        ttk.Label(header, textvariable=self.status).pack(side="right")

        conn = ttk.LabelFrame(outer, text="UART 控制", padding=8)
        conn.pack(fill="x")
        self.port_var = tk.StringVar(value="COM3")
        self.baud_var = tk.StringVar(value="1500000")
        ttk.Label(conn, text="串口").pack(side="left")
        ttk.Entry(conn, textvariable=self.port_var, width=10).pack(side="left", padx=5)
        ttk.Label(conn, text="波特率").pack(side="left")
        ttk.Entry(conn, textvariable=self.baud_var, width=10).pack(side="left", padx=5)
        ttk.Button(conn, text="连接 UART", command=self.connect).pack(side="left", padx=5)
        ttk.Button(conn, text="断开", command=self.disconnect).pack(side="left", padx=5)
        ttk.Button(conn, text="启动/确认板端网关", command=self.start_gateway).pack(side="left", padx=5)
        ttk.Button(conn, text="读取板端状态", command=self.read_status).pack(side="left", padx=5)

        panes = ttk.Panedwindow(outer, orient="horizontal")
        panes.pack(fill="both", expand=True, pady=10)
        left = ttk.Frame(panes, padding=(0, 0, 8, 0))
        right = ttk.Frame(panes, padding=(8, 0, 0, 0))
        panes.add(left, weight=1)
        panes.add(right, weight=2)
        self.build_send_panel(left)
        self.build_receive_panel(right)

    def build_send_panel(self, parent):
        box = ttk.LabelFrame(parent, text="网口发送控制", padding=8)
        box.pack(fill="both", expand=True)
        row = ttk.Frame(box)
        row.pack(fill="x")
        ttk.Label(row, text="业务").pack(side="left")
        self.channel_var = tk.StringVar(value=list(CHANNELS)[0])
        channel = ttk.Combobox(row, textvariable=self.channel_var, values=list(CHANNELS), state="readonly", width=21)
        channel.pack(side="left", padx=5)
        channel.bind("<<ComboboxSelected>>", self.channel_changed)
        self.iface_var = tk.StringVar(value="eth1")
        ttk.Label(row, text="网口").pack(side="left")
        ttk.Entry(row, textvariable=self.iface_var, width=7).pack(side="left", padx=5)
        ttk.Label(box, text="源 IP").pack(anchor="w", pady=(8, 0))
        self.src_var = tk.StringVar(value="10.240.1.36")
        ttk.Entry(box, textvariable=self.src_var).pack(fill="x")
        ttk.Label(box, text="目标 IP").pack(anchor="w", pady=(8, 0))
        self.dst_var = tk.StringVar(value="10.240.1.2")
        ttk.Entry(box, textvariable=self.dst_var).pack(fill="x")
        ttk.Label(box, text="UDP 端口").pack(anchor="w", pady=(8, 0))
        self.port_var_udp = tk.StringVar(value=str(DEFAULT_PORT))
        ttk.Entry(box, textvariable=self.port_var_udp).pack(fill="x")
        ttk.Label(box, text="发送类型").pack(anchor="w", pady=(8, 0))
        self.kind_var = tk.StringVar(value="协议应答 1ACF")
        ttk.Combobox(box, textvariable=self.kind_var,
                     values=["协议应答 1ACF", "协议指令 EB90", "文件分段", "业务透传"],
                     state="readonly").pack(fill="x")
        ttk.Label(box, text="指令码 / 文件块号").pack(anchor="w", pady=(8, 0))
        self.opcode_var = tk.StringVar(value="001D")
        ttk.Entry(box, textvariable=self.opcode_var).pack(fill="x")
        ttk.Label(box, text="参数或数据（十六进制，不含空格也可）").pack(anchor="w", pady=(8, 0))
        mode_row = ttk.Frame(box)
        mode_row.pack(fill="x")
        ttk.Label(mode_row, text="数据输入格式").pack(side="left")
        self.data_mode_var = tk.StringVar(value="十六进制")
        ttk.Combobox(mode_row, textvariable=self.data_mode_var,
                     values=["十六进制", "UTF-8 文本"], state="readonly", width=14).pack(side="left", padx=5)
        self.data = scrolledtext.ScrolledText(box, height=7, wrap="word", font=("Consolas", 10))
        self.data.pack(fill="both", expand=True)
        self.data.insert("1.0", "")
        lower = ttk.Frame(box)
        lower.pack(fill="x", pady=(8, 0))
        ttk.Label(lower, text="序号").pack(side="left")
        self.sequence_var = tk.StringVar(value="0")
        ttk.Entry(lower, textvariable=self.sequence_var, width=8).pack(side="left", padx=4)
        ttk.Label(lower, text="分组(文件)").pack(side="left")
        self.grouping_var = tk.StringVar(value="3")
        ttk.Entry(lower, textvariable=self.grouping_var, width=6).pack(side="left", padx=4)
        ttk.Button(lower, text="打包并发送", command=self.send_packet).pack(side="right")
        self.packet_preview = tk.StringVar(value="待发送的协议内容会显示在这里")
        ttk.Label(box, textvariable=self.packet_preview, wraplength=420).pack(anchor="w", pady=(8, 0))

    def build_receive_panel(self, parent):
        top = ttk.LabelFrame(parent, text="板端网口接收内容", padding=8)
        top.pack(fill="both", expand=True)
        self.receive = scrolledtext.ScrolledText(top, wrap="word", state="disabled", font=("Consolas", 10))
        self.receive.pack(fill="both", expand=True)
        bottom = ttk.LabelFrame(parent, text="板端原始状态 / UART 输出", padding=8)
        bottom.pack(fill="both", expand=True, pady=(10, 0))
        self.raw = scrolledtext.ScrolledText(bottom, height=9, wrap="word", state="disabled", font=("Consolas", 9))
        self.raw.pack(fill="both", expand=True)

    def channel_changed(self, _event=None):
        src, dst, iface, _priority = CHANNELS[self.channel_var.get()]
        self.src_var.set(src)
        self.dst_var.set(dst)
        self.iface_var.set(iface)

    def append(self, widget, text):
        widget.configure(state="normal")
        widget.insert("end", text + "\n")
        widget.see("end")
        widget.configure(state="disabled")

    def connect(self):
        try:
            self.disconnect()
            self.serial = SerialPort(self.port_var.get().strip(), int(self.baud_var.get()), self.rx_bytes.put)
            self.serial.open()
            self.status.set(f"已连接 {self.port_var.get()} / {self.baud_var.get()} 8N1")
            self.serial.command("echo __RK_ETH_UART_READY__")
        except Exception as exc:
            self.disconnect()
            messagebox.showerror("UART 连接失败", str(exc))

    def disconnect(self):
        if self.serial:
            self.serial.close()
        self.serial = None
        if self.status.get() != "未连接":
            self.status.set("未连接")

    def shell(self, command: str):
        if self.serial:
            self.serial.command(command)
            self.append(self.raw, f"$ {command}")
        else:
            raise RuntimeError("请先连接 UART")

    def start_gateway(self):
        command = ("cd /home/youyeetoo/youyeetoo_eth_gateway; "
                   "if ! pgrep -f '^./youyeetoo_eth_gateway ' >/dev/null; then "
                   "nohup ./youyeetoo_eth_gateway --payload-iface eth0 --x-iface eth1 --port 47000 "
                   ">/dev/null 2>&1 & fi; echo GATEWAY_READY; ip -br addr show eth0 eth1")
        try:
            self.shell(command)
        except Exception as exc:
            messagebox.showerror("启动失败", str(exc))

    def read_status(self):
        try:
            self.shell("echo __STATUS__; ip -br addr show eth0 eth1; ip -4 route; tc qdisc show dev eth0; tc qdisc show dev eth1; echo __STATUS_END__")
        except Exception as exc:
            messagebox.showerror("读取失败", str(exc))

    def send_packet(self):
        try:
            src = str(ipaddress.ip_address(self.src_var.get().strip()))
            dst = str(ipaddress.ip_address(self.dst_var.get().strip()))
            iface = self.iface_var.get().strip()
            port = int(self.port_var_udp.get())
            if not 1 <= port <= 65535 or not re.fullmatch(r"[A-Za-z0-9_.:-]+", iface):
                raise ValueError("网口或端口无效")
            kind = self.kind_var.get()
            text = self.data.get("1.0", "end-1c").strip()
            input_bytes = (text.encode("utf-8") if self.data_mode_var.get() == "UTF-8 文本"
                           else (bytes.fromhex(text) if text else b""))
            if kind == "协议应答 1ACF":
                raw = command_packet(self.opcode_var.get(), hex_dump(input_bytes), self.sequence_var.get(), RESPONSE_MARKER)
            elif kind == "协议指令 EB90":
                raw = command_packet(self.opcode_var.get(), hex_dump(input_bytes), self.sequence_var.get(), COMMAND_MARKER)
            elif kind == "文件分段":
                raw = file_packet(self.opcode_var.get(), hex_dump(input_bytes), self.grouping_var.get(), self.sequence_var.get())
            else:
                raw = input_bytes
                if not raw:
                    raise ValueError("业务透传数据不能为空")
            self.packet_preview.set(f"实际发送 {len(raw)} 字节: {hex_dump(raw[:96])}")
            if not self.serial:
                raise RuntimeError("请先连接 UART")
            encoded = hex_dump(raw).replace(" ", "")
            py = ("import socket,binascii; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); "
                  "s.setsockopt(socket.SOL_IP,15,1); "
                  f"s.setsockopt(socket.SOL_SOCKET,25,{(iface + chr(0)).encode()!r}); "
                  f"s.bind(({src!r},0)); s.sendto(binascii.unhexlify({encoded!r}),({dst!r},{port})); s.close()")
            command = (f"( added=0; ip -4 addr show dev {iface} | grep -qwF {src} || "
                       f"{{ ip addr add {src}/32 dev {iface}; added=1; }}; "
                       f"python3 -c {quote_shell(py)}; rc=$?; "
                       f"[ $added -eq 0 ] || ip addr del {src}/32 dev {iface}; "
                       f"echo __RK_ETH_SEND_RC__=$rc )")
            self.shell(command)
            self.append(self.receive, f"TX iface={iface} {src} -> {dst}:{port} kind={kind} bytes={len(raw)}")
            self.append(self.receive, f"TX data={hex_dump(raw)}")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def process_serial(self):
        while True:
            try:
                data = self.rx_bytes.get_nowait()
            except queue.Empty:
                break
            self.rx_text += data.decode("utf-8", errors="replace").replace("\r", "")
            if len(self.rx_text) > 30000:
                self.rx_text = self.rx_text[-15000:]
            while "\n" in self.rx_text:
                line, self.rx_text = self.rx_text.split("\n", 1)
                self.parse_line(line)
        self.root.after(80, self.process_serial)

    def parse_line(self, line: str):
        clean = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", line).strip()
        if not clean or clean.startswith("$ "):
            return
        if clean.startswith("RX iface="):
            if clean in self.seen:
                return
            self.seen.add(clean)
            if len(self.seen) > 500:
                self.seen = set(list(self.seen)[-250:])
            match = re.search(r"RX iface=(\S+) src=(\S+) dst=(\S+) proto=(\d+) bytes=(\d+) priority=(\d+) flow=(.*)", clean)
            if match:
                iface, src, dst, proto, size, priority, flow = match.groups()
                data_match = re.search(r" data_hex=([0-9A-Fa-f.]+)", flow)
                if data_match:
                    hex_text = data_match.group(1).replace("...", "")
                    flow = flow[:data_match.start()].strip()
                    try:
                        payload = bytes.fromhex(hex_text)
                        printable = "".join(chr(value) if 32 <= value < 127 else "." for value in payload)
                    except ValueError:
                        printable = ""
                    self.append(self.receive, f"RX iface={iface} {src} -> {dst} proto={proto} bytes={size} priority={priority} flow={flow}")
                    self.append(self.receive, f"RX data_hex={hex_text}")
                    if printable:
                        self.append(self.receive, f"RX text={printable}")
                else:
                    self.append(self.receive, f"RX iface={iface} {src} -> {dst} proto={proto} bytes={size} priority={priority} flow={flow}")
            else:
                self.append(self.receive, clean)
        elif "protocol=0x" in clean or "protocol=opaque" in clean or "checksum=" in clean:
            self.append(self.receive, f"    DATA {clean}")
        elif (clean.startswith("__STATUS") or clean.startswith("GATEWAY_READY")
              or clean.startswith("Linux ") or clean.startswith("__RK_ETH_SEND_RC__")):
            self.append(self.raw, clean)
        elif "qdisc " in clean or clean.startswith("eth0") or clean.startswith("eth1") or "via " in clean:
            self.append(self.raw, clean)

    def poll_board_log(self):
        if self.serial and time.monotonic() - self.last_poll > 0.4:
            self.last_poll = time.monotonic()
            try:
                self.serial.command("tail -n 80 /home/youyeetoo/youyeetoo_eth_gateway/youyeetoo_eth_gateway.log")
            except Exception:
                pass
        self.root.after(500, self.poll_board_log)

    def close(self):
        self.disconnect()
        self.root.destroy()


def quote_shell(value: str) -> str:
    # Single-quoted shell argument; the generated command contains no single quote.
    return "'" + value.replace("'", "'\\''") + "'"


if __name__ == "__main__":
    if tk is None:
        raise SystemExit("GUI mode requires a Python installation with tkinter")
    app_root = tk.Tk()
    App(app_root)
    app_root.mainloop()
