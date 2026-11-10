// Design 3 - Pads + Scope: a big white OLED shows the cell last touched; under
// it the 8 x 4 grid as square rubber-capped pads with a lamp strip that lights
// on every hit, a mini slide fader for each cell's level. Monochrome white
// screens, red accents.
window.WMT = ['SAMPLE', 'DRUM'];
OLED = { amber: '#eef3ff', blue: '#eef3ff', mint: '#eef3ff', violet: '#eef3ff', white: '#eef3ff' };
header(`48000 HZ  /  32 VOICES  /  ${nLoaded()} OF 32 CELLS`, { wm: ['#f4f1e8', '#e2342b'], divX: 330, segs: 32, lit: 3 });
const WH = OLED.white;

// ---- SCOPE -------------------------------------------------------------
section(16, 72, 470, 178, 'SAMPLE', 'LAST CLICKED OR HIT');
oled(30, 108, 442, 128);
otext(38, 121, 'SNARE  ·  64-95  ·  SN HARD 01.WAV', WH, 'start', 8.5);
otext(464, 121, '24 BIT  0.48 S', T.oledText, 'end', 8.5);
for (let i = 1; i < 8; i++) line(38 + 426 * i / 8, 128, 38 + 426 * i / 8, 214, '#1c1f24', 0.8);
waveform(38, 130, 426, 82, 'snare', 2, WH);
line(38 + 426 * 0.34, 128, 38 + 426 * 0.34, 214, T.red, 1.4, 'filter="url(#bloom)"');
otext(38, 230, 'LEVEL  0.0 DB', T.oledText, 'start', 8.5);
otext(464, 230, 'EMBEDDED IN PROJECT', T.oledText, 'end', 8.5);
glass(30, 108, 442, 128);

chevron(492, 161);

// ---- ISOLATOR ----------------------------------------------------------
section(498, 72, 350, 178, 'ISOLATOR', 'BEFORE THE TRIM');
isoOled(512, 106, 322, 56, WH);
isoKnobs([[546, 196], [618, 196], [712, 196], [786, 196]], 13, { vs: 8, ls: 8, vgap: 10, lgap: 11 });
line(665, 178, 665, 236, '#3a3a3a', 1);

chevron(854, 161);

// ---- OUTPUT ------------------------------------------------------------
section(860, 72, 324, 178, 'OUTPUT');
knob(910, 156, 24, 0.36, '-6.0 DB', 'OUTPUT TRIM', { bipolar: true });
oled(962, 106, 208, 50);
{
  const segs = 30, x0 = 982, w = 180;
  ['L', 'R'].forEach((ch, k) => {
    const y = 114 + k * 20, lvl = k ? 0.70 : 0.74, pk = k ? 0.84 : 0.86;
    otext(970, y + 8, ch, T.oledText, 'start', 8);
    for (let i = 0; i < segs; i++) {
      const f = i / segs, c = f > 0.91 ? T.red : WH, on = f < lvl;
      rect(x0 + i * w / segs, y, w / segs - 2, 10, on ? c : hexA(c, 0.08), null, 0.5, 0, on ? 'filter="url(#bloom)"' : '');
    }
    rect(x0 + Math.round(pk * segs) * w / segs, y, w / segs - 2, 10, WH, null, 0.5, 0, 'filter="url(#bloom)"');
  });
  glass(962, 106, 208, 50);
}
[['-48', 0.2], ['-24', 0.45], ['-12', 0.64], ['-6', 0.78], ['0', 0.91]].forEach(([s, f]) =>
  text(982 + 180 * f, 170, s, { size: 7.5, weight: 400, anchor: 'middle', fill: s === '0' ? T.red : T.muted, font: MONO }));
lamp(978, 196, 0); text(990, 199.5, 'OVER 0 DBFS', { size: 8.5 });
lamp(1088, 196, 0); text(1100, 199.5, 'BYPASS', { size: 8.5 });
text(970, 228, 'HAT CHOKE  ·  CLOSED HH CUTS OPEN HH', { size: 7.5, weight: 400, fill: T.muted });

// ---- PADS --------------------------------------------------------------
section(16, 262, 1168, 362, 'KIT');
const X0 = 104, PX = 134, PW = 118, Y0 = 332, PY = 71, PH = 46;
DRUMS.forEach((d, c) => {
  const cx = X0 + c * PX + PW / 2;
  text(cx, 294, d.name, { size: 9, anchor: 'middle' });
  noteBox(cx, 300, 84, d, { col: WH, h: 16, size: 8.5 });
});
BANDS.forEach((b, r) => {
  const cy = Y0 + r * PY + PH / 2;
  text(64, cy + 2, `${b[0]}-${b[1]}`, { size: 10, font: MONO, weight: 400, anchor: 'middle' });
  text(64, cy + 15, ['SOFT', '', '', 'HARD'][r], { size: 7, weight: 400, anchor: 'middle', fill: T.dim });
});
defs.push(`<linearGradient id="padgrad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#262626"/><stop offset="0.12" stop-color="#1b1b1b"/><stop offset="1" stop-color="#0d0d0d"/></linearGradient>`);
const LIT = { '0,3': 1, '1,2': 1, '3,2': 0.6 }, SELC = '1,2';
for (let r = 0; r < 4; r++) for (let c = 0; c < 8; c++) {
  const d = DRUMS[c], x = X0 + c * PX, y = Y0 + r * PY, key = `${c},${r}`, name = FILES[r][c], lit = LIT[key];
  rect(x - 2, y - 2, PW + 4, PH + 4, '#050505', key === SELC ? T.red : '#2e2e2e', 4, key === SELC ? 1.4 : 0.8);
  rect(x, y, PW, PH, 'url(#padgrad)', '#000', 3, 0.8);
  line(x + 3, y + 0.8, x + PW - 3, y + 0.8, '#4a4a4a', 0.8);
  // lamp strip
  rect(x + 8, y + 5, PW - 16, 3, lit ? T.lampOn : '#240d0b', null, 1.5, 0, lit ? `filter="url(#bloom)" opacity="${lit}"` : '');
  if (lit) rect(x + 4, y + 2, PW - 8, 9, hexA(T.lampOn, 0.12 * lit), null, 3);
  if (name) {
    path(wavePath(x + 10, y + 13, PW - 20, 18, d.kind, r), 'none', 0, hexA('#9a978f', 0.32));
    text(x + PW / 2, y + PH - 6, name, { size: 7.5, weight: 400, anchor: 'middle', fill: T.ink });
  } else {
    const fb = fallback(c, r);
    text(x + PW / 2, y + 26, 'EMPTY', { size: 7.5, weight: 400, anchor: 'middle', fill: T.dim });
    if (fb >= 0) text(x + PW / 2, y + PH - 6, `PLAYS ROW ${fb + 1} ${fb > r ? '↓' : '↑'}`, { size: 7, weight: 400, anchor: 'middle', fill: T.dim });
  }
  hfader(x + 6, y + PH + 11, PW - 12, LEVELS[r][c], { pointer: key === SELC ? T.red : T.ink });
}

render();
