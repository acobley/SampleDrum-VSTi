#!/bin/sh
#-----------------------------------------------------------------------------
# Rebuild the Polivoks panel's two bitmaps from d2p.js:
#
#   resource/pv_panel.png   the unchanging panel (2x)
#   resource/pv_wear.png    the nearly-new wear, a transparent layer (2x)
#
# Needs python3 with playwright (and its Chromium), numpy, scipy and Pillow.
# Run it after moving anything in d2p.js - and move the same thing in
# source/SampleDrumEditor.cpp, which draws the controls at those coordinates.
#-----------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
python3 render-panel.py "$WORK"
python3 wear.py "$WORK/panel-full.png" "$WORK/panel-layout.json" "$WORK"
cp "$WORK/panel-static.png" "$ROOT/resource/pv_panel.png"
cp "$WORK/pv_wear.png" "$ROOT/resource/pv_wear.png"
echo "wrote resource/pv_panel.png and resource/pv_wear.png"
