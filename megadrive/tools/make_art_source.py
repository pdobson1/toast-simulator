#!/usr/bin/env python3
"""Makes tools/art-source.html: the browser game (../index.html) with hooks that let render_art.js draw its art on demand."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, '..', '..', 'index.html')).read()
src = src.replace("function loop(now) {", "function loop(now) {\n  if (window.__stop) return;", 1)
hook = """
window.__art = {
  setScene(h) { H = h; OY = Math.round((H - 180) * 0.6); TOP = 23 - OY; SB = H - OY; cv.height = H; ctx.imageSmoothingEnabled = false; },
  kitchen() { ctx.save(); ctx.translate(0, OY); drawKitchen(); ctx.restore(); },
  toaster(lever) { R.state = 'flying'; R.lever = lever; ctx.save(); ctx.translate(0, OY); drawToaster(tt()); ctx.restore(); },
  geom() { return Object.assign(geom(tt()), { oy: OY, w: tt().w, h: tt().h }); },
  glyphs(chars) { return chars.split('').map(c => { const g = glyph(c, '#ffffff'); return Array.from(g.getContext('2d').getImageData(0, 0, 9, 9).data); }); },
  S, R, ctx, cv,
};
"""
i = src.rindex("})();")
open(os.path.join(HERE, 'art-source.html'), 'w').write(src[:i] + hook + src[i:])
