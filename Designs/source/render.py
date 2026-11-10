#!/usr/bin/env python3
"""Render SampleDrum Polivoks designs: rbfont.js + pv.js + dN.js -> clean PNG
at 2x plus a layout dump (window.LAYOUT) for the weathering pass.
usage: render.py d1 d2 d3"""
import sys, json, pathlib
from playwright.sync_api import sync_playwright
D = pathlib.Path(__file__).parent
with sync_playwright() as p:
    b = p.chromium.launch()
    for name in sys.argv[1:]:
        html = ('<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;background:#121212}</style>'
                '</head><body><script>' + (D / 'rbfont.js').read_text() + '\n' + (D / 'pv.js').read_text() + '\n' + (D / 'sd.js').read_text() + '\n' +
                (D / f'{name}.js').read_text() + '</script></body></html>')
        (D / f'{name}.html').write_text(html)
        pg = b.new_page(viewport={'width': 1200, 'height': 640}, device_scale_factor=2)
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.goto((D / f'{name}.html').as_uri()); pg.wait_for_timeout(300)
        if errs: print('ERR', name, errs)
        w, h = pg.evaluate('[document.body.scrollWidth, document.body.scrollHeight]')
        print(name, 'page', w, h, 'wordmark', pg.evaluate('window.wmWidth'))
        pg.screenshot(path=str(D / f'{name}-clean.png'), clip={'x': 0, 'y': 0, 'width': 1200, 'height': 640})
        (D / f'{name}-layout.json').write_text(json.dumps(pg.evaluate('window.LAYOUT')))
    b.close()
