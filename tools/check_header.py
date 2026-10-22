"""Check a Beaver Surf ROM header. .gba uses the GBA header; .gb and .gbc use the Game Boy one."""

import sys
from pathlib import Path

GBA_LOGO = bytes.fromhex("24ffae51")
GB_LOGO = bytes.fromhex(
    "ceed6666cc0d000b03730083000c000d"
    "0008111f8889000edccc6ee6ddddd999"
    "bbbb67636e0eecccdddc999fbbb9333e"
)


def check_gba(path: Path, data: bytes) -> None:
    if len(data) < 0xC0:
        raise SystemExit(f"{path} is too small to be a GBA ROM")
    if data[4:8] != GBA_LOGO:
        raise SystemExit(f"{path} is missing the GBA logo")
    if data[0xB2] != 0x96:
        raise SystemExit(f"{path} header byte 0xB2 is {data[0xB2]:#x}, expected 0x96")
    checksum = 0
    for byte in data[0xA0:0xBD]:
        checksum = (checksum - byte) & 0xFF
    checksum = (checksum - 0x19) & 0xFF
    if data[0xBD] != checksum:
        raise SystemExit(f"{path} complement is {data[0xBD]:#x}, expected {checksum:#x}")
    title = data[0xA0:0xAC].split(b"\x00", 1)[0].decode("ascii", "replace")
    print(f"{path}: {len(data)} bytes, title {title!r}, header ok")


def check_gb(path: Path, data: bytes) -> None:
    if len(data) < 0x150:
        raise SystemExit(f"{path} is too small to be a Game Boy ROM")
    if data[0x104:0x134] != GB_LOGO:
        raise SystemExit(f"{path} is missing the Nintendo logo")
    if data[0x143] != 0xC0:
        raise SystemExit(f"{path} CGB flag is {data[0x143]:#x}, expected 0xC0 (Color only)")
    if data[0x147] != 0x1B:
        raise SystemExit(
            f"{path} cart type is {data[0x147]:#x}, expected 0x1B (MBC5 + RAM + battery)"
        )
    checksum = 0
    for byte in data[0x134:0x14D]:
        checksum = (checksum - byte - 1) & 0xFF
    if data[0x14D] != checksum:
        raise SystemExit(f"{path} header checksum is {data[0x14D]:#x}, expected {checksum:#x}")
    title = data[0x134:0x143].split(b"\x00", 1)[0].decode("ascii", "replace")
    print(f"{path}: {len(data)} bytes, title {title!r}, header ok")


def main() -> None:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "web/beaver.gba")
    data = path.read_bytes()
    if path.suffix.lower() in {".gb", ".gbc"}:
        check_gb(path, data)
    else:
        check_gba(path, data)


if __name__ == "__main__":
    main()
