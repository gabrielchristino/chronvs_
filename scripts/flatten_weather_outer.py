"""Keep a dark overscan band around the Clima face."""

from math import hypot
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "assets" / "weather" / "weather_face_base.png"
SIZE = 412
CENTER = (SIZE - 1) / 2
RADIUS = 198.5
OUTSIDE = (0, 0, 0)


def main() -> None:
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(IMAGE), "-f", "rawvideo",
         "-pix_fmt", "rgba", "-"], check=True, stdout=subprocess.PIPE
    )
    pixels = bytearray(result.stdout)
    if len(pixels) != SIZE * SIZE * 4:
        raise ValueError("Weather base must be 412 x 412 pixels")
    for y in range(SIZE):
        for x in range(SIZE):
            if hypot(x - CENTER, y - CENTER) > RADIUS:
                offset = 4 * (y * SIZE + x)
                pixels[offset:offset + 4] = bytes((*OUTSIDE, 255))
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba",
         "-s", f"{SIZE}x{SIZE}", "-i", "-", "-frames:v", "1", str(IMAGE)],
        input=pixels, check=True
    )


if __name__ == "__main__":
    main()
