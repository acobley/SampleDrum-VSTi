// SampleDrum (from ForTran) - Polivoks black-panel drawing kit (after R2Synth Polivoks 1c OLED).
// Matte black panel, white silkscreen, outlined section titles, black knurled
// knobs with printed scales, chicken-head rotary selectors, red jewel lamps and
// small colour OLEDs behind glass. All text is later converted to the 70s
// sci-fi lettering by rbConvert(); the weathering pass reads window.LAYOUT.
const T = {
  ground: '#121212', header: '#0c0c0c', headerRule: '#e9e6dc',
  sec: '#151515', secStroke: '#cfccc4', ink: '#f4f1e8', muted: '#b9b6ae', dim: '#7d7a73',
  tick: '#e9e6dc', red: '#e2342b', lampOn: '#ff3b2f', lampOff: '#3a1512',
  oled: { amber: '#ffae2e', blue: '#2fb8ff', mint: '#38f0a0', violet: '#c27bff', white: '#eef3ff' },
  oledText: '#8d96a3', oledDot: '#2a2d33',
};
let OLED = { ...T.oled };            // a design can make every screen one colour
const MONO = 'DejaVu Sans Mono, monospace', SANS = 'DejaVu Sans, sans-serif';
const out = [], defs = [];
// STATIC: draw only what never changes - the panel background the plug-in
// loads. Everything wrapped in dyn() is drawn by the plug-in itself.
const STATIC = !!window.STATIC;
let SKIP = false;
const add = s => { if (!SKIP) out.push(s); };
function dyn (f) { const o = SKIP; SKIP = STATIC || o; f (); SKIP = o; }
const f1 = n => Math.round(n * 100) / 100;
window.LAYOUT = { knobs: [], sels: [], screens: [], faders: [], sections: [], screws: [], lamps: [] };
const LY = window.LAYOUT;

function rect(x, y, w, h, fill, stroke, r = 0, sw = 1, extra = '') {
  add(`<rect x="${f1(x)}" y="${f1(y)}" width="${f1(w)}" height="${f1(h)}" rx="${r}" fill="${fill}" ${stroke ? `stroke="${stroke}" stroke-width="${sw}"` : ''} ${extra}/>`);
}
function line(x1, y1, x2, y2, stroke, sw = 1, extra = '') {
  add(`<line x1="${f1(x1)}" y1="${f1(y1)}" x2="${f1(x2)}" y2="${f1(y2)}" stroke="${stroke}" stroke-width="${sw}" stroke-linecap="round" ${extra}/>`);
}
function circle(cx, cy, r, fill, stroke, sw = 1, extra = '') {
  add(`<circle cx="${f1(cx)}" cy="${f1(cy)}" r="${f1(r)}" fill="${fill}" ${stroke ? `stroke="${stroke}" stroke-width="${sw}"` : ''} ${extra}/>`);
}
// y is the baseline; the converter keeps it.
function text(x, y, s, o = {}) {
  const { size = 10, fill = T.ink, weight = 700, anchor = 'start', font = SANS } = o;
  add(`<text x="${f1(x)}" y="${f1(y)}" font-family="${font}" font-size="${size}" font-weight="${weight}" fill="${fill}" text-anchor="${anchor}">${s}</text>`);
}
function path(d, stroke, sw = 1.5, fill = 'none', extra = '') {
  add(`<path d="${d}" stroke="${stroke}" stroke-width="${sw}" fill="${fill}" stroke-linejoin="round" stroke-linecap="round" ${extra}/>`);
}
function pol(cx, cy, r, a) { const t = a * Math.PI / 180; return [cx + r * Math.sin(t), cy - r * Math.cos(t)]; }
function hexA(hex, a) { const n = parseInt(hex.slice(1), 16); return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`; }

// ---------------------------------------------------------------- knobs
// outer = radius to the end of the printed scale.
function knob(cx, cy, outer, frac, val, label, o = {}) {
  const body = outer - (outer >= 22 ? 6 : outer >= 14 ? 5 : 3.5);
  const ang = -135 + 270 * Math.max(0, Math.min(1, frac));
  const nt = outer >= 14 ? 11 : 7;
  for (let i = 0; i < nt; i++) {
    const t = -135 + 270 * i / (nt - 1), long = i === 0 || i === nt - 1 || i === (nt - 1) / 2;
    const [x0, y0] = pol(cx, cy, body + 1.5, t), [x1, y1] = pol(cx, cy, outer - (long ? 0 : 1.6), t);
    line(x0, y0, x1, y1, T.tick, long ? 1.2 : 0.85);
  }
  if (outer >= 22 && !o.noEnds) {
    const lo = o.bipolar ? '-' : '0', hi = o.bipolar ? '+' : '10';
    let [x, y] = pol(cx, cy, outer + 2, -138); text(x - 2, y + 7, lo, { size: 7.5, weight: 400, anchor: 'end', fill: T.tick });
    [x, y] = pol(cx, cy, outer + 2, 138); text(x + 2, y + 7, hi, { size: 7.5, weight: 400, fill: T.tick });
  }
  dyn (() => {
  const teeth = outer >= 14 ? 28 : 18, pts = [];
  for (let i = 0; i < teeth * 2; i++) {
    const rr = i % 2 ? body - (outer >= 14 ? 1.4 : 0.9) : body;
    pts.push(pol(cx, cy, rr, 360 * i / (teeth * 2)).map(f1).join(','));
  }
  add(`<polygon points="${pts.join(' ')}" fill="#0a0a0a" stroke="#2f2f2f" stroke-width="0.6"/>`);
  circle(cx, cy, body * 0.78, 'url(#capgrad)', '#000', 0.8);
  const [ax, ay] = pol(cx, cy, body * 0.15, ang), [bx, by] = pol(cx, cy, body - 0.5, ang);
  line(ax, ay, bx, by, o.pointer || T.ink, outer >= 22 ? 2.2 : outer >= 14 ? 1.8 : 1.4);
  });
  LY.knobs.push({ cx, cy, body, cap: body * 0.78, label: (label || '').toUpperCase() });
  let ty = cy + outer + (o.vgap || 11);
  if (val !== null && val !== undefined && val !== '') {
    dyn (() => text(cx, ty, val, { size: o.vs || 9.5, font: MONO, weight: 400, anchor: 'middle', fill: T.ink })); ty += o.lgap || 12;
  }
  if (label) text(cx, ty, label, { size: o.ls || 9, anchor: 'middle', fill: T.ink });
}

// ------------------------------------------------- chicken-head selector
// angles: degrees from 12 o'clock for each position.
function selector(cx, cy, r, names, cur, o = {}) {
  const angles = o.angles || (names.length === 2 ? [-45, 45] : names.length === 3 ? [-50, 0, 50] : [-75, -25, 25, 75]);
  const lr = o.lr || r + 11;
  dyn (() => {
  names.forEach((nm, i) => {
    const t = angles[i], active = i === cur;
    const [x0, y0] = pol(cx, cy, r + 2.5, t), [x1, y1] = pol(cx, cy, r + 7, t);
    line(x0, y0, x1, y1, active ? T.red : T.tick, active ? 2 : 1.4);
    const [lx, ly] = pol(cx, cy, lr, t);
    const anchor = Math.abs(t) < 10 ? 'middle' : t < 0 ? 'end' : 'start';
    const dy = Math.abs(t) < 10 ? -2 : Math.abs(t) > 60 ? 3.5 : 1;
    text(lx + (anchor === 'middle' ? 0 : t < 0 ? -1 : 1), ly + dy, nm, { size: o.ns || 8.5, anchor, fill: active ? T.ink : '#86837c', weight: active ? 700 : 400 });
  });
  const ang = angles[cur];
  circle(cx, cy, r * 0.8, '#0b0b0b', '#444', 0.8);
  const tip = pol(cx, cy, r + 3, ang), l = pol(cx, cy, r * 0.5, ang - 90), rg = pol(cx, cy, r * 0.5, ang + 90), tail = pol(cx, cy, r, ang + 180);
  add(`<path d="M${f1(tip[0])},${f1(tip[1])} L${f1(rg[0])},${f1(rg[1])} Q${f1(tail[0])},${f1(tail[1])} ${f1(l[0])},${f1(l[1])} Z" fill="url(#capgrad)" stroke="#000" stroke-width="0.8"/>`);
  const [px, py] = pol(cx, cy, 2, ang);
  line(px, py, tip[0], tip[1], T.ink, 1.6);
  });
  LY.sels.push({ cx, cy, r, label: (o.label || '').toUpperCase() });
  if (o.label) text(cx, cy + r + (o.lgap || 16), o.label, { size: 9, anchor: 'middle', fill: T.ink });
}

// ------------------------------------------------------- slide fader
function fader(cx, top, bot, frac, o = {}) {
  rect(cx - 2.5, top, 5, bot - top, '#030303', '#3a3a3a', 2, 0.8);
  for (let i = 0; i <= 10; i++) {
    const y = bot - (bot - top) * i / 10, long = i % 5 === 0;
    line(cx - 9, y, cx - (long ? 14 : 12), y, T.tick, long ? 1.1 : 0.8);
    line(cx + 9, y, cx + (long ? 14 : 12), y, T.tick, long ? 1.1 : 0.8);
  }
  const y = bot - (bot - top) * frac;
  rect(cx - 8, y - 11, 16, 22, 'url(#fadergrad)', '#000', 1.5, 0.8);
  line(cx - 7, y - 4, cx + 7, y - 4, '#2c2c2c', 0.8); line(cx - 7, y + 4, cx + 7, y + 4, '#2c2c2c', 0.8);
  line(cx - 6.5, y, cx + 6.5, y, o.pointer || T.ink, 1.8);
  LY.faders.push({ x0: cx - 8, y0: y - 11, x1: cx + 8, y1: y + 11, slot: [cx - 2.5, top, cx + 2.5, bot] });
}

// ------------------------------------------------------------- lamps
function lamp(cx, cy, on, r = 4.2) {
  LY.lamps.push({ cx, cy, r });
  dyn (() => {
  circle(cx, cy, r + 1.6, '#000', '#555', 0.8);
  if (on) circle(cx, cy, r + 4, hexA(T.lampOn, 0.22));
  circle(cx, cy, r, on ? T.lampOn : T.lampOff);
  circle(cx - r * 0.3, cy - r * 0.3, r * 0.32, '#fff', null, 0, `opacity="${on ? 0.55 : 0.22}"`);
  });
}

function badge(cx, cy, n, sel) { dyn (() => {
  circle(cx, cy, 9.5, sel ? T.ink : 'none', T.ink, 1.3);
  text(cx, cy + 3.8, n, { size: 10.5, anchor: 'middle', fill: sel ? '#121212' : T.ink });
}); }

function button(x, y, w, h, label, o = {}) { dyn (() => {
  rect(x, y, w, h, '#0c0c0c', T.secStroke, 2, 1.1);
  text(x + w / 2, y + h / 2 + 3.4, label, { size: o.size || 9, anchor: 'middle', fill: T.ink });
}); }

// width of a string once converted to the lettering
function rbW(str, size, bold) {
  const sc = size * 0.64 / 6, w = bold ? 1.25 : 0.95; let x = 0;
  for (const ch of String(str).toUpperCase()) x += ch === ' ' ? 4.4 : (RB[ch] || RB['·'])[0] + w + 1.05;
  return (x - 1.05) * sc;
}
// ------------------------------------------------------------ sections
function section(x, y, w, h, title, sub, o = {}) {
  rect(x, y, w, h, T.sec, T.secStroke, 2, 1.2);
  LY.sections.push([x, y, x + w, y + h]);
  if (!title) return;
  const tx = x + 16, ty = y + 22, tw = rbW(title, 13, true) + 14;
  rect(tx - 2, ty - 11, tw, 22, T.sec, T.secStroke, 2, 1.2);
  text(tx + 5, ty + 4.5, title, { size: 13 });
  if (sub) text(tx + tw + 12, ty + 3.5, sub, { size: 8.6, weight: 400, fill: T.muted });
}
function rule(x1, y1, x2, y2) { line(x1, y1, x2, y2, '#3a3a3a', 1); }
function chevron(x, y) { path(`M${x - 5},${y - 10} L${x + 4},${y} L${x - 5},${y + 10}`, '#d9d6cc', 2.2); }
function sub(x, y, s, o = {}) { text(x, y, s, { size: o.size || 10.5, fill: o.fill || T.ink, anchor: o.anchor || 'start' }); }

// --------------------------------------------------------------- OLED
let scrN = 0;
function oled(x, y, w, h) {
  rect(x - 3, y - 3, w + 6, h + 6, '#050505', '#3c3c3c', 4, 1);
  rect(x, y, w, h, '#000', null, 2);
  for (let gx = x + 24; gx < x + w - 1; gx += 24) for (let gy = y + 12; gy < y + h - 1; gy += 12) circle(gx, gy, 0.6, T.oledDot);
  for (let gy = y + 24; gy < y + h - 1; gy += 24) for (let gx = x + 12; gx < x + w - 1; gx += 12) circle(gx, gy, 0.6, T.oledDot);
  LY.screens.push([x, y, x + w, y + h]);
}
function glass(x, y, w, h) { if (STATIC) return;
  const id = 'gl' + (scrN++);
  defs.push(`<linearGradient id="${id}" x1="0" y1="0" x2="0.35" y2="1"><stop offset="0" stop-color="#fff" stop-opacity="0.07"/><stop offset="0.45" stop-color="#fff" stop-opacity="0.015"/><stop offset="0.46" stop-color="#fff" stop-opacity="0"/></linearGradient>`);
  rect(x, y, w, h, `url(#${id})`, null, 2);
  line(x + 3, y + 0.8, x + w - 3, y + 0.8, '#fff', 0.6, 'opacity="0.12"');
}
function fillGrad(col) {
  const id = 'fg' + (scrN++);
  defs.push(`<linearGradient id="${id}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="${col}" stop-opacity="0.38"/><stop offset="1" stop-color="${col}" stop-opacity="0.02"/></linearGradient>`);
  return `url(#${id})`;
}
function trace(d, col, sw = 1.6) { path(d, col, sw, 'none', 'filter="url(#bloom)"'); }
function otext(x, y, s, col, anchor = 'start', size = 9) { text(x, y, s, { size, font: MONO, weight: 400, fill: col, anchor }); }

// ADSR curve. Times in seconds.
function adsr(x, y, w, h, A, D, S, R, total, keyUp, lvl = 1, top = 16) {
  const sx = t => x + 6 + (t / total) * (w - 12), sy = v => y + h - 6 - v * lvl * (h - top - 6);
  const pts = [];
  for (let i = 0; i <= 120; i++) {
    const t = total * i / 120; let v;
    if (t < A) v = t / A;
    else if (t < keyUp) v = S + (1 - S) * Math.exp(-6.9 * (t - A) / D);
    else { const vk = S + (1 - S) * Math.exp(-6.9 * (keyUp - A) / D); v = vk * Math.exp(-6.9 * (t - keyUp) / R); }
    pts.push([sx(t), sy(v)]);
  }
  return { d: 'M' + pts.map(p => `${f1(p[0])},${f1(p[1])}`).join(' L'), base: sy(0), x0: sx(0), x1: sx(total), kx: sx(keyUp) };
}
function envOled(x, y, w, h, col, A, D, S, R, o = {}) {
  oled(x, y, w, h);
  const p = adsr(x, y, w, h, A, D, S, R, o.total || 1.4, o.keyUp || 0.85, o.lvl || 1, o.label ? 20 : 8);
  dyn (() => {
  path(`${p.d} L${f1(p.x1)},${f1(p.base)} L${f1(p.x0)},${f1(p.base)} Z`, 'none', 0, fillGrad(col));
  line(p.kx, y + (o.label ? 18 : 4), p.kx, y + h - 4, T.oledText, 1, 'stroke-dasharray="3 3"');
  trace(p.d, col);
  if (o.right) otext(x + w - 8, y + 13, o.right, col, 'end');
  });
  if (o.label) (o.dynLabel ? dyn : (f => f ())) (() => otext(x + 8, y + 13, o.label, T.oledText));
  glass(x, y, w, h);
}

// The spectral filter: LP on, comb on Add.
function filterMag(f, fc = 2400) {
  const lp = 1 / Math.sqrt(1 + Math.pow(f / fc, 8));
  const comb = 1 + 0.18 * Math.cos(2 * Math.PI * Math.log2(f / 110) * 0.8);
  return Math.min(1.25, lp * comb);
}
function filterOled(x, y, w, h, o = {}) {
  oled(x, y, w, h);
  const col = OLED.blue, pc = OLED.amber;
  const fx = f => x + 6 + (Math.log(f / 20) / Math.log(1000)) * (w - 12);
  const fy = m => y + h - 18 - (m / 1.3) * (h - 42);
  dyn (() => {
  const pts = [];
  for (let i = 0; i <= 160; i++) { const f = 20 * Math.pow(1000, i / 160); pts.push([fx(f), fy(filterMag(f))]); }
  const d = 'M' + pts.map(p => `${f1(p[0])},${f1(p[1])}`).join(' L');
  path(`${d} L${f1(x + w - 6)},${y + h - 18} L${x + 6},${y + h - 18} Z`, 'none', 0, fillGrad(col));
  trace(d, col);
  const f0 = o.f0 || 220;
  for (let k = 1; k <= 10; k++) {
    const f = f0 * k, X = fx(f), Y = fy(filterMag(f));
    line(X, Y, X, y + h - 18, hexA(pc, 0.4), 1);
    if (k === o.sel) circle(X, Y, 4, '#000', '#fff', 1.6); else circle(X, Y, 2.6, pc);
  }
  otext(x + w - 8, y + 13, o.right || '2.40K  35%  4P', col, 'end');
  });
  otext(x + 8, y + 13, o.label || 'AT A3  HARMONICS READ HERE', T.oledText);
  otext(x + 8, y + h - 5, '20 HZ', col); otext(x + w - 8, y + h - 5, '20 KHZ', T.oledText, 'end');
  glass(x, y, w, h);
}

// ------------------------------------------------------------- header
function header(engine, o = {}) {
  rect(0, 0, 1200, 640, T.ground);
  rect(0, 0, 1200, 56, T.header);
  line(0, 55, 1200, 55, T.headerRule, 2);
  for (const [x, y] of [[8, 8], [1192, 8], [8, 632], [1192, 632]]) {
    circle(x, y, 3.2, '#3a3a3a', '#000', 0.6); line(x - 2.2, y - 2.2, x + 2.2, y + 2.2, '#222', 0.8);
    LY.screws.push([x, y]);
  }
  add(`<g id="wm" transform="translate(22,15)"></g>`);
  window.WM = o.wm || ['#f4f1e8', '#f4f1e8'];
  const dv = o.divX || 236;
  line(dv, 16, dv, 40, '#5a5852', 1);
  dyn (() => text(dv + 16, 32, engine, { size: 10.5, font: MONO, weight: 400, fill: T.ink }));
  // Voice meter: a thin OLED strip, sixteen segments, the sounding ones lit.
  text(o.vx ? o.vx - 12 : 958, 32, 'VOICES', { size: 9.5, anchor: 'end' });
  const vx = o.vx || 970;
  oled(vx, 19, 208, 18);
  const segs = o.segs || 16, lit = o.lit === undefined ? 4 : o.lit, pitch = 196 / segs, sw = pitch * 0.64;
  const vcol = o.vcol || OLED.mint, voff = o.voff || '#14211b';
  if (!STATIC) for (let i = 0; i < segs; i++) rect(vx + 6 + i * pitch, 24, sw, 8, i < lit ? vcol : voff, null, 1, 1, i < lit ? 'filter="url(#bloom)"' : '');
  glass(vx, 19, 208, 18);
}

// Demo patch, shared with the earlier ForTran concepts.
const P = {
  level: [0.92, 0.70, 0.64, 0.58, 0.52, 0.47, 0.42, 0.38, 0.34, 0.30],
  phase: [0.5, 0.5, 0.62, 0.5, 0.35, 0.5, 0.5, 0.7, 0.5, 0.5],
  inv: [0, 0, 0, 0, 1, 0, 0, 0, 1, 0],
  env: [1, 1, 1, 1, 1, 1, 1, 0, 0, 0],
  A: [0.01, 0.012, 0.012, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08],
  D: [0.9, 0.6, 0.24, 0.30, 0.25, 0.20, 0.18, 0.16, 0.14, 0.12],
  S: [0.8, 0.6, 0.45, 0.40, 0.32, 0.25, 0.20, 0.16, 0.12, 0.10],
  R: [0.5, 0.45, 0.38, 0.32, 0.28, 0.24, 0.20, 0.18, 0.16, 0.14],
  lfoRate: [0, 0, 0.35, 0, 0.5, 0, 0.62, 0, 0, 0],
  lfoDepth: [0, 0, 0.3, 0, 0.22, 0, 0.4, 0, 0, 0],
};
const ms = s => s >= 1 ? `${s.toFixed(2)} S` : `${Math.round(s * 1000)} MS`;
const pc = v => `${Math.round(v * 100)} %`;
const SEL = 2;

function render() {
  const d = `<defs>
  <radialGradient id="capgrad" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#3a3a3a"/><stop offset="0.6" stop-color="#161616"/><stop offset="1" stop-color="#050505"/></radialGradient>
  <linearGradient id="fadergrad" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3a3a3a"/><stop offset="0.5" stop-color="#161616"/><stop offset="1" stop-color="#060606"/></linearGradient>
  <filter id="bloom" x="-5%" y="-20%" width="110%" height="140%"><feGaussianBlur stdDeviation="0.9" result="b"/><feComponentTransfer in="b" result="c"><feFuncA type="linear" slope="0.7"/></feComponentTransfer><feMerge><feMergeNode in="c"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  ${defs.join('')}</defs>`;
  document.body.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="640" viewBox="0 0 1200 640" style="display:block">${d}${out.join('')}</svg>`;
  const wm = document.getElementById('wm');
  const big = (t, col, x) => { const a = rbPaths(t, 24, 1.7, 1.2); return { a, svg: `<g transform="translate(${x},0)"><g fill="none" stroke="${col}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">${a.paths}</g><g fill="none" stroke="#0c0c0c" stroke-width=".32" stroke-linecap="round" stroke-linejoin="round">${a.paths}</g></g>` }; };
  const WT = window.WMT || ['FOR', 'TRAN'];
  const p1 = big(WT[0], WM[0], 0), p2 = big(WT[1], WM[1], p1.a.width + 12);
  wm.innerHTML = p1.svg + p2.svg;
  window.wmWidth = p1.a.width + 12 + p2.a.width;
  rbConvert();
}
