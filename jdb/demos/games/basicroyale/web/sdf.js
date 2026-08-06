// The card rasteriser from art.jdb, ported line for line. The designer has to
// show what the game will draw, so it runs the same distance functions over the
// same grid rather than an approximation that drifts apart over time.
// Loaded by designer.html in the browser and by tools that check it in node.

function palette(team, accent) {
  const p = team === 0
    ? { TEAM: [50, 240, 255], ACCENT: [190, 110, 255] }
    : { TEAM: [255, 45, 175], ACCENT: [255, 190, 60] };
  p.HULL = [186, 190, 230];
  p.DARK = [122, 96, 180];
  p.LIGHT = [255, 252, 255];
  if (accent && accent.length === 3) p.ACCENT = accent.slice();
  return p;
}

// One authored row into the fields the distance functions want. An unreadable
// row draws a plain hull disc, the same way art.jdb answers one.
function expand(row) {
  const t = String(row[0]).toUpperCase();
  const n = i => Number(row[i]) || 0;
  if (t === 'DISC') return { T:'CIRC', X:n(1), Y:n(2), R:n(3), COL:String(row[4]), GLOW:n(5) };
  if (t === 'BOXR') return { T:'BOX', X:n(1), Y:n(2), HX:n(3), HY:n(4), RR:n(5), ROT:n(6),
                             COL:String(row[7]), GLOW:n(8) };
  if (t === 'BAR')  return { T:'SEG', X1:n(1), Y1:n(2), X2:n(3), Y2:n(4), R:n(5),
                             COL:String(row[6]), GLOW:n(7) };
  return { T:'CIRC', X:20, Y:20, R:7, COL:'HULL', GLOW:0 };
}

// Signed distance from (x, y) to the shape: negative inside, zero on the edge.
function dist(sh, x, y) {
  if (sh.T === 'CIRC') {
    const dx = x - sh.X, dy = y - sh.Y;
    return Math.sqrt(dx * dx + dy * dy) - sh.R;
  }
  if (sh.T === 'BOX') {
    const bx = x - sh.X, by = y - sh.Y;
    const ang = sh.ROT * Math.PI / 180;
    const ca = Math.cos(ang), sa = Math.sin(ang);
    const rx = bx * ca + by * sa, ry = by * ca - bx * sa;
    const qx = Math.abs(rx) - sh.HX + sh.RR;
    const qy = Math.abs(ry) - sh.HY + sh.RR;
    const px = qx > 0 ? qx : 0, py = qy > 0 ? qy : 0;
    const inside = Math.max(qx, qy);
    return Math.sqrt(px * px + py * py) + Math.min(inside, 0) - sh.RR;
  }
  const ex = sh.X2 - sh.X1, ey = sh.Y2 - sh.Y1;
  let elen = ex * ex + ey * ey;
  if (elen < 0.0001) elen = 0.0001;
  const px2 = x - sh.X1, py2 = y - sh.Y1;
  const tt = (px2 * ex + py2 * ey) / elen;
  const tc = tt < 0 ? 0 : (tt > 1 ? 1 : tt);
  const cx = px2 - ex * tc, cy = py2 - ey * tc;
  return Math.sqrt(cx * cx + cy * cy) - sh.R;
}

// Every geometric slot of a row times k, which is how the card sheet reaches a
// bigger canvas. It is not the same picture at more samples: the antialiasing
// band and the halo stay measured in canvas pixels, so a blown up figure keeps
// a hard edge and a tight glow where the in-game sprite keeps a soft wide one.
const SCALED = { DISC: [1, 2, 3], BOXR: [1, 2, 3, 4, 5], BAR: [1, 2, 3, 4, 5] };

function blowup(rows, k) {
  return rows.map(row => {
    const out = row.slice();
    for (const i of SCALED[String(row[0]).toUpperCase()] || []) out[i] = Number(out[i]) * k;
    return out;
  });
}

// Rasterises the rows onto a w x h grid at `scale` samples per grid unit and
// returns RGBA bytes. zoom > 1 enlarges the figure inside the same canvas: the
// grid shrinks toward the centre instead of every shape carrying scaled numbers.
function raster(rows, w, h, scale, pal, zoom) {
  const W = Math.round(w * scale), H = Math.round(h * scale);
  const n = W * H;
  const xs = new Float64Array(n), ys = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const gx = ((i % W) + 0.5) / scale;
    const gy = (Math.floor(i / W) + 0.5) / scale;
    xs[i] = (gx - w / 2) / zoom + w / 2;
    ys[i] = (gy - h / 2) / zoom + h / 2;
  }
  const or = new Float64Array(n), og = new Float64Array(n);
  const ob = new Float64Array(n), oa = new Float64Array(n);

  for (const row of rows) {
    const sh = expand(row);
    const col = pal[sh.COL] || pal.HULL;
    const glow = sh.GLOW;
    for (let i = 0; i < n; i++) {
      const d = dist(sh, xs[i], ys[i]);
      // body coverage with one grid unit of antialiasing
      let cov = 0.5 - d;
      cov = cov < 0 ? 0 : (cov > 1 ? 1 : cov);
      // halo: quadratic falloff over four and a half units outside the edge
      if (glow > 0) {
        let gd = 1 - d / 4.5;
        gd = gd < 0 ? 0 : (gd > 1 ? 1 : gd);
        if (cov < 0.999) cov += gd * gd * glow;
      }
      const keep = 1 - cov;
      or[i] = or[i] * keep + col[0] * cov;
      og[i] = og[i] * keep + col[1] * cov;
      ob[i] = ob[i] * keep + col[2] * cov;
      oa[i] = oa[i] * keep + 255 * cov;
    }
  }

  const out = new Uint8ClampedArray(n * 4);
  for (let i = 0; i < n; i++) {
    out[i * 4] = or[i];
    out[i * 4 + 1] = og[i];
    out[i * 4 + 2] = ob[i];
    out[i * 4 + 3] = oa[i];
  }
  return { data: out, w: W, h: H };
}

if (typeof module !== 'undefined') module.exports = { palette, expand, dist, raster, blowup };
