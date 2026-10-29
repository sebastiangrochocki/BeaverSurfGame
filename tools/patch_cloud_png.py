"""Patch the generated indexed GBC background when the Rust tool is unavailable."""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "gbc" / "res" / "bg.png"

ROWS = [
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAABBBBAAAAAA",
    "AAAABBBBBBBBAAAA",
    "AAABBBBBBBBBBAAA",
    "AACCBBBCCBBBCCAA",
    "ACCCCBCCCCBCCCCA",
    "AAACCCCCCCCCCAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
    "AAAAAAAAAAAAAAAA",
]

# cloud_block() is packed as four 8x8 patterns in the horizontal sheet.
DEST_X = 46 * 8
INDEX = {"A": 8, "B": 9, "C": 11}

image = Image.open(PATH)
if image.mode != "P" or image.size[1] != 8:
    raise SystemExit(f"unexpected generated background format: {image.mode} {image.size}")

for source_y, row in enumerate(ROWS):
    for source_x, value in enumerate(row):
        dest_x = DEST_X + source_x + (source_y // 8) * 16
        image.putpixel((dest_x, source_y % 8), INDEX[value])

image.save(PATH)
