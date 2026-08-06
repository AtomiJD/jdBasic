// Holds web/sdf.js against the rasteriser it was ported from. The designer
// draws its previews with the port, so a drift between the two would show the
// author a figure the game never renders.
//
//   jdbasic sdf_ref.jdb DROID 1     -> tmp/sdf_ref_DROID_1.txt   board sprite
//   jdbasic sdf_ref.jdb DROID 4     -> tmp/sdf_ref_DROID_4.txt   card sheet
//   node sdf_check.js DROID
//
// The two sizes are not the same picture at two resolutions: the sheet scales
// every coordinate but leaves the antialiasing band and the halo measured in
// canvas pixels, so a card figure has a tighter glow than the board sprite.
const fs = require('fs');
const { palette, raster, blowup } = require('./web/sdf.js');

const cards = JSON.parse(fs.readFileSync(__dirname + '/cards.json', 'utf8'));
const kinds = process.argv.slice(2);
if (!kinds.length) kinds.push(...Object.keys(cards));

let bad = 0, ran = 0;
for (const kind of kinds) {
  if (!cards[kind]) { console.log(kind + ': unknown card'); bad++; continue; }
  for (const k of [1, 4]) {
    const path = __dirname + '/tmp/sdf_ref_' + kind + '_' + k + '.txt';
    if (!fs.existsSync(path)) continue;
    const want = fs.readFileSync(path, 'utf8').trim().split(/\s+/).map(Number);
    const rows = k === 1 ? cards[kind].SHAPE : blowup(cards[kind].SHAPE, k);
    const got = raster(rows, 40 * k, 40 * k, 1, palette(0, cards[kind].COLOR), 1.18).data;
    let off = 0, worst = 0;
    for (let i = 0; i < want.length; i++) {
      const d = Math.abs(want[i] - got[i]);
      if (d) off++;
      if (d > worst) worst = d;
    }
    ran++;
    if (off) bad++;
    console.log((kind + ' x' + k).padEnd(18) +
      (off ? off + ' of ' + want.length + ' bytes differ, worst ' + worst
           : want.length + ' bytes identical'));
  }
}
if (!ran) console.log('no reference dumps in tmp/ - run sdf_ref.jdb first');
process.exit(bad ? 1 : 0);
