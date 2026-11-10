// Design 2 - Channel Strips: each drum is a strip, like a mixer. Note box at
// the head, its four velocity cells stacked soft to hard, a knob for each
// cell's level, a hit lamp at the foot. Every screen amber. Master on the right.
window.WMT = ['SAMPLE', 'DRUM'];
OLED = { amber: '#ffae2e', blue: '#ffae2e', mint: '#ffae2e', violet: '#ffae2e', white: '#ffae2e' };
header(`48000 HZ  /  32 VOICES  /  ${nLoaded()} OF 32 CELLS`, { wm: ['#f4f1e8', '#ffae2e'], divX: 330, segs: 32, lit: 2 });

const A = OLED.amber, SX = c => 16 + c * 118, SW = 110, RY = r => 146 + r * 112, OW = 94, OH = 62;
const LIT = { '0,3': 0.2, '3,1': 0.6 };
DRUMS.forEach((d, c) => {
  const x = SX(c);
  section(x, 72, SW, 552, d.name.length > 8 ? d.short : d.name);
  noteBox(x + SW / 2, 114, 92, d, { col: A });
  for (let r = 0; r < 4; r++) {
    const y = RY(r), ox = x + 8, name = FILES[r][c], key = `${c},${r}`, lit = LIT[key];
    oled(ox, y, OW, OH);
    if (lit !== undefined) rect(ox - 3, y - 3, OW + 6, OH + 6, 'none', A, 4, 1.6, 'filter="url(#bloom)"');
    otext(ox + OW - 5, y + 11, `${BANDS[r][0]}-${BANDS[r][1]}`, T.oledText, 'end', 7);
    if (name) {
      otext(ox + 5, y + OH - 5, name, lit !== undefined ? '#fff2dc' : A, 'start', 7);
      waveform(ox + 4, y + 14, OW - 8, OH - 28, d.kind, r, A, { dim: lit === undefined });
      if (lit !== undefined) line(ox + 4 + (OW - 8) * lit, y + 14, ox + 4 + (OW - 8) * lit, y + OH - 14, '#fff', 1, 'opacity="0.8"');
    } else {
      const fb = fallback(c, r);
      otext(ox + OW / 2, y + OH / 2, 'EMPTY', T.oledText, 'middle', 7.5);
      if (fb >= 0) otext(ox + OW / 2, y + OH / 2 + 12, `PLAYS ${BANDS[fb][0]}-${BANDS[fb][1]} ${fb > r ? '↓' : '↑'}`, hexA(A, 0.7), 'middle', 6.5);
    }
    glass(ox, y, OW, OH);
    const db = LEVELS[r][c];
    knob(ox + 14, y + OH + 22, 12, levelFrac(db), null, null);
    otext(ox + 34, y + OH + 20, dbTxt(db), T.ink, 'start', 8.5);
    text(ox + 34, y + OH + 31, 'LEVEL', { size: 7, weight: 400, fill: T.dim });
  }
  const hit = c === 0 || c === 3;
  lamp(x + 22, 604, hit);
  text(x + 34, 607.5, 'HIT', { size: 8.5 });
});

// ---- MASTER -------------------------------------------------------------
section(964, 72, 220, 552, 'MASTER');
oled(978, 106, 192, 84);
otext(984, 118, 'LAST HIT', T.oledText, 'start', 7.5);
otext(1164, 118, 'VEL 112', A, 'end', 7.5);
waveform(984, 124, 180, 44, 'kick', 3, A);
line(984 + 180 * 0.2, 124, 984 + 180 * 0.2, 168, '#fff', 1, 'opacity="0.8"');
otext(984, 184, 'KICK 96-127  BD HARD 03', A, 'start', 7);
otext(1164, 184, '0.62 S', T.oledText, 'end', 7);
glass(978, 106, 192, 84);
rule(978, 204, 1170, 204);

sub(980, 224, 'ISOLATOR', { size: 9.5 });
otext(1170, 224, 'BEFORE TRIM', T.dim, 'end', 7);
isoOled(978, 234, 192, 54, A);
text(1015, 312, 'BASS', { size: 8.5, anchor: 'middle', fill: T.muted });
text(1129, 312, 'TREBLE', { size: 8.5, anchor: 'middle', fill: T.muted });
line(1072, 304, 1072, 370, '#3a3a3a', 1);
isoKnobs([[996, 332], [1040, 332], [1104, 332], [1150, 332]], 12, { labels: ['LEVEL', 'FREQ', 'LEVEL', 'FREQ'], vs: 7.5, ls: 8, vgap: 9, lgap: 10 });
rule(978, 380, 1170, 380);

knob(1018, 432, 22, 0.36, '-6.0 DB', 'OUTPUT TRIM', { bipolar: true });
outMeter(1072, 394, 98, 134, { bw: 20 });
lamp(992, 504, 0); text(1004, 507.5, 'OVER', { size: 8.5 });
rule(978, 540, 1170, 540);
sub(980, 560, 'HAT CHOKE', { size: 9.5 });
otext(980, 576, 'CLOSED HH CUTS OPEN HH', T.muted, 'start', 8);
lamp(992, 600, 0); text(1004, 603.5, 'BYPASS', { size: 9 });
otext(1170, 603.5, 'KIT SAVED IN PROJECT', T.dim, 'end', 7);

render();
