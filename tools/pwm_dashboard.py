#!/usr/bin/env python3
"""
PWM Verification Dashboard for STM32F411 Frequency Counter.

Connects to the board via USB CDC serial, controls PWM outputs (PA8/PB6),
and displays captured frequency/duty measurements from PA15.
Wire a PWM output to PA15 to verify loopback.

Uses SCPI command protocol (IEEE 488.2 / SCPI subsystem hierarchy).

Requires: pyserial  (pip install pyserial)
"""

import json
import os
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk, font as tkfont

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    raise SystemExit(
        "pyserial is required.  Install with:  pip install pyserial"
    )

# ---------------------------------------------------------------------------
#  Constants
# ---------------------------------------------------------------------------

TARGET_VID = 0x0483
TARGET_PID = 0x5741
SETTINGS_PATH = os.path.join(os.path.expanduser("~"), ".stm32_pwm_dashboard.json")

FREQ_STEPS = [1, 10, 100, 1000, 10000]
DUTY_STEPS = [0.1, 1.0, 5.0, 10.0]
REFRESH_INTERVALS = [100, 200, 500, 1000, 2000]

CONSOLE_MAX_LINES = 5000

# ---------------------------------------------------------------------------
#  Response Parser (SCPI format)
# ---------------------------------------------------------------------------

# MEAS:ALL? response: <capture>,<freq>,<duty>,<period>,<pulse>
# Example: ON,1000,50.00,96000,48000
RE_MEAS_ALL = re.compile(
    r"(ON|OFF),(\d+),(\d+)\.(\d+),(\d+),(\d+)"
)

# SYST:NAME? response: "nickname"\r\n
RE_NICKNAME = re.compile(r'"([^"]{0,16})"')


class ResponseParser:
    """Parse SCPI responses from the frequency counter firmware."""

    @staticmethod
    def parse_meas_all(text):
        """Return (dict, end_index) for the last MEAS:ALL? response, or (None, -1).

        Uses the *last* match so the display always shows the most recent
        measurement when multiple responses arrive between poll cycles.
        """
        last_match = None
        for m in RE_MEAS_ALL.finditer(text):
            last_match = m
        if not last_match:
            return None, -1
        result = {
            "capture_on": last_match.group(1) == "ON",
            "freq_hz": int(last_match.group(2)),
            "duty_pct": int(last_match.group(3))
            + int(last_match.group(4)) / 100.0,
            "period_ticks": int(last_match.group(5)),
            "pulse_ticks": int(last_match.group(6)),
        }
        return result, last_match.end()

    @staticmethod
    def parse_nickname(text):
        """Return (nickname_str, end_index) from SYST:NAME? response, or (None, -1)."""
        last = None
        for m in RE_NICKNAME.finditer(text):
            last = m
        if not last:
            return None, -1
        return last.group(1), last.end()


# ---------------------------------------------------------------------------
#  Serial Manager
# ---------------------------------------------------------------------------


class SerialManager:
    """Thread-safe serial port manager with background reader."""

    def __init__(self):
        self._port = None
        self._rx_queue = queue.Queue()
        self._reader_thread = None
        self._running = False
        self._disconnected_flag = False

    # -- public API --

    def connect(self, port_name, baudrate=115200):
        self.disconnect()
        self._port = serial.Serial(port_name, baudrate, timeout=0.05)
        self._disconnected_flag = False
        self._running = True
        self._reader_thread = threading.Thread(
            target=self._reader_loop, daemon=True
        )
        self._reader_thread.start()

    def disconnect(self):
        self._running = False
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1.0)
        self._reader_thread = None
        if self._port and self._port.is_open:
            try:
                self._port.close()
            except Exception:
                pass
        self._port = None
        # drain remaining
        while not self._rx_queue.empty():
            try:
                self._rx_queue.get_nowait()
            except queue.Empty:
                break

    def send(self, text):
        if self._port and self._port.is_open:
            self._port.write(text.encode("ascii", errors="replace"))

    def poll_rx(self):
        """Return accumulated received text or empty string."""
        chunks = []
        while True:
            try:
                chunks.append(self._rx_queue.get_nowait())
            except queue.Empty:
                break
        return "".join(chunks)

    def is_connected(self):
        return (
            self._port is not None
            and self._port.is_open
            and not self._disconnected_flag
        )

    def was_disconnected(self):
        return self._disconnected_flag

    @staticmethod
    def list_ports():
        """Return list of port-info dicts with pyserial metadata."""
        result = []
        for p in serial.tools.list_ports.comports():
            is_target = p.vid == TARGET_VID and p.pid == TARGET_PID
            info = {
                "device": p.device,
                "description": p.description,
                "is_target": is_target,
                "vid": p.vid,
                "pid": p.pid,
                "serial_number": p.serial_number,
                "manufacturer": p.manufacturer,
                "product": p.product,
                "hwid": p.hwid,
            }
            if is_target:
                sn_short = (p.serial_number or "?")[:8]
                info["display"] = f"{p.device} - FC-411 [{sn_short}]"
            else:
                info["display"] = f"{p.device} - {p.description}"
            result.append(info)
        # target devices first, then alphabetically by device name
        result.sort(key=lambda x: (not x["is_target"], x["device"]))
        return result

    # -- internal --

    def _reader_loop(self):
        while self._running:
            try:
                if self._port and self._port.is_open and self._port.in_waiting:
                    data = self._port.read(self._port.in_waiting)
                    if data:
                        text = data.decode("ascii", errors="replace")
                        self._rx_queue.put(text)
                else:
                    time.sleep(0.01)
            except (serial.SerialException, OSError):
                self._disconnected_flag = True
                self._running = False
                break


# ---------------------------------------------------------------------------
#  PWM Control Panel (reusable widget)
# ---------------------------------------------------------------------------


class PwmControlPanel(ttk.LabelFrame):
    """Independent control panel for one PWM channel."""

    def __init__(self, parent, channel_name, pin_name, send_cmd_cb, **kwargs):
        super().__init__(parent, text=f"  {channel_name} ({pin_name})  ", **kwargs)
        self._ch = channel_name.upper()  # "PWM1" or "PWM2"
        self._send = send_cmd_cb
        self._enabled = False
        self._freq_debounce_id = None
        self._duty_debounce_id = None

        self._build_ui()

    # -- UI construction --

    def _build_ui(self):
        pad = dict(padx=4, pady=2)

        # --- Frequency row ---
        row = 0
        ttk.Label(self, text="Freq (Hz):").grid(row=row, column=0, sticky="w", **pad)
        self._freq_var = tk.StringVar(value="1000")
        self._freq_entry = ttk.Entry(self, textvariable=self._freq_var, width=12)
        self._freq_entry.grid(row=row, column=1, sticky="ew", **pad)
        self._freq_entry.bind("<Return>", lambda e: self._apply_from_entry())

        ttk.Label(self, text="Step:").grid(row=row, column=2, sticky="e", **pad)
        self._freq_step_var = tk.StringVar(value="100")
        freq_step_cb = ttk.Combobox(
            self,
            textvariable=self._freq_step_var,
            values=[str(s) for s in FREQ_STEPS],
            width=6,
            state="readonly",
        )
        freq_step_cb.grid(row=row, column=3, sticky="w", **pad)
        freq_step_cb.bind("<<ComboboxSelected>>", lambda e: self._update_freq_slider_range())

        # --- Frequency slider ---
        row = 1
        self._freq_slider = ttk.Scale(
            self, from_=0, to=100000, orient="horizontal",
            command=self._on_freq_slider,
        )
        self._freq_slider.grid(row=row, column=0, columnspan=4, sticky="ew", **pad)
        self._freq_slider.set(1000)
        self._update_freq_slider_range()

        # --- Duty row ---
        row = 2
        ttk.Label(self, text="Duty (%):").grid(row=row, column=0, sticky="w", **pad)
        self._duty_var = tk.StringVar(value="50.00")
        self._duty_entry = ttk.Entry(self, textvariable=self._duty_var, width=12)
        self._duty_entry.grid(row=row, column=1, sticky="ew", **pad)
        self._duty_entry.bind("<Return>", lambda e: self._apply_from_entry())

        ttk.Label(self, text="Step:").grid(row=row, column=2, sticky="e", **pad)
        self._duty_step_var = tk.StringVar(value="1.0")
        duty_step_cb = ttk.Combobox(
            self,
            textvariable=self._duty_step_var,
            values=[str(s) for s in DUTY_STEPS],
            width=6,
            state="readonly",
        )
        duty_step_cb.grid(row=row, column=3, sticky="w", **pad)

        # --- Duty slider ---
        row = 3
        self._duty_slider = ttk.Scale(
            self, from_=0, to=100, orient="horizontal",
            command=self._on_duty_slider,
        )
        self._duty_slider.grid(row=row, column=0, columnspan=4, sticky="ew", **pad)
        self._duty_slider.set(50)

        # --- Enable/Disable button ---
        row = 4
        self._enable_btn = ttk.Button(
            self, text="Enable", command=self._toggle_enable
        )
        self._enable_btn.grid(row=row, column=0, columnspan=4, sticky="ew", **pad)

        # column weights
        self.columnconfigure(1, weight=1)

    # -- slider callbacks --

    def _on_freq_slider(self, value):
        step = int(self._freq_step_var.get())
        rounded = round(float(value) / step) * step
        rounded = max(step, rounded)  # at least 1 step
        self._freq_var.set(str(int(rounded)))
        # debounce
        if self._freq_debounce_id is not None:
            self.after_cancel(self._freq_debounce_id)
        self._freq_debounce_id = self.after(300, self._apply_pwm)

    def _on_duty_slider(self, value):
        step = float(self._duty_step_var.get())
        rounded = round(float(value) / step) * step
        rounded = max(0.0, min(100.0, rounded))
        self._duty_var.set(f"{rounded:.2f}")
        # debounce
        if self._duty_debounce_id is not None:
            self.after_cancel(self._duty_debounce_id)
        self._duty_debounce_id = self.after(300, self._apply_pwm)

    def _update_freq_slider_range(self):
        step = int(self._freq_step_var.get())
        max_val = step * 1000
        self._freq_slider.configure(from_=0, to=max_val)
        # clamp current value
        try:
            cur = int(self._freq_var.get())
            if cur > max_val:
                self._freq_slider.set(max_val)
            else:
                self._freq_slider.set(cur)
        except ValueError:
            pass

    # -- apply --

    def _apply_from_entry(self):
        """Apply immediately from entry fields and sync sliders."""
        try:
            freq = int(self._freq_var.get())
            self._freq_slider.set(freq)
        except ValueError:
            pass
        try:
            duty = float(self._duty_var.get())
            self._duty_slider.set(duty)
        except ValueError:
            pass
        self._apply_pwm()

    def _apply_pwm(self):
        """Send staged freq + duty + enable to device via SCPI commands."""
        if not self._enabled:
            return
        try:
            freq_hz = int(self._freq_var.get())
        except ValueError:
            freq_hz = 1000
        try:
            duty_pct = float(self._duty_var.get())
        except ValueError:
            duty_pct = 50.0
        duty_centipct = int(round(duty_pct * 100))
        duty_centipct = max(0, min(10000, duty_centipct))

        self._send(f"SOUR:{self._ch}:FREQ {freq_hz}\n")
        self._send(f"SOUR:{self._ch}:DUTY {duty_centipct}\n")
        self._send(f"SOUR:{self._ch}:ENAB 1\n")

    def _toggle_enable(self):
        self._enabled = not self._enabled
        self._enable_btn.configure(
            text="Disable" if self._enabled else "Enable"
        )
        if self._enabled:
            self._apply_pwm()
        else:
            self._send(f"SOUR:{self._ch}:ENAB 0\n")

    # -- state get/set for persistence --

    def get_state(self):
        return {
            "freq": self._freq_var.get(),
            "duty": self._duty_var.get(),
            "freq_step": self._freq_step_var.get(),
            "duty_step": self._duty_step_var.get(),
            "enabled": self._enabled,
        }

    def set_state(self, state):
        if "freq" in state:
            self._freq_var.set(state["freq"])
            try:
                self._freq_slider.set(int(state["freq"]))
            except (ValueError, tk.TclError):
                pass
        if "duty" in state:
            self._duty_var.set(state["duty"])
            try:
                self._duty_slider.set(float(state["duty"]))
            except (ValueError, tk.TclError):
                pass
        if "freq_step" in state:
            self._freq_step_var.set(state["freq_step"])
            self._update_freq_slider_range()
        if "duty_step" in state:
            self._duty_step_var.set(state["duty_step"])
        if "enabled" in state:
            self._enabled = state["enabled"]
            self._enable_btn.configure(
                text="Disable" if self._enabled else "Enable"
            )

    def set_controls_enabled(self, enabled):
        state = "!disabled" if enabled else "disabled"
        for child in self.winfo_children():
            try:
                child.state([state])
            except (AttributeError, tk.TclError):
                pass


# ---------------------------------------------------------------------------
#  Main Application
# ---------------------------------------------------------------------------


class PwmDashboardApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("STM32F411 PWM Verification Dashboard")
        self.minsize(880, 640)

        self._serial = SerialManager()
        self._rx_buffer = ""
        self._auto_refresh = tk.BooleanVar(value=True)
        self._refresh_interval = tk.StringVar(value="500")
        self._refresh_after_id = None
        self._capture_on = True  # mirrors device state
        self._connected_device_info = None
        self._pending_nickname_query = False

        self._build_fonts()
        self._build_ui()
        self._load_settings()
        self._set_controls_enabled(False)
        self._poll_rx()
        self._schedule_refresh()

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # -- fonts --

    def _build_fonts(self):
        self._font_big = tkfont.Font(family="Consolas", size=22, weight="bold")
        self._font_med = tkfont.Font(family="Consolas", size=13)
        self._font_console = tkfont.Font(family="Consolas", size=10)

    # -- UI layout --

    def _build_ui(self):
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=0)  # measurements
        self.rowconfigure(3, weight=1)  # console expands

        self._build_connection_panel()
        self._build_pwm_panels()
        self._build_measurements_panel()
        self._build_console_panel()

    # --- Connection Panel ---

    def _build_connection_panel(self):
        frame = ttk.LabelFrame(self, text="  Connection  ")
        frame.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 2))
        frame.columnconfigure(1, weight=1)

        ttk.Label(frame, text="Port:").grid(row=0, column=0, padx=4, pady=4)
        self._port_var = tk.StringVar()
        self._port_combo = ttk.Combobox(
            frame, textvariable=self._port_var, width=40
        )
        self._port_combo.grid(row=0, column=1, sticky="ew", padx=4, pady=4)

        self._refresh_ports_btn = ttk.Button(
            frame, text="Refresh", width=8, command=self._refresh_ports
        )
        self._refresh_ports_btn.grid(row=0, column=2, padx=4, pady=4)

        self._connect_btn = ttk.Button(
            frame, text="Connect", width=10, command=self._toggle_connect
        )
        self._connect_btn.grid(row=0, column=3, padx=4, pady=4)

        self._status_label = ttk.Label(
            frame, text="  Disconnected  ", foreground="red"
        )
        self._status_label.grid(row=0, column=4, padx=8, pady=4)

        # --- Device info row (hidden until connected to FC-411) ---
        self._device_info_frame = ttk.Frame(frame)
        # row=1, span all columns
        self._device_info_frame.grid(row=1, column=0, columnspan=5, sticky="ew", padx=4, pady=(0, 4))
        self._device_info_frame.grid_remove()  # hidden by default

        self._vid_pid_label = ttk.Label(
            self._device_info_frame, text="VID: ----  PID: ----",
            font=("Consolas", 9),
        )
        self._vid_pid_label.pack(side="left", padx=(0, 6))

        ttk.Separator(self._device_info_frame, orient="vertical").pack(
            side="left", fill="y", padx=6, pady=2
        )

        self._serial_label = ttk.Label(
            self._device_info_frame, text="SN: ----------------",
            font=("Consolas", 9),
        )
        self._serial_label.pack(side="left", padx=(0, 6))

        ttk.Separator(self._device_info_frame, orient="vertical").pack(
            side="left", fill="y", padx=6, pady=2
        )

        ttk.Label(self._device_info_frame, text="Nickname:").pack(side="left", padx=(0, 2))
        self._nickname_var = tk.StringVar()
        self._nickname_entry = ttk.Entry(
            self._device_info_frame, textvariable=self._nickname_var, width=18,
        )
        self._nickname_entry.pack(side="left", padx=2)
        self._nickname_entry.bind("<Return>", lambda e: self._apply_nickname())

        self._nickname_apply_btn = ttk.Button(
            self._device_info_frame, text="Apply", width=6, command=self._apply_nickname,
        )
        self._nickname_apply_btn.pack(side="left", padx=2)

        self._nickname_reset_btn = ttk.Button(
            self._device_info_frame, text="Reset", width=6, command=self._reset_nickname,
        )
        self._nickname_reset_btn.pack(side="left", padx=2)

        self._refresh_ports()

    def _refresh_ports(self):
        ports = SerialManager.list_ports()
        display = [info["display"] for info in ports]
        self._port_combo["values"] = display
        self._port_map = {info["display"]: info for info in ports}
        # auto-select first target device
        for info in ports:
            if info["is_target"]:
                self._port_var.set(info["display"])
                return
        if display and not self._port_var.get():
            self._port_var.set(display[0])

    def _toggle_connect(self):
        if self._serial.is_connected():
            self._serial.disconnect()
            self._update_connection_status(False)
        else:
            desc = self._port_var.get()
            info = self._port_map.get(desc)
            port = info["device"] if info else desc
            try:
                self._serial.connect(port)
                self._connected_device_info = info
                self._update_connection_status(True)
                # show device info and query nickname for FC-411 devices
                if info and info["is_target"]:
                    self._show_device_info(info)
                    self._query_nickname()
            except Exception as e:
                self._console_append(f"[ERROR] {e}\n")
                self._update_connection_status(False)

    def _update_connection_status(self, connected):
        if connected:
            self._status_label.configure(
                text="  Connected  ", foreground="green"
            )
            self._connect_btn.configure(text="Disconnect")
            self._set_controls_enabled(True)
            self._schedule_refresh()
        else:
            self._status_label.configure(
                text="  Disconnected  ", foreground="red"
            )
            self._connect_btn.configure(text="Connect")
            self._set_controls_enabled(False)
            self._cancel_refresh()
            self._hide_device_info()

    # --- Device Info helpers ---

    def _show_device_info(self, info):
        vid = info.get("vid") or 0
        pid = info.get("pid") or 0
        sn = info.get("serial_number") or "?"
        self._vid_pid_label.configure(text=f"VID: {vid:04X}  PID: {pid:04X}")
        self._serial_label.configure(text=f"SN: {sn}")
        self._nickname_var.set("")
        self._device_info_frame.grid()

    def _hide_device_info(self):
        self._device_info_frame.grid_remove()
        self._connected_device_info = None
        self._pending_nickname_query = False
        self._nickname_var.set("")

    def _query_nickname(self):
        self._pending_nickname_query = True
        self._send_cmd("SYST:NAME?\n")

    def _apply_nickname(self):
        nick = self._nickname_var.get().strip()[:16]
        if nick:
            self._pending_nickname_query = True
            self._send_cmd(f'SYST:NAME "{nick}"\n')

    def _reset_nickname(self):
        self._pending_nickname_query = True
        self._send_cmd("SYST:NAME:DEF\n")

    def _try_parse_nickname(self):
        nick, end = ResponseParser.parse_nickname(self._rx_buffer)
        if nick is not None:
            self._nickname_var.set(nick)
            self._pending_nickname_query = False
            self._rx_buffer = self._rx_buffer[end:]

    # --- PWM Panels ---

    def _build_pwm_panels(self):
        frame = ttk.Frame(self)
        frame.grid(row=1, column=0, sticky="ew", padx=6, pady=2)
        frame.columnconfigure(0, weight=1)
        frame.columnconfigure(1, weight=1)

        self._pwm1_panel = PwmControlPanel(
            frame, "PWM1", "PA8", self._send_cmd
        )
        self._pwm1_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 3))

        self._pwm2_panel = PwmControlPanel(
            frame, "PWM2", "PB6", self._send_cmd
        )
        self._pwm2_panel.grid(row=0, column=1, sticky="nsew", padx=(3, 0))

    # --- Measurements Panel ---

    def _build_measurements_panel(self):
        frame = ttk.LabelFrame(self, text="  Captured Measurements  ")
        frame.grid(row=2, column=0, sticky="ew", padx=6, pady=2)
        frame.columnconfigure(0, weight=1)
        frame.columnconfigure(1, weight=1)

        # big labels row
        big_frame = ttk.Frame(frame)
        big_frame.grid(row=0, column=0, columnspan=2, sticky="ew", padx=4, pady=4)
        big_frame.columnconfigure(0, weight=1)
        big_frame.columnconfigure(1, weight=1)

        self._freq_label = ttk.Label(
            big_frame, text="Freq: --- Hz", font=self._font_big, anchor="center"
        )
        self._freq_label.grid(row=0, column=0, sticky="ew", padx=8)

        self._duty_label = ttk.Label(
            big_frame, text="Duty: ---%", font=self._font_big, anchor="center"
        )
        self._duty_label.grid(row=0, column=1, sticky="ew", padx=8)

        # small labels row
        small_frame = ttk.Frame(frame)
        small_frame.grid(row=1, column=0, columnspan=2, sticky="ew", padx=4, pady=(0, 4))
        small_frame.columnconfigure(0, weight=1)
        small_frame.columnconfigure(1, weight=1)

        self._period_label = ttk.Label(
            small_frame, text="Period: --- ticks", font=self._font_med, anchor="center"
        )
        self._period_label.grid(row=0, column=0, sticky="ew", padx=8)

        self._pulse_label = ttk.Label(
            small_frame, text="Pulse: --- ticks", font=self._font_med, anchor="center"
        )
        self._pulse_label.grid(row=0, column=1, sticky="ew", padx=8)

        # controls row
        ctrl_frame = ttk.Frame(frame)
        ctrl_frame.grid(row=2, column=0, columnspan=2, sticky="ew", padx=4, pady=(0, 4))

        self._capture_btn = ttk.Button(
            ctrl_frame, text="Capture: ON", width=14,
            command=self._toggle_capture,
        )
        self._capture_btn.pack(side="left", padx=4)

        ttk.Label(ctrl_frame, text="Edge:").pack(side="left", padx=(8, 2))
        self._edge_var = tk.StringVar(value="Rising")
        self._edge_combo = ttk.Combobox(
            ctrl_frame,
            textvariable=self._edge_var,
            values=["Rising", "Falling"],
            width=8,
            state="readonly",
        )
        self._edge_combo.pack(side="left", padx=2)
        self._edge_combo.bind("<<ComboboxSelected>>", lambda e: self._apply_edge())

        ttk.Separator(ctrl_frame, orient="vertical").pack(
            side="left", fill="y", padx=6, pady=2
        )

        ttk.Checkbutton(
            ctrl_frame, text="Auto-refresh", variable=self._auto_refresh,
            command=self._schedule_refresh,
        ).pack(side="left", padx=4)

        ttk.Label(ctrl_frame, text="Interval:").pack(side="left", padx=(8, 2))
        interval_cb = ttk.Combobox(
            ctrl_frame,
            textvariable=self._refresh_interval,
            values=[str(v) + " ms" for v in REFRESH_INTERVALS],
            width=8,
            state="readonly",
        )
        interval_cb.pack(side="left", padx=2)
        interval_cb.bind("<<ComboboxSelected>>", lambda e: self._schedule_refresh())

        ttk.Button(
            ctrl_frame, text="Refresh Now", command=self._manual_refresh
        ).pack(side="left", padx=8)

    # --- Console Panel ---

    def _build_console_panel(self):
        frame = ttk.LabelFrame(self, text="  CDC Console (SCPI)  ")
        frame.grid(row=3, column=0, sticky="nsew", padx=6, pady=(2, 6))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(1, weight=1)

        # TX row
        tx_frame = ttk.Frame(frame)
        tx_frame.grid(row=0, column=0, sticky="ew", padx=4, pady=4)
        tx_frame.columnconfigure(1, weight=1)

        ttk.Label(tx_frame, text="TX:").grid(row=0, column=0, padx=(0, 4))
        self._tx_var = tk.StringVar()
        self._tx_entry = ttk.Entry(tx_frame, textvariable=self._tx_var)
        self._tx_entry.grid(row=0, column=1, sticky="ew")
        self._tx_entry.bind("<Return>", lambda e: self._send_tx())
        ttk.Button(tx_frame, text="Send", width=6, command=self._send_tx).grid(
            row=0, column=2, padx=(4, 0)
        )

        # RX area
        rx_frame = ttk.Frame(frame)
        rx_frame.grid(row=1, column=0, sticky="nsew", padx=4, pady=(0, 4))
        rx_frame.columnconfigure(0, weight=1)
        rx_frame.rowconfigure(0, weight=1)

        self._rx_text = tk.Text(
            rx_frame,
            wrap="word",
            state="disabled",
            font=self._font_console,
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#d4d4d4",
            height=10,
        )
        self._rx_text.grid(row=0, column=0, sticky="nsew")

        scrollbar = ttk.Scrollbar(rx_frame, orient="vertical", command=self._rx_text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self._rx_text.configure(yscrollcommand=scrollbar.set)

        # tag for sent commands
        self._rx_text.tag_configure("sent", foreground="#569cd6")

        # Clear button
        ttk.Button(frame, text="Clear", width=6, command=self._clear_console).grid(
            row=2, column=0, sticky="w", padx=4, pady=(0, 4)
        )

    # -- command sending --

    def _send_cmd(self, cmd):
        """Send a command string to the device. Shows it in the console."""
        if not self._serial.is_connected():
            return
        self._serial.send(cmd)
        # show in console (strip trailing \n for display)
        display = cmd.rstrip("\r\n")
        self._console_append(f"> {display}\n", tag="sent")

    def _send_tx(self):
        """Send text from the TX entry."""
        text = self._tx_var.get().strip()
        if text:
            self._send_cmd(text + "\n")
            self._tx_var.set("")

    # -- console output --

    def _console_append(self, text, tag=None):
        self._rx_text.configure(state="normal")
        if tag:
            self._rx_text.insert("end", text, tag)
        else:
            self._rx_text.insert("end", text)
        # trim
        line_count = int(self._rx_text.index("end-1c").split(".")[0])
        if line_count > CONSOLE_MAX_LINES:
            excess = line_count - CONSOLE_MAX_LINES
            self._rx_text.delete("1.0", f"{excess}.0")
        self._rx_text.configure(state="disabled")
        self._rx_text.see("end")

    def _clear_console(self):
        self._rx_text.configure(state="normal")
        self._rx_text.delete("1.0", "end")
        self._rx_text.configure(state="disabled")

    # -- polling --

    def _poll_rx(self):
        """Poll serial RX queue and process data."""
        # check for unexpected disconnect
        if self._serial.was_disconnected():
            self._serial.disconnect()
            self._update_connection_status(False)
            self._console_append("[DISCONNECTED]\n")
        else:
            text = self._serial.poll_rx()
            if text:
                self._console_append(text)
                self._rx_buffer += text
                if self._pending_nickname_query:
                    self._try_parse_nickname()
                self._try_parse_measurements()
                # trim buffer
                if len(self._rx_buffer) > 4096:
                    self._rx_buffer = self._rx_buffer[-2048:]

        self.after(50, self._poll_rx)

    def _try_parse_measurements(self):
        result, end = ResponseParser.parse_meas_all(self._rx_buffer)
        if result:
            self._freq_label.configure(
                text=f"Freq: {result['freq_hz']} Hz"
            )
            self._duty_label.configure(
                text=f"Duty: {result['duty_pct']:.2f}%"
            )
            self._period_label.configure(
                text=f"Period: {result['period_ticks']} ticks"
            )
            self._pulse_label.configure(
                text=f"Pulse: {result['pulse_ticks']} ticks"
            )
            # sync capture state from device
            if result["capture_on"] != self._capture_on:
                self._capture_on = result["capture_on"]
                self._update_capture_ui()
            # consume all parsed data up to the last match
            self._rx_buffer = self._rx_buffer[end:]

    # -- auto-refresh --

    def _get_refresh_ms(self):
        try:
            val = self._refresh_interval.get().replace(" ms", "")
            return int(val)
        except ValueError:
            return 500

    def _cancel_refresh(self):
        if self._refresh_after_id is not None:
            self.after_cancel(self._refresh_after_id)
            self._refresh_after_id = None

    def _schedule_refresh(self):
        self._cancel_refresh()
        if self._auto_refresh.get() and self._serial.is_connected() and self._capture_on:
            self._do_refresh()

    def _do_refresh(self):
        if self._serial.is_connected() and self._capture_on:
            self._send_cmd("MEAS:ALL?\n")
        if self._auto_refresh.get() and self._serial.is_connected() and self._capture_on:
            ms = self._get_refresh_ms()
            self._refresh_after_id = self.after(ms, self._do_refresh)

    def _manual_refresh(self):
        if self._serial.is_connected():
            self._send_cmd("MEAS:ALL?\n")

    def _toggle_capture(self):
        new_state = not self._capture_on
        cmd = "CAPT:ENAB ON\n" if new_state else "CAPT:ENAB OFF\n"
        self._send_cmd(cmd)
        self._capture_on = new_state
        self._update_capture_ui()
        # stop polling when capture off, resume when on
        if new_state:
            self._schedule_refresh()
        else:
            self._cancel_refresh()

    def _update_capture_ui(self):
        text = "Capture: ON" if self._capture_on else "Capture: OFF"
        self._capture_btn.configure(text=text)

    def _apply_edge(self):
        val = 0 if self._edge_var.get() == "Rising" else 1
        self._send_cmd(f"CAPT:EDGE {val}\n")

    # -- enable/disable controls --

    def _set_controls_enabled(self, enabled):
        self._pwm1_panel.set_controls_enabled(enabled)
        self._pwm2_panel.set_controls_enabled(enabled)
        state = "!disabled" if enabled else "disabled"
        self._capture_btn.state([state])

    # -- settings persistence --

    def _load_settings(self):
        try:
            with open(SETTINGS_PATH, "r") as f:
                s = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            return

        if "port" in s:
            self._port_var.set(s["port"])
        if "pwm1" in s:
            self._pwm1_panel.set_state(s["pwm1"])
        if "pwm2" in s:
            self._pwm2_panel.set_state(s["pwm2"])
        if "auto_refresh" in s:
            self._auto_refresh.set(s["auto_refresh"])
        if "refresh_interval" in s:
            self._refresh_interval.set(s["refresh_interval"])

    def _save_settings(self):
        s = {
            "port": self._port_var.get(),
            "pwm1": self._pwm1_panel.get_state(),
            "pwm2": self._pwm2_panel.get_state(),
            "auto_refresh": self._auto_refresh.get(),
            "refresh_interval": self._refresh_interval.get(),
        }
        try:
            with open(SETTINGS_PATH, "w") as f:
                json.dump(s, f, indent=2)
        except OSError:
            pass

    def _on_close(self):
        self._save_settings()
        self._serial.disconnect()
        self.destroy()


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app = PwmDashboardApp()
    app.mainloop()
