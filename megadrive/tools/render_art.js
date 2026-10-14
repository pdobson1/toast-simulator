// Renders the browser game's art into PNGs for the Mega Drive converter: the kitchen, the toaster and the font.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const b = await chromium.launch(); const p = await b.newPage();
  await p.addInitScript(() => { window.__stop = true; localStorage.setItem('toast-simulator-v1', JSON.stringify({ clock: 610, sound: false, plate: [], spreads: [] })); });
  await p.goto('file://' + __dirname + '/art-source.html');
  await p.evaluate(() => document.fonts.load('8px "Press Start 2P"'));
  await p.waitForTimeout(500);
  const out = await p.evaluate(() => {
    const A = window.__art; A.R.cat = null; A.R.catTimer = 1e9; A.R.t = 0; A.R.parts = [];
    A.setScene(208);
    const ctx = A.ctx, cv = A.cv, shot = () => cv.toDataURL('image/png');
    const res = {};
    ctx.clearRect(0, 0, 320, 208); A.kitchen(); res.kitchen = shot();
    ctx.clearRect(0, 0, 320, 208); A.toaster(0); res.toasterUp = shot();
    ctx.clearRect(0, 0, 320, 208); A.toaster(1); res.toasterDown = shot();
    res.geom = A.geom();
    const chars = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!+-/.,:%x?\'';
    res.chars = chars; res.glyphs = A.glyphs(chars);
    return res;
  });
  for (const k of ['kitchen', 'toasterUp', 'toasterDown']) fs.writeFileSync(__dirname + '/../res/' + k + '.png', Buffer.from(out[k].split(',')[1], 'base64'));
  fs.writeFileSync(__dirname + '/../res/art.json', JSON.stringify({ geom: out.geom, chars: out.chars, glyphs: out.glyphs }));
  console.log(JSON.stringify(out.geom));
  await b.close();
})();
