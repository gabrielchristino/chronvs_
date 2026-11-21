"""Capture the headless bench without toggling reset, logging until DONE."""
import argparse
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--seconds", type=float, default=180)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 300:
        parser.error("seconds must be in (0, 300]")
    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200
    connection.timeout = 0.25
    connection.dtr = False
    connection.rts = False
    args.output.parent.mkdir(parents=True, exist_ok=True)
    connection.open()
    done = False
    deadline = time.monotonic() + args.seconds
    with connection, args.output.open("x", encoding="utf-8") as log:
        while time.monotonic() < deadline:
            line = connection.readline().decode("utf-8", errors="replace")
            if not line:
                continue
            log.write(line)
            log.flush()
            if "hw_test:" in line or "E (" in line or "W (" in line:
                print(line.rstrip(), flush=True)
            if "hw_test: DONE " in line:
                done = True
                break
    print(f"Capture complete={done}: {args.output}", flush=True)
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
