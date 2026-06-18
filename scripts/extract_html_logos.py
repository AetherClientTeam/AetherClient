#!/usr/bin/env python3
"""
Extract common logo/image sources from an HTML file into PNG files.

Supported fast paths:
- <img src="data:image/...;base64,...">
- <img src="file-or-url">
- inline <svg>...</svg> blocks, converted to PNG when cairosvg is installed

For CSS/canvas-only logos, pass --render to save a browser screenshot when
Playwright is installed in the current Python environment.
"""

from __future__ import annotations

import argparse
import base64
import mimetypes
import re
import shutil
import sys
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from pathlib import Path


class ImageSourceParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.sources: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() != "img":
            return
        attr_map = {name.lower(): value for name, value in attrs if value}
        src = attr_map.get("src") or attr_map.get("data-src")
        if src:
            self.sources.append(src)


def safe_name(prefix: str, index: int, suffix: str) -> str:
    suffix = suffix if suffix.startswith(".") else f".{suffix}"
    return f"{prefix}_{index:02d}{suffix}"


def convert_svg_to_png(svg_bytes: bytes, output: Path) -> bool:
    try:
        import cairosvg  # type: ignore
    except Exception:
        return False
    cairosvg.svg2png(bytestring=svg_bytes, write_to=str(output))
    return True


def write_data_uri(src: str, output_dir: Path, index: int) -> Path | None:
    match = re.match(r"data:(image/[a-zA-Z0-9.+-]+);base64,(.*)", src, re.DOTALL)
    if not match:
        return None
    mime, payload = match.groups()
    data = base64.b64decode(payload)
    ext = mimetypes.guess_extension(mime) or ".bin"
    if mime == "image/svg+xml":
        png = output_dir / safe_name("logo_data_svg", index, ".png")
        if convert_svg_to_png(data, png):
            return png
        svg = output_dir / safe_name("logo_data_svg", index, ".svg")
        svg.write_bytes(data)
        return svg
    output = output_dir / safe_name("logo_data", index, ext)
    output.write_bytes(data)
    return output


def read_external_source(src: str, html_path: Path) -> tuple[bytes, str]:
    parsed = urllib.parse.urlparse(src)
    if parsed.scheme in {"http", "https"}:
        request = urllib.request.Request(src, headers={"User-Agent": "AetherLogoExtractor/1.0"})
        with urllib.request.urlopen(request, timeout=20) as response:
            content_type = response.headers.get("content-type", "").split(";")[0].strip()
            return response.read(), content_type
    path = Path(urllib.request.url2pathname(parsed.path)) if parsed.scheme == "file" else Path(src)
    if not path.is_absolute():
        path = html_path.parent / path
    return path.read_bytes(), mimetypes.guess_type(str(path))[0] or ""


def write_external_source(src: str, html_path: Path, output_dir: Path, index: int) -> Path | None:
    try:
        data, mime = read_external_source(src, html_path)
    except Exception as exc:
        print(f"skip external image {src!r}: {exc}", file=sys.stderr)
        return None
    ext = mimetypes.guess_extension(mime) or Path(urllib.parse.urlparse(src).path).suffix or ".bin"
    if mime == "image/svg+xml" or ext.lower() == ".svg":
        png = output_dir / safe_name("logo_external_svg", index, ".png")
        if convert_svg_to_png(data, png):
            return png
        svg = output_dir / safe_name("logo_external_svg", index, ".svg")
        svg.write_bytes(data)
        return svg
    output = output_dir / safe_name("logo_external", index, ext)
    output.write_bytes(data)
    return output


def extract_inline_svgs(html: str, output_dir: Path) -> list[Path]:
    outputs: list[Path] = []
    for index, match in enumerate(re.finditer(r"<svg\b[^>]*>.*?</svg>", html, re.IGNORECASE | re.DOTALL), 1):
        data = match.group(0).encode("utf-8")
        png = output_dir / safe_name("logo_inline_svg", index, ".png")
        if convert_svg_to_png(data, png):
            outputs.append(png)
        else:
            svg = output_dir / safe_name("logo_inline_svg", index, ".svg")
            svg.write_bytes(data)
            outputs.append(svg)
    return outputs


def render_page(html_path: Path, output_dir: Path) -> Path | None:
    try:
        from playwright.sync_api import sync_playwright  # type: ignore
    except Exception:
        print("render skipped: Playwright is not installed for this Python.", file=sys.stderr)
        return None
    output = output_dir / "logo_render_page.png"
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch()
        page = browser.new_page(viewport={"width": 1400, "height": 1000}, device_scale_factor=2)
        page.goto(html_path.resolve().as_uri())
        page.screenshot(path=str(output), full_page=True, omit_background=True)
        browser.close()
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract image/logo sources from HTML.")
    parser.add_argument("html", type=Path)
    parser.add_argument("-o", "--output", type=Path, default=Path("extracted_logos"))
    parser.add_argument("--render", action="store_true", help="Also render the page to PNG with Playwright when available.")
    args = parser.parse_args()

    html_path = args.html
    output_dir = args.output
    output_dir.mkdir(parents=True, exist_ok=True)
    html = html_path.read_text(encoding="utf-8", errors="replace")

    outputs = extract_inline_svgs(html, output_dir)
    img_parser = ImageSourceParser()
    img_parser.feed(html)
    for index, src in enumerate(img_parser.sources, 1):
        output = write_data_uri(src, output_dir, index)
        if output is None:
            output = write_external_source(src, html_path, output_dir, index)
        if output:
            outputs.append(output)

    if args.render:
        rendered = render_page(html_path, output_dir)
        if rendered:
            outputs.append(rendered)

    for output in outputs:
        print(output)
    return 0 if outputs else 1


if __name__ == "__main__":
    raise SystemExit(main())
