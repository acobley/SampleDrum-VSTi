// SampleDrum parts on top of the Polivoks kit (pv.js): the kit model, sample
// waveforms, grid cells, the per-column note box and the per-cell level.
// Everything here mirrors SampleDrumKit.h: 8 columns (drums) x 4 rows
// (velocity bands, soft at the top), GM default notes, 60 = C3.
let seed = 7;
const rnd = () => { seed = (seed * 16807) % 2147483647; return (seed - 1) / 2147483646; };
const DRUMS = [
  { name: 'KICK', short: 'KICK', note: 36, nn: 'C1', kind: 'kick', fam: 'amber' },
  { name: 'SNARE', short: 'SNARE', note: 38, nn: 'D1', kind: 'snare', fam: 'blue' },
  { name: 'CLAP', short: 'CLAP', note: 39, nn: 'D#1', kind: 'clap', fam: 'blue' },
  { name: 'CLOSED HH', short: 'CL HH', note: 42, nn: 'F#1', kind: 'chh', fam: 'mint' },
  { name: 'OPEN HH', short: 'OP HH', note: 46, nn: 'A#1', kind: 'ohh', fam: 'mint' },
  { name: 'LOW TOM', short: 'LO TOM', note: 45, nn: 'A1', kind: 'ltom', fam: 'violet' },
  { name: 'HIGH TOM', short: 'HI TOM', note: 50, nn: 'D2', kind: 'htom', fam: 'violet' },
  { name: 'CRASH', short: 'CRASH', note: 49, nn: 'C#2', kind: 'crash', fam: 'white' },
];
const BANDS = [[1, 31], [32, 63], [64, 95], [96, 127]];
// The demo kit: file name per cell (row-major as saved: cell = col + row*8),
// '' = empty (falls back to the nearest loaded row, softer on a tie).
const FILES = [
  ['BD SOFT 01', 'SN GHOST 02', 'CLAP LIGHT', 'HH TIP 01', 'OH LOOSE 01', 'TOM LO SOFT', 'TOM HI SOFT', ''],
  ['BD MED 01', 'SN MED 04', 'CLAP MED', 'HH TIP 02', 'OH LOOSE 02', 'TOM LO MED', 'TOM HI MED', ''],
  ['BD MED 02', 'SN HARD 01', 'CLAP STACK', 'HH SHANK 01', 'OH WIDE 01', 'TOM LO HARD', 'TOM HI HARD', 'CRASH 16 IN'],
  ['BD HARD 03', 'SN RIM 02', 'CLAP STACK 2', 'HH SHANK 02', 'OH WIDE 02', 'TOM LO HARD 2', 'TOM HI HARD 2', ''],
];
const LEVELS = [ // dB per cell, -40..+12; 0 = unity
  [-6, -4, -8, -3, -5, -6, -6, 0], [-2, -2, -4, -1, -3, -3, -3, 0],
  [0, 0, -2, 0, -1, -1, -1, -2], [1.5, 1, 0, 1, 0, 0, 0, 0],
];
const levelFrac = db => (db + 40) / 52;
const loaded = (c, r) => !!FILES[r][c];
// which row an empty cell borrows from
function fallback(c, r) {
  for (let d = 1; d < 4; d++) {
    if (r - d >= 0 && loaded(c, r - d)) return r - d;   // softer wins a tie
    if (r + d < 4 && loaded(c, r + d)) return r + d;
  }
  return -1;
}
const nLoaded = () => FILES.flat().filter(Boolean).length;

// A one-shot's samples, signed, 0..1 along the file.
function sampleData(kind, n, row) {
  seed = 1000 + kind.length * 37 + row * 11;
  const hard = 0.55 + row * 0.15, out = [];
  let ph = 0;
  for (let i = 0; i < n; i++) {
    const t = i / n, nz = rnd() * 2 - 1;
    let v = 0;
    switch (kind) {
      case 'kick': { const f = 3 + 22 * Math.exp(-t * 18); ph += f / n * 6; v = Math.sin(ph * 2 * Math.PI) * Math.exp(-t * 4.5) + nz * 0.25 * Math.exp(-t * 60); break; }
      case 'snare': { ph += 28 / n * 6; v = (0.45 * Math.sin(ph * 2 * Math.PI) * Math.exp(-t * 14) + 0.75 * nz * Math.exp(-t * 6.5)); break; }
      case 'clap': { const b = [0, 0.04, 0.085, 0.13]; let e = 0; b.forEach((s, k) => { if (t >= s) e = Math.max(e, (k === 3 ? 1 : 0.8) * Math.exp(-(t - s) * (k === 3 ? 7 : 70))); }); v = nz * e; break; }
      case 'chh': v = nz * Math.exp(-t * 22); break;
      case 'ohh': v = nz * Math.exp(-t * 3.2) * (0.8 + 0.2 * Math.sin(t * 90)); break;
      case 'ltom': { const f = 6 + 5 * Math.exp(-t * 10); ph += f / n * 6; v = Math.sin(ph * 2 * Math.PI) * Math.exp(-t * 3.6) + nz * 0.15 * Math.exp(-t * 30); break; }
      case 'htom': { const f = 10 + 6 * Math.exp(-t * 10); ph += f / n * 6; v = Math.sin(ph * 2 * Math.PI) * Math.exp(-t * 4.2) + nz * 0.15 * Math.exp(-t * 30); break; }
      case 'crash': v = nz * (Math.min(1, t * 60)) * Math.exp(-t * 1.6); break;
    }
    out.push(Math.max(-1, Math.min(1, v * hard)));
  }
  return out;
}
// min/max per pixel column, as a sample editor draws it
function wavePath(x, y, w, h, kind, row) {
  const N = Math.round(w * 6), s = sampleData(kind, N, row), cols = Math.round(w);
  const top = [], bot = [], mid = y + h / 2;
  for (let c = 0; c < cols; c++) {
    let lo = 0, hi = 0;
    for (let i = Math.floor(c * N / cols); i < Math.floor((c + 1) * N / cols); i++) { lo = Math.min(lo, s[i]); hi = Math.max(hi, s[i]); }
    top.push([x + c * w / cols, mid - hi * h / 2]); bot.push([x + c * w / cols, mid - lo * h / 2]);
  }
  return 'M' + top.map(p => `${f1(p[0])},${f1(p[1])}`).join(' L') + ' L' + bot.reverse().map(p => `${f1(p[0])},${f1(p[1])}`).join(' L') + ' Z';
}
function waveform(x, y, w, h, kind, row, col, o = {}) {
  const d = wavePath(x, y, w, h, kind, row);
  path(d, col, 0.5, hexA(col, o.dim ? 0.28 : 0.55), o.dim ? '' : 'filter="url(#bloom)"');
  line(x, y + h / 2, x + w, y + h / 2, hexA(col, 0.35), 0.6);
}

// The note box at the top of a column: "< 36 C1 >". Amber when two columns share a note.
function noteBox(cx, y, w, d, o = {}) {
  const x = cx - w / 2, h = o.h || 18;
  oled(x, y, w, h);
  const col = o.layered ? OLED.amber : (o.col || OLED.white);
  otext(x + 7, y + h / 2 + 3.2, '‹', T.oledText, 'start', 9);
  otext(x + w - 7, y + h / 2 + 3.2, '›', T.oledText, 'end', 9);
  otext(cx, y + h / 2 + 3.2, `${d.note}  ${d.nn}`, col, 'middle', o.size || 9);
  glass(x, y, w, h);
}

// Segmented level bar (an LED ladder, left to right), 0 dB marked.
function levelSegs(x, y, w, db, col, o = {}) {
  const n = o.n || 26, h = o.h || 6, p = w / n, lit = Math.round(levelFrac(db) * n), zero = Math.round(levelFrac(0) * n);
  rect(x - 2, y - 2, w + 4, h + 4, '#050505', '#333', 2, 0.8);
  for (let i = 0; i < n; i++) {
    const on = i < lit;
    rect(x + i * p + 0.5, y, p - 1.2, h, on ? (i >= zero ? OLED.amber : col) : '#1a1a1a', null, 0.5, 0, on ? 'filter="url(#bloom)"' : '');
  }
  line(x + zero * p - 0.6, y - 4, x + zero * p - 0.6, y + h + 4, T.tick, 0.9);
}
// Horizontal mini fader for a cell level.
function hfader(x, y, w, db, o = {}) {
  rect(x, y - 2, w, 4, '#030303', '#3a3a3a', 2, 0.7);
  const zx = x + w * levelFrac(0);
  for (let i = 0; i <= 8; i++) { const tx = x + w * i / 8; line(tx, y + 5, tx, y + (i % 4 === 0 ? 8.5 : 7), T.tick, 0.7); }
  line(zx, y - 7.5, zx, y - 4.5, T.red, 1.1);
  const cx = x + w * levelFrac(db);
  rect(cx - 5, y - 6, 10, 12, 'url(#fadergrad)', '#000', 1.2, 0.7);
  line(cx, y - 4.5, cx, y + 4.5, o.pointer || T.ink, 1.4);
  LY.faders.push({ x0: cx - 5, y0: y - 6, x1: cx + 5, y1: y + 6, slot: [x, y - 2, x + w, y + 2] });
}
const dbTxt = db => db <= -40 ? 'OFF' : (db > 0 ? '+' : '') + db.toFixed(1) + ' DB';

// A stereo output meter on an OLED, peak about -8 dBFS.
function outMeter(x, y, w, h, o = {}) {
  oled(x, y, w, h);
  const marks = [[6, '+6'], [0, '0'], [-6, '-6'], [-12, '-12'], [-24, '-24'], [-48, '-48']];
  const top = y + 16, bot = y + h - 10, my = db => top + (bot - top) * (1 - Math.pow((db + 60) / 66, 1.6));
  const bw = o.bw || 22, gap = (w - 2 * bw) / 3, segs = 30;
  ['L', 'R'].forEach((ch, k) => {
    const bx = x + gap + k * (bw + gap), lvl = k ? -9 : -7.5, pk = k ? -4.5 : -3.2;
    for (let i = 0; i < segs; i++) {
      const db = -60 + 66 * Math.pow(i / segs, 1 / 1.6), sy = my(db), sy2 = my(-60 + 66 * Math.pow((i + 1) / segs, 1 / 1.6));
      const c = db >= 0 ? T.red : db >= -6 ? OLED.amber : OLED.mint, on = db < lvl;
      rect(bx, sy2 + 0.8, bw, Math.max(1, sy - sy2 - 1.6), on ? c : hexA(c, 0.08), null, 0.5, 0, on ? 'filter="url(#bloom)"' : '');
    }
    line(bx, my(pk), bx + bw, my(pk), OLED.amber, 1.4, 'filter="url(#bloom)"');
    otext(bx + bw / 2, y + 11, ch, T.oledText, 'middle', 8);
  });
  marks.forEach(([db, s]) => otext(x + w / 2, my(db) + 2.8, s, db >= 0 ? T.red : T.oledText, 'middle', 7));
  glass(x, y, w, h);
}

// ---- the output isolator (PORTING-NOTES §14): LR4 splits at bass / treble Hz,
// bass and treble bands from kill to +6 dB, mid at unity. Demo settings below.
const ISO = { bass: 4.0, bassHz: 200, treble: -18.0, trebleHz: 3500 };
const isoGainFrac = db => (db + 40) / 46;
const isoBassFrac = hz => Math.log(hz / 50) / Math.log(10);
const isoTrebFrac = hz => Math.log(hz / 1500) / Math.log(8);
const hzTxt = hz => hz >= 1000 ? (hz / 1000).toFixed(2) + ' KHZ' : Math.round(hz) + ' HZ';
const isoDb = db => db <= -40 ? 'KILL' : (db > 0 ? '+' : '') + db.toFixed(1) + ' DB';
function isoMag(f) {
  const L = 1 / (1 + Math.pow(f / ISO.bassHz, 4)), Hh = Math.pow(f / ISO.trebleHz, 4) / (1 + Math.pow(f / ISO.trebleHz, 4));
  const M = Math.max(0, 1 - L - Hh), g = db => db <= -40 ? 0 : Math.pow(10, db / 20);
  return g(ISO.bass) * L + M + g(ISO.treble) * Hh;
}
function isoOled(x, y, w, h, col, o = {}) {
  oled(x, y, w, h);
  const top = y + (o.label === false ? 6 : 16), bot = y + h - (o.foot === false ? 6 : 14);
  const fx = f => x + 6 + Math.log(f / 20) / Math.log(1000) * (w - 12);
  const fy = db => top + (bot - top) * (6 - Math.max(-30, db)) / 36;
  // band tints
  rect(x + 6, top, fx(ISO.bassHz) - x - 6, bot - top, hexA(col, 0.06), null, 0);
  rect(fx(ISO.trebleHz), top, x + w - 6 - fx(ISO.trebleHz), bot - top, hexA(col, 0.06), null, 0);
  line(x + 6, fy(0), x + w - 6, fy(0), T.oledText, 0.6, 'stroke-dasharray="2 3"');
  [ISO.bassHz, ISO.trebleHz].forEach(f => line(fx(f), top, fx(f), bot, hexA(col, 0.5), 0.8, 'stroke-dasharray="3 3"'));
  const pts = [];
  for (let i = 0; i <= 160; i++) { const f = 20 * Math.pow(1000, i / 160); pts.push([fx(f), fy(20 * Math.log10(isoMag(f)))]); }
  const d = 'M' + pts.map(p => `${f1(p[0])},${f1(p[1])}`).join(' L');
  path(`${d} L${f1(x + w - 6)},${f1(bot)} L${x + 6},${f1(bot)} Z`, 'none', 0, fillGrad(col));
  trace(d, col);
  const sz = o.size || 7.5;
  if (o.label !== false) { otext(x + 7, y + 11, 'LOW', T.oledText, 'start', sz); otext((fx(ISO.bassHz) + fx(ISO.trebleHz)) / 2, y + 11, 'MID', T.oledText, 'middle', sz); otext(x + w - 7, y + 11, 'HIGH', T.oledText, 'end', sz); }
  if (o.foot !== false) { otext(fx(ISO.bassHz), y + h - 4, hzTxt(ISO.bassHz), col, 'middle', sz); otext(fx(ISO.trebleHz), y + h - 4, hzTxt(ISO.trebleHz), col, 'middle', sz); }
  glass(x, y, w, h);
}
// The four controls; pos = [[x,y] x4], knob outer radius r.
function isoKnobs(pos, r, o = {}) {
  const lab = o.labels || ['BASS', 'BASS FREQ', 'TREBLE', 'TREBLE FREQ'];
  const v = [[isoGainFrac(ISO.bass), isoDb(ISO.bass)], [isoBassFrac(ISO.bassHz), hzTxt(ISO.bassHz)],
             [isoGainFrac(ISO.treble), isoDb(ISO.treble)], [isoTrebFrac(ISO.trebleHz), hzTxt(ISO.trebleHz)]];
  pos.forEach(([x, y], i) => knob(x, y, r, v[i][0], v[i][1], lab[i], { noEnds: true, vs: o.vs || 8.5, ls: o.ls || 8.5, vgap: o.vgap, lgap: o.lgap }));
}
