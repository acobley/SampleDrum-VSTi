#!/usr/bin/env python3
"""Render the plug-in's Polivoks panel from d2p.js:

  panel-static.png  the unchanging panel, 2x  -> resource/pv_panel.png
  panel-full.png    with the demo patch drawn, 2x (reference; the wear is fitted to it)
  panel-layout.json where every knob, selector, lamp and screen is (for wear.py)

usage: render-panel.py [outdir]"""
import sys, json, pathlib
from playwright.sync_api import sync_playwright
D = pathlib.Path(__file__).parent
OUT = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else D).resolve()
js = (D / 'rbfont.js').read_text() + '\n' + (D / 'pv.js').read_text() + '\n' + (D / 'sd.js').read_text() + '\n' + (D / 'd2p.js').read_text()
with sync_playwright() as p:
    b = p.chromium.launch()
    for name, static in (('panel-static', True), ('panel-full', False)):
        html = ('<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;background:#121212}</style>'
                f'</head><body><script>window.STATIC={str(static).lower()};\n' + js + '</script></body></html>')
        f = OUT / f'{name}.html'
        f.write_text(html)
        pg = b.new_page(viewport={'width': 1200, 'height': 640}, device_scale_factor=2)
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.goto(f.as_uri()); pg.wait_for_timeout(300)
        if errs:
            sys.exit(f'{name}: {errs}')
        w, h = pg.evaluate('[document.body.scrollWidth, document.body.scrollHeight]')
        print(name, 'page', w, h)
        pg.screenshot(path=str(OUT / f'{name}.png'), clip={'x': 0, 'y': 0, 'width': 1200, 'height': 640})
        if not static:
            (OUT / 'panel-layout.json').write_text(json.dumps(pg.evaluate('window.LAYOUT')))
        f.unlink()
    b.close()
