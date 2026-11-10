// Design 1 - Kit Grid: the 8 x 4 grid as it is in the plug-in, each cell a
// small colour OLED showing its sample, an LED level ladder under every cell,
// the note box at the head of each column. Output on the right.
window.WMT = ['SAMPLE', 'DRUM'];
header(`48000 HZ  /  32 VOICES  /  ${nLoaded()} OF 32 CELLS`, { wm: ['#f4f1e8', '#ffae2e'], divX: 330, segs: 32, lit: 3 });

section(16, 72, 980, 552, 'KIT', 'A COLUMN IS A DRUM  ·  A ROW IS A VELOCITY BAND  ·  DROP A .WAV ON ANY CELL');
const X0 = 100, PX = 111, CW = 103, Y0 = 162, PY = 106, CH = 74;
const famCol = d => OLED[d.fam];

// column heads
DRUMS.forEach((d, c) => {
  const cx = X0 + c * PX + CW / 2;
  text(cx, 120, d.name, { size: 9.5, anchor: 'middle' });
  noteBox(cx, 128, 84, d, { col: famCol(d) });
});
// row heads
text(30, 152, 'VELOCITY', { size: 8, weight: 400, fill: T.muted });
BANDS.forEach((b, r) => {
  const cy = Y0 + r * PY + CH / 2;
  text(58, cy - 2, `${b[0]}-${b[1]}`, { size: 10.5, font: MONO, weight: 400, anchor: 'middle' });
  text(58, cy + 13, ['SOFT', '', '', 'HARD'][r], { size: 7.5, weight: 400, anchor: 'middle', fill: T.dim });
});

// what is sounding right now
const LIT = { '0,3': 0.18, '3,2': 0.55, '1,2': 0.3 };
const FAIL = '6,0';
for (let r = 0; r < 4; r++) for (let c = 0; c < 8; c++) {
  const d = DRUMS[c], x = X0 + c * PX, y = Y0 + r * PY, col = famCol(d), key = `${c},${r}`;
  const name = FILES[r][c], lit = LIT[key];
  oled(x, y, CW, CH);
  if (lit !== undefined) {
    rect(x - 3, y - 3, CW + 6, CH + 6, 'none', col, 4, 1.6, 'filter="url(#bloom)"');
  }
  if (key === FAIL) {
    otext(x + 6, y + 12, name, T.red, 'start', 7.5);
    otext(x + CW / 2, y + CH / 2 + 6, 'FILE NOT FOUND', T.red, 'middle', 7.5);
  } else if (name) {
    otext(x + 6, y + 12, name, lit !== undefined ? col : T.oledText, 'start', 7.5);
    waveform(x + 5, y + 18, CW - 10, CH - 24, d.kind, r, col, { dim: lit === undefined });
    if (lit !== undefined) line(x + 5 + (CW - 10) * lit, y + 18, x + 5 + (CW - 10) * lit, y + CH - 6, '#fff', 1, 'opacity="0.8"');
  } else {
    const fb = fallback(c, r);
    rect(x + 6, y + 6, CW - 12, CH - 12, 'none', '#2c2f35', 2, 1, 'stroke-dasharray="3 3"');
    otext(x + CW / 2, y + CH / 2 - 2, 'EMPTY', T.oledText, 'middle', 7.5);
    if (fb >= 0) otext(x + CW / 2, y + CH / 2 + 11, `PLAYS ROW ${fb + 1} ${fb > r ? '↓' : '↑'}`, hexA(col, 0.8), 'middle', 7);
  }
  glass(x, y, CW, CH);
  levelSegs(x + 1, y + CH + 9, CW - 2, LEVELS[r][c], col, { n: 26, h: 5 });
}
text(X0, 606, 'CLICK A CELL TO HEAR IT  ·  DRAG TO MOVE  ·  OPTION-DRAG TO COPY  ·  RIGHT-CLICK TO CLEAR', { size: 8, weight: 400, fill: T.dim });

chevron(1003, 348);

// ---- ISOLATOR ---------------------------------------------------------
section(1010, 72, 174, 262, 'ISOLATOR');
isoOled(1024, 106, 146, 62, OLED.blue);
isoKnobs([[1058, 204], [1136, 204], [1058, 278], [1136, 278]], 15, { vs: 8, ls: 8, vgap: 9, lgap: 10 });

// ---- OUTPUT -------------------------------------------------------------
section(1010, 344, 174, 280, 'OUTPUT');
knob(1052, 410, 20, 0.36, '-6.0 DB', 'TRIM', { bipolar: true });
outMeter(1092, 380, 78, 150, { bw: 15 });
lamp(1034, 486, 0); text(1046, 489.5, 'OVER', { size: 8.5 });
lamp(1034, 512, 0); text(1046, 515.5, 'BYPASS', { size: 8.5 });
rule(1026, 546, 1168, 546);
sub(1030, 566, 'HAT CHOKE', { size: 9.5 });
otext(1030, 584, 'CLOSED HH CUTS', T.muted, 'start', 8.5);
otext(1030, 598, 'OPEN HH IN 5 MS', T.muted, 'start', 8.5);
otext(1030, 614, 'KIT SAVED IN PROJECT', T.dim, 'start', 7);

render();
