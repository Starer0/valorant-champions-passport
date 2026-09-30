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
    # Dark outlines and navy fill must survive packing; whitening them makes
    # TYLOO and Liquid lose their original detail on the device.
    step = (52 * 52 + 22 * 22) * 3
    for team_index, code in ((12, "TL"), (13, "TYL")):
        start = team_index * step
        dark = 0
        for i in range(52 * 52):
            pixel = int.from_bytes(blob[start + i * 2:start + i * 2 + 2], "little")
            r, g, b = (pixel >> 11) * 255 // 31, ((pixel >> 5) & 63) * 255 // 63, (pixel & 31) * 255 // 31
            if blob[start + 52 * 52 * 2 + i] > 220 and max(r, g, b) < 65:
                dark += 1
        assert dark > 100, f"{code} dark colour/detail was lost: {dark} pixels"
    print("champions assets: PASS")


if __name__ == "__main__":
    main()
