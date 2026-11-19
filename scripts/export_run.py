"""Recover/export a Chronvs CSV from an SD card to standard GPX (no dependencies)."""
import argparse
import csv
from datetime import datetime, timezone
import math
from pathlib import Path
from xml.sax.saxutils import escape


def export(source: Path, destination: Path) -> int:
    """Stream complete rows only; preserve segments and never overwrite files."""
    points = 0
    previous_utc = 0
    segment = None
    with source.open(encoding="utf-8", newline="") as src:
        header = next(csv.reader([src.readline()]))
        expected = "utc,elapsed_s,active_s,segment,latitude,longitude,speed_kmh,altitude_m,distance_m".split(",")
        if header != expected:
            raise ValueError("CSV Chronvs inválido")
        with destination.open("x", encoding="utf-8", newline="\n") as dst:
            dst.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            dst.write('<gpx version="1.1" creator="Chronvs" xmlns="http://www.topografix.com/GPX/1/1">\n')
            dst.write(f"<trk><name>{escape(source.stem)}</name><type>running</type>\n")
            for line in src:
                # An interrupted final write must not create a fabricated point.
                if not line.endswith("\n") or line.startswith("#"):
                    continue
                row = next(csv.reader([line]))
                if len(row) != len(expected):
                    raise ValueError("Linha incompleta no CSV")
                utc, _, _, part, lat, lon, _, alt, _ = row
                utc, part, lat, lon = int(utc), int(part), float(lat), float(lon)
                if utc <= previous_utc or not (math.isfinite(lat) and math.isfinite(lon) and -90 <= lat <= 90 and -180 <= lon <= 180):
                    raise ValueError("Ponto GPS inválido")
                timestamp = datetime.fromtimestamp(utc, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
                if part != segment:
                    if segment is not None:
                        dst.write("</trkseg>\n")
                    dst.write("<trkseg>\n")
                    segment = part
                dst.write(f'<trkpt lat="{lat:.7f}" lon="{lon:.7f}">')
                if alt:
                    altitude = float(alt)
                    if not math.isfinite(altitude):
                        raise ValueError("Altitude inválida")
                    dst.write(f"<ele>{altitude:.2f}</ele>")
                dst.write(f"<time>{timestamp}</time></trkpt>\n")
                points += 1
                previous_utc = utc
            if segment is not None:
                dst.write("</trkseg>\n")
            dst.write("</trk></gpx>\n")
    if not points:
        raise ValueError("Corrida sem pontos GPS")
    return points


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("gpx", type=Path)
    args = parser.parse_args()
    try:
        count = export(args.csv, args.gpx)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Falha: {error}\n")
    print(f"{count} pontos exportados para {args.gpx}")
