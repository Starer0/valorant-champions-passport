#!/usr/bin/env python3
"""Pack the 2026 Champions team marks for LVGL RGB565A8 images.

Run manually when the official event artwork changes. The firmware build uses
the checked-in binary and never downloads assets at build or device runtime.
Requires Pillow: python -m pip install Pillow
"""

from __future__ import annotations

import argparse
import io
import urllib.request
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "main" / "champions_assets.bin"
EVENT_ART = (
    "https://cmsassets.rgpub.io/sanity/images/dsfx7636/news_live/"
    "ebb7d451ea8198a7830cf04857302e584d553942-1920x1080.png"
)
# Team images are the marks served by the official Champions Shanghai roster.
# Order must match champions_team_index() in main/champions_model.c.
TEAMS = (
    ("100T", "1644966311613_100TVAL.png"),
    ("EDG", "1735920597800_edg1.png"),
    ("FUT", "1644342678681_FUT_VRL_DARK.png"),
    ("G2", "1675329807714_G2-Esports-Logo1.png"),
    ("GE", "1677137854820_GERedandBlue-WhiteBG.png"),
    ("JDG", "1707126862498_JDG.png"),
    ("KC", "1651626649157_download1.png"),
    ("LOUD", "1644060140569_LOUDGreen.png"),
    ("NRG", "1731490666442_NRGWHT.png"),
    ("NS", "1674219422693_NS_.png"),
    ("PRX", "1630018120582_paper-rex-2021.png"),
    ("T1", "1677137430839_T1_infoboximage1.png"),
    ("TL", "1637252586938_teamliquid.png"),
    ("TYL", "1776403069581_TYLLOGO.png"),
    ("VIT", "1722030793680_VITALITY_YELLOW.png"),
    ("XLG", "1735920774181_XLGA.PNG"),
)


def open_image(url: str, cached: Path | None) -> Image.Image:
    if cached and cached.is_file():
        return Image.open(cached).convert("RGBA")
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return Image.open(io.BytesIO(response.read())).convert("RGBA")


def fit_logo(image: Image.Image, size: int) -> Image.Image:
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("Logo has no opaque pixels")
    image = image.crop(bounds)
    image.thumbnail((size - 4, size - 4), Image.Resampling.LANCZOS)
    output = Image.new("RGBA", (size, size))
    output.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))
    # Preserve the official colours and outlines. Contrast is provided by the
    # application's tile background, never by changing the team's artwork.
    return output


def event_mark(image: Image.Image, size: int) -> Image.Image:
    if image.size != (1920, 1080):
        raise ValueError(f"Unexpected event artwork dimensions: {image.size}")
    crop = image.crop((845, 235, 1075, 470)).convert("RGB")
    alpha = Image.new("L", crop.size)
    source = crop.load()
    mask = alpha.load()
    for y in range(crop.height):
        for x in range(crop.width):
            r, g, b = source[x, y]
            low = min(r, g, b)
            if max(r, g, b) - low < 22:
                mask[x, y] = max(0, min(255, (low - 180) * 255 // 55))
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("Champions mark not found in event artwork")
    alpha = alpha.crop(bounds)
    alpha.thumbnail((size - 2, size - 2), Image.Resampling.LANCZOS)
    output = Image.new("RGBA", (size, size), (215, 181, 109, 0))
    output.paste(
        Image.new("RGBA", alpha.size, (215, 181, 109, 255)),
        ((size - alpha.width) // 2, (size - alpha.height) // 2),
        alpha,
    )
    return output


def rgb565a8(image: Image.Image) -> bytes:
    colour = bytearray()
    alpha = bytearray()
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            colour.extend((pixel & 0xFF, pixel >> 8))
            alpha.append(a)
    return bytes(colour + alpha)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache-dir", type=Path, help="optional local source image cache")
    args = parser.parse_args()
    data = bytearray()
    for code, filename in TEAMS:
        cached = args.cache_dir / "logos" / f"{code}.png" if args.cache_dir else None
        image = open_image(f"https://static.lolesports.com/teams/{filename}", cached)
        for size in (52, 22):
            data.extend(rgb565a8(fit_logo(image, size)))
    cached = args.cache_dir / "champions-banner.png" if args.cache_dir else None
    image = open_image(EVENT_ART, cached)
    for size in (70, 28):
        data.extend(rgb565a8(event_mark(image, size)))
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(data)
    print(f"Wrote {len(data)} bytes to {OUTPUT}")


if __name__ == "__main__":
    main()
