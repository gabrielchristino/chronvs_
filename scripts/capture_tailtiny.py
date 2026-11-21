"""Capture a bounded, redacted USB log from the dedicated TailTiny runtime."""
import argparse
from pathlib import Path
import re
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=45)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 300:
        parser.error("seconds must be in (0, 300]")
    connection = serial.Serial()
    connection.port, connection.baudrate = args.port, 115200
    connection.timeout = 0.25
    connection.dtr = connection.rts = False
    connection.open()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    connected = False
    with connection, args.output.open("x", encoding="utf-8") as log:
        if args.reset:
            connection.rts = True
            # usbser.sys needs a DTR update to propagate the new RTS level,
            # matching esptool's Windows HardReset implementation.
            connection.dtr = connection.dtr
            time.sleep(0.1)
            connection.rts = False
            connection.dtr = connection.dtr
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            raw = connection.readline().decode("utf-8", errors="replace")
            line = re.sub(r"tskey-[^\s\"']+", "[REDACTED]", raw)
            if not line:
                continue
            log.write(line)
            log.flush()
            if "chronvs_tail:" in line or "E (" in line or "W (" in line:
                print(line.rstrip(), flush=True)
            if "Connected! VPN IP:" in line or re.search(r"STATUS state=4 VPN=100\.", line):
                connected = True
    print(f"Capture connected={connected}: {args.output}", flush=True)
    return 0 if connected else 1


if __name__ == "__main__":
    raise SystemExit(main())
