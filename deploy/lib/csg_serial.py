# Copyright (c) 2026 Anton Bespalov. All rights reserved.
# Licensed under the terms specified by the MIT License
# Part of the csiGarland project https://github.com/ABespalov/csigarland
#
# Purpose: The USB serial ports of this computer, and a board's log read from one, on Linux and macOS.
# Contributors: None
#
# Runs under a Python with pyserial, as ESP-IDF's has once deploy/flash-device.sh has run: Windows
# asks the system for this, where Linux and macOS have pyserial ask theirs.
#
#   csg_serial.py candidates                      each USB serial port: port, VID, PID, identity hash
#   csg_serial.py monitor <port> <log> <seconds>  the log until the board goes; exit 3 if never opened
from __future__ import annotations

import datetime
import hashlib
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.stderr.write("This Python has no pyserial.\n")
    sys.exit(4)


def candidates() -> None:
    for port in list_ports.comports():
        if port.vid is None or port.pid is None:
            continue
        # The USB serial of this chip is its MAC address, so only a hash is ever said.
        serial_number = (port.serial_number or "").strip().upper()
        identity = hashlib.sha256(serial_number.encode("utf-8")).hexdigest().upper() if serial_number else ""
        sys.stdout.write(f"{port.device}\t{port.vid:04X}\t{port.pid:04X}\t{identity}\n")


def monitor(device: str, log_path: str, open_seconds: float) -> int:
    """Returns 0 if the port was ever opened: one that attached and then lost the device did its job."""
    port = serial.Serial()
    port.port = device
    port.baudrate = 115200
    port.timeout = 0.5
    # On USB Serial/JTAG these two lines are wired to the chip's reset and boot logic, so both stay
    # released: reading the log must not reset the board.
    port.dtr = False
    port.rts = False
    deadline = time.monotonic() + open_seconds
    while True:
        try:
            port.open()
            break
        except (serial.SerialException, OSError):
            # The port vanishes for a moment while the board re-enumerates.
            if time.monotonic() >= deadline:
                return 3
            time.sleep(0.25)
    sys.stderr.write(f"Monitoring {device}, logging to {log_path}. Press Ctrl+C to stop.\n")
    with open(log_path, "a", encoding="utf-8", buffering=1) as log:
        log.write(f"--- csiGarland monitor {device} opened {datetime.datetime.now().astimezone().isoformat()} ---\n")
        pending = b""
        try:
            while port.is_open:
                try:
                    chunk = port.read(port.in_waiting or 1)
                except (serial.SerialException, OSError):
                    sys.stderr.write("Device disconnected.\n")
                    break
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    # The firmware logs ASCII, but the ROM and a crash can send anything.
                    clean = line.decode("utf-8", errors="replace").rstrip()
                    sys.stdout.write(clean + "\n")
                    sys.stdout.flush()
                    log.write(clean + "\n")
        finally:
            if port.is_open:
                port.close()
    return 0


def main(argv: list[str]) -> int:
    if argv[:1] == ["candidates"]:
        candidates()
        return 0
    if argv[:1] == ["monitor"] and len(argv) == 4:
        return monitor(argv[1], argv[2], float(argv[3]))
    sys.stderr.write("csg_serial.py candidates | monitor <port> <log> <seconds>\n")
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)
