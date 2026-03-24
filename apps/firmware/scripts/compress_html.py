"""PlatformIO pre-build script: compress web/*.html to gzip PROGMEM C headers."""

import gzip
import os
import re
from pathlib import Path


def minify_html(html: str) -> str:
    """Remove HTML comments, compress whitespace."""
    # Remove HTML comments
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)
    # Compress runs of whitespace to single space
    html = re.sub(r'\s+', ' ', html)
    # Remove spaces around tags
    html = re.sub(r'>\s+<', '><', html)
    # Strip leading/trailing whitespace
    return html.strip()


def to_c_array(data: bytes, name: str) -> str:
    """Generate C header with PROGMEM byte array."""
    upper = name.upper()
    hex_bytes = ', '.join(f'0x{b:02x}' for b in data)
    lines = []
    lines.append(f'#ifndef {upper}_GZ_H')
    lines.append(f'#define {upper}_GZ_H')
    lines.append('')
    lines.append('#include <Arduino.h>')
    lines.append('')
    lines.append(f'const uint8_t {upper}_GZ[] PROGMEM = {{')

    # Wrap hex bytes at ~100 chars per line
    row = []
    row_len = 0
    for b in data:
        token = f'0x{b:02x}'
        if row_len + len(token) + 2 > 100:
            lines.append('    ' + ', '.join(row) + ',')
            row = []
            row_len = 0
        row.append(token)
        row_len += len(token) + 2
    if row:
        lines.append('    ' + ', '.join(row))

    lines.append('};')
    lines.append(f'const size_t {upper}_GZ_LEN = sizeof({upper}_GZ);')
    lines.append('')
    lines.append(f'#endif // {upper}_GZ_H')
    lines.append('')
    return '\n'.join(lines)


def name_from_html(filename: str) -> str:
    """Map HTML filename to C identifier: wifi_setup.html -> HTML_WIFI_SETUP."""
    stem = Path(filename).stem  # wifi_setup
    return f'HTML_{stem.upper()}'


def header_filename(filename: str) -> str:
    """Map HTML filename to header: wifi_setup.html -> html_wifi_setup_gz.h."""
    stem = Path(filename).stem
    return f'html_{stem}_gz.h'


def process_html_files(web_dir: Path, include_dir: Path) -> None:
    """Compress all HTML files in web_dir, output gzip headers to include_dir."""
    if not web_dir.exists():
        print(f'[compress_html] web directory not found: {web_dir}')
        return

    html_files = sorted(web_dir.glob('*.html'))
    if not html_files:
        print('[compress_html] no HTML files found in web/')
        return

    include_dir.mkdir(parents=True, exist_ok=True)

    for html_path in html_files:
        out_name = header_filename(html_path.name)
        out_path = include_dir / out_name
        c_name = name_from_html(html_path.name)

        # Skip if header is newer than source
        if out_path.exists():
            src_mtime = html_path.stat().st_mtime
            dst_mtime = out_path.stat().st_mtime
            if dst_mtime >= src_mtime:
                print(f'[compress_html] {html_path.name} unchanged, skipping')
                continue

        raw = html_path.read_text(encoding='utf-8')
        minified = minify_html(raw)
        minified_bytes = minified.encode('utf-8')

        compressed = gzip.compress(minified_bytes, compresslevel=9)

        header_content = to_c_array(compressed, c_name)
        out_path.write_text(header_content, encoding='utf-8')

        ratio = len(minified_bytes) / len(compressed) if compressed else 0
        print(f'[compress_html] {html_path.name}: '
              f'{len(raw)} -> {len(minified_bytes)} (minified) -> {len(compressed)} bytes (gzip) '
              f'[{ratio:.1f}x compression]')


def main():
    """Entry point for both PlatformIO and standalone execution."""
    # Determine project root: scripts/ is one level below firmware root
    script_dir = Path(__file__).resolve().parent
    firmware_dir = script_dir.parent

    web_dir = firmware_dir / 'web'
    include_dir = firmware_dir / 'src' / 'include'

    process_html_files(web_dir, include_dir)


# PlatformIO pre-build hook entry point
try:
    Import("env")  # noqa: F821 - PlatformIO SCons built-in
    main()
except NameError:
    # Running standalone (not via PlatformIO)
    pass

if __name__ == '__main__':
    main()
