"""Check the packed LVGL asset layout without ESP-IDF or Pillow."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLOB = ROOT / "main" / "champions_assets.bin"
TEAM_COUNT = 16


def check_image(blob: bytes, offset: int, side: int) -> int:
    pixels = side * side
    block = blob[offset : offset + pixels * 3]
    assert len(block) == pixels * 3
    alpha = block[pixels * 2 :]
    assert max(alpha) >= 200
    assert min(alpha) == 0
    assert sum(value > 32 for value in alpha) > pixels // 25
    return offset + len(block)


def main() -> None:
    blob = BLOB.read_bytes()
    offset = 0
    for _ in range(TEAM_COUNT):
        offset = check_image(blob, offset, 52)
        offset = check_image(blob, offset, 22)
    offset = check_image(blob, offset, 70)
    offset = check_image(blob, offset, 28)
    assert offset == len(blob) == 170076
    print("champions assets: PASS")


if __name__ == "__main__":
    main()
