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
    res.models = [];  // every toaster you can buy, in shop order
    for (const id of ['nan', 'kwik', 'chrome', 'smart', 'auto', 'rocket', 'gold']) {
      A.S.toaster = id;
      ctx.clearRect(0, 0, 320, 208); A.toaster(0); const up = shot();
      ctx.clearRect(0, 0, 320, 208); A.toaster(1); const down = shot();
      res.models.push({ id, up, down, geom: A.geom() });
    }
    A.S.toaster = 'nan';
    const chars = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!+-/.,:%x?\'<>()';
    res.chars = chars; res.glyphs = A.glyphs(chars);
    return res;
  });
  const png = (name, url) => fs.writeFileSync(__dirname + '/../res/' + name + '.png', Buffer.from(url.split(',')[1], 'base64'));
  for (const k of ['kitchen', 'toasterUp', 'toasterDown']) png(k, out[k]);
  for (const m of out.models) { png('toaster-' + m.id + '-up', m.up); png('toaster-' + m.id + '-down', m.down); }
  fs.writeFileSync(__dirname + '/../res/art.json', JSON.stringify({ geom: out.geom, chars: out.chars, glyphs: out.glyphs,
    models: out.models.map(m => ({ id: m.id, geom: m.geom })) }));
  console.log(JSON.stringify(out.geom));
  await b.close();
})();
