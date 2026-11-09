"""Generate original analytic HDR inputs for published GI environment contracts."""
import argparse
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DIRECTORY = ROOT / "Assets/Textures/GIReference"


def encode(rgb):
    _, exponent = math.frexp(max(rgb))
    # DirectXTex decodes RGBE at (mantissa + 0.5); floor selects the nearest bin centre.
    scale = 256.0 / (2.0 ** exponent)
    return bytes([min(255, int(value * scale)) for value in rgb] + [exponent + 128])


def image(directional):
    width, height = 128, 64
    header = f"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n".encode("ascii")
    pixels = bytearray()
    for y in range(height):
        latitude = math.pi * (y + 0.5) / height
        for x in range(width):
            longitude = 2 * math.pi * ((x + 0.5) / width - 0.5)
            direction = (math.sin(latitude) * math.sin(longitude), math.cos(latitude),
                         math.sin(latitude) * math.cos(longitude))
            pixels.extend(encode(tuple(1 + 0.5 * value for value in direction) if directional else (1, 1, 1)))
    return header + pixels


def main(check):
    for name, directional in (("Constant.hdr", False), ("Directional.hdr", True)):
        expected = image(directional)
        path = DIRECTORY / name
        if check:
            if not path.is_file() or path.read_bytes() != expected:
                raise ValueError(f"Regenerate analytic environment: {path}")
        else:
            DIRECTORY.mkdir(parents=True, exist_ok=True)
            path.write_bytes(expected)
    print("Analytic GI environment inputs match generator.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    main(parser.parse_args().check)
