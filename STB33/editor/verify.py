#!/usr/bin/env python3
"""Assemble the exact RAVET controls and probe the panel in hidden macOS WebKit.
Writes only temporary artifacts; preview image defaults to /tmp/dw303-editor.png.
"""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--assemble-only', action='store_true', help='Write /tmp/dw303-editor.html for manual browser preview')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
web = root.parent / 'ravet/crates/ravet-surface/web'
html = (root / 'editor/index.html').read_text()
html = html.replace('<!-- RAVET_CONTROLS -->', '<style>' + (web / 'theme.css').read_text()
                    + '</style><script>' + (web / 'controls.js').read_text() + '</script>')
assert len(html.encode()) < 1048576
print(f'Assembled inline page: {len(html.encode())} bytes', flush=True)
if args.assemble_only:
    pathlib.Path('/tmp/dw303-editor.html').write_text(html)
    print('Open /tmp/dw303-editor.html in a browser; host edits require the CLAP editor.')
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='dw303-webkit-') as directory:
    directory = pathlib.Path(directory)
    source = directory / 'index.html'
    source.write_text(html)
    binary = directory / 'probe'
    subprocess.run(['swiftc', '-module-cache-path', str(directory / 'cache'),
                    str(root / 'editor/verify.swift'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(source), '/tmp/dw303-editor.png'], check=True)
print('WebKit checks passed; preview: /tmp/dw303-editor.png')
