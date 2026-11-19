"""Capture an existing USB serial session without requesting a board reset."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 0 < args.seconds <= 300:
        parser.error("--seconds must be between 0 and 300")
    output = args.output or Path("logs") / (datetime.now(timezone.utc).strftime("display-scan-%Y%m%dT%H%M%SZ.log"))
    output.parent.mkdir(parents=True, exist_ok=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        with connection, output.open("x", encoding="utf-8") as log:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                line = connection.readline().decode("utf-8", errors="replace")
                if line:
                    log.write(line)
                    log.flush()
                    if "display_scan:" in line or "display_bench:" in line or "display_perf:" in line or "panel_test:" in line or "panel_part:" in line:
                        print(line.rstrip(), flush=True)
    except (OSError, serial.SerialException) as error:
        parser.exit(1, f"Capture stopped: {error}; partial log: {output}\n")
    print(f"Local log: {output}")


if __name__ == "__main__":
    main()
