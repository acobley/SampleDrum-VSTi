
// A hand-drawn stroke face: 70s sci-fi lettering. Heavy, wide,
// rounded-square capitals and figures; every letter is drawn on a 4 x 6 grid
// (y down) and stroked with round joins. Drawn from scratch, so there is no
// font file or font licence involved.
const RB = {
  A: [4, 'M0,6 L0,2 Q0,0 2,0 Q4,0 4,2 L4,6 M0,3.6 L4,3.6'],
  B: [4, 'M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6 L0,0'],
  C: [4, 'M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6'],
  D: [4, 'M0,0 L2,0 Q4,0 4,2 L4,4 Q4,6 2,6 L0,6 Z'],
  E: [4, 'M4,0 L0,0 L0,6 L4,6 M0,3 L3.2,3'],
  F: [4, 'M4,0 L0,0 L0,6 M0,3 L3.2,3'],
  G: [4, 'M4,0 L1.6,0 Q0,0 0,1.6 L0,4.4 Q0,6 1.6,6 L4,6 L4,3.2 L2.2,3.2'],
  H: [4, 'M0,0 L0,6 M4,0 L4,6 M0,3 L4,3'],
  I: [0, 'M0,0 L0,6'],
  J: [4, 'M4,0 L4,4.4 Q4,6 2.4,6 L0,6'],
  K: [4, 'M0,0 L0,6 M0,3 L1.8,3 L4,0 M1.8,3 L4,6'],
  L: [4, 'M0,0 L0,6 L4,6'],
  M: [5, 'M0,6 L0,1.4 Q0,0 1.25,0 Q2.5,0 2.5,1.4 L2.5,4.2 M2.5,1.4 Q2.5,0 3.75,0 Q5,0 5,1.4 L5,6'],
  N: [4, 'M0,6 L0,0 L4,6 L4,0'],
  O: [4, 'M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z'],
  P: [4, 'M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3'],
  Q: [4, 'M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z M2.6,4.4 L4.3,6.2'],
  R: [4, 'M0,6 L0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L0,3 M2.4,3 L4,6'],
  S: [4, 'M4,0 L1.4,0 Q0,0 0,1.5 Q0,3 1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.6,6 L0,6'],
  T: [4, 'M0,0 L4,0 M2,0 L2,6'],
  U: [4, 'M0,0 L0,4 Q0,6 2,6 Q4,6 4,4 L4,0'],
  V: [4, 'M0,0 L0,3.4 L2,6 L4,3.4 L4,0'],
  W: [5, 'M0,0 L0,4.6 Q0,6 1.25,6 Q2.5,6 2.5,4.6 L2.5,1.8 M2.5,4.6 Q2.5,6 3.75,6 Q5,6 5,4.6 L5,0'],
  X: [4, 'M0,0 L4,6 M4,0 L0,6'],
  Y: [4, 'M0,0 L0,1.4 Q0,3 2,3 Q4,3 4,1.4 L4,0 M2,3 L2,6'],
  Z: [4, 'M0,0 L4,0 L0,6 L4,6'],
  0: [4, 'M1.6,0 L2.4,0 Q4,0 4,1.6 L4,4.4 Q4,6 2.4,6 L1.6,6 Q0,6 0,4.4 L0,1.6 Q0,0 1.6,0 Z'],
  1: [1.2, 'M0,1 L1.2,0 L1.2,6'],
  2: [4, 'M0,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 L1.5,3 Q0,3 0,4.5 L0,6 L4,6'],
  3: [4, 'M0,0 L2.8,0 Q4,0 4,1.5 Q4,3 2.8,3 L1.2,3 M2.8,3 Q4,3 4,4.5 Q4,6 2.8,6 L0,6'],
  4: [4, 'M3,6 L3,0 L0,4 L4,4'],
  5: [4, 'M4,0 L0,0 L0,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L0,6'],
  6: [4, 'M3.6,0 L1.6,0 Q0,0 0,1.6 L0,4.5 Q0,6 1.5,6 L2.5,6 Q4,6 4,4.5 Q4,3 2.5,3 L0,3'],
  7: [4, 'M0,0 L4,0 L1.6,6'],
  8: [4, 'M1.5,3 Q0,3 0,1.5 Q0,0 1.5,0 L2.5,0 Q4,0 4,1.5 Q4,3 2.5,3 Z M1.5,3 L2.5,3 Q4,3 4,4.5 Q4,6 2.5,6 L1.5,6 Q0,6 0,4.5 Q0,3 1.5,3'],
  9: [4, 'M0.4,6 L2.4,6 Q4,6 4,4.4 L4,1.5 Q4,0 2.5,0 L1.5,0 Q0,0 0,1.5 Q0,3 1.5,3 L4,3'],
  '.': [0, 'M0,6 L0,6.01'], ',': [0, 'M0,6 L-0.4,7'], ':': [0, 'M0,2 L0,2.01 M0,5.5 L0,5.51'],
  '·': [0, 'M0,3 L0,3.01'], '•': [0, 'M0,3 L0,3.01'], '●': [1.6, 'M0.8,3 L0.8,3.01'],
  '%': [4, 'M0,0 L1.1,0 L1.1,1.3 L0,1.3 Z M4,0 L0,6 M2.9,4.7 L4,4.7 L4,6 L2.9,6 Z'],
  '-': [3, 'M0,3 L3,3'], '−': [3, 'M0,3 L3,3'], '–': [3, 'M0,3 L3,3'], '—': [4, 'M0,3 L4,3'],
  '+': [3, 'M0,3 L3,3 M1.5,1.5 L1.5,4.5'], '×': [3, 'M0,1.6 L3,4.6 M3,1.6 L0,4.6'],
  '/': [3, 'M0,6 L3,0'], '=': [3, 'M0,2 L3,2 M0,4 L3,4'],
  '→': [4.4, 'M0,3 L4.4,3 M2.8,1.4 L4.4,3 L2.8,4.6'], '↓': [3, 'M1.5,0 L1.5,6 M0,4.5 L1.5,6 L3,4.5'], '↑': [3, 'M1.5,6 L1.5,0 M0,1.5 L1.5,0 L3,1.5'], '#': [3.6, 'M1.2,0.6 L0.8,5.4 M2.8,0.6 L2.4,5.4 M0,2 L3.6,2 M0,4 L3.6,4'],
  '‹': [2.2, 'M2.2,1 L0,3 L2.2,5'], '›': [2.2, 'M0,1 L2.2,3 L0,5'], '⌄': [3.4, 'M0,2.4 L1.7,4 L3.4,2.4'],
  '┆': [0, 'M0,0 L0,1.4 M0,2.3 L0,3.7 M0,4.6 L0,6'], "'": [0, 'M0,0 L0,1.6'], '(': [1.2, 'M1.2,0 Q0,1 0,3 Q0,5 1.2,6'], ')': [1.2, 'M0,0 Q1.2,1 1.2,3 Q1.2,5 0,6'],
};
const RB_SPACE = 2.6;

// One string as paths. cap = cap height in px; w = stroke in grid units.
function rbPaths(str, cap, w, track) {
  const s = cap / 6; let x = 0, d = [];
  for (const ch of str.toUpperCase()) {
    if (ch === ' ') { x += RB_SPACE; continue; }
    const g = RB[ch] || RB['·'];
    d.push(`<path transform="translate(${(x + w / 2) * s},${w / 2 * s}) scale(${s})" d="${g[1]}"/>`);
    x += g[0] + w + track;
  }
  return { paths: d.join(''), width: Math.max(0, (x - track)) * s, height: cap + w * s };
}
function rbSvg(str, cap, color, w = 1.0, track = 1.05) {
  const p = rbPaths(str, cap, w, track);
  return `<svg class="rb" width="${p.width.toFixed(1)}" height="${p.height.toFixed(1)}" style="overflow:visible;vertical-align:${(-w * cap / 12).toFixed(1)}px" fill="none" stroke="${color}" stroke-width="${w}" stroke-linecap="round" stroke-linejoin="round">${p.paths}</svg>`;
}

// Replace every HTML text node and SVG <text> in the page with the face.
function rbConvert(root = document.body) {
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes = [];
  while (walker.nextNode()) {
    const n = walker.currentNode;
    if (!n.nodeValue.trim()) continue;
    if (n.parentElement.closest('script,style,svg')) continue;
    nodes.push(n);
  }
  nodes.forEach(n => {
    const el = n.parentElement, cs = getComputedStyle(el);
    const fs = parseFloat(cs.fontSize), bold = parseInt(cs.fontWeight) >= 600;
    const cap = fs * (el.dataset.rbcap ? +el.dataset.rbcap : 0.64);
    const w = bold ? 1.25 : 0.95;
    const raw = n.nodeValue.replace(/\s+/g, ' ');
    const words = raw.trim().split(' ');
    const span = document.createElement('rb-t');
    const lead = raw.startsWith(' ') ? ' ' : '', tail = raw.endsWith(' ') ? ' ' : '';
    const sp = `<rb-s style="display:inline-block;width:${(cap / 6 * 4.4).toFixed(1)}px"></rb-s>`;
    span.innerHTML = lead + (words.length > 4
      ? words.map(wd => `<rb-w style="white-space:nowrap">${rbSvg(wd, cap, cs.color, w)}</rb-w>`).join(`<rb-s style="display:inline-block;width:${(cap / 6 * 2.4).toFixed(1)}px"></rb-s> `)
      : `<rb-w style="white-space:nowrap">${words.map(wd => rbSvg(wd, cap, cs.color, w)).join(sp)}</rb-w>`) + tail;
    n.replaceWith(span);
  });
  document.querySelectorAll('svg text').forEach(t => {
    const fs = parseFloat(t.getAttribute('font-size') || 10), bold = (t.getAttribute('font-weight') || '400') >= 600;
    const cap = fs * 0.64, w = bold ? 1.25 : 0.95;
    const p = rbPaths(t.textContent, cap, w, 1.05);
    const x = +(t.getAttribute('x') || 0), y = +(t.getAttribute('y') || 0);
    const anchor = t.getAttribute('text-anchor');
    const dx = anchor === 'end' ? -p.width : anchor === 'middle' ? -p.width / 2 : 0;
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    if (t.getAttribute('transform')) g.setAttribute('transform', t.getAttribute('transform'));
    g.innerHTML = `<g transform="translate(${x + dx},${y - cap})" fill="none" stroke="${t.getAttribute('fill')}" stroke-width="${w}" stroke-linecap="round" stroke-linejoin="round">${p.paths}</g>`;
    t.replaceWith(g);
  });
}
