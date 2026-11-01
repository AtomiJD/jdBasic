// Service worker for jdTower, scope /tower/. Makes the page installable and
// keeps the ten megabyte runtime out of the network path on every start.
const CACHE = 'jdtower-v3';

// The runtime and its vendor bundle change only on a --wasm deploy, so they
// come from the cache and are refreshed in the background. Everything else
// (game source, level files) is asked for first, so a deploy reaches players at once.
const HEAVY = /\/tower\/play\/(jdbasic\.(js|wasm)|vendor\/)/;

self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', ev => {
  ev.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(n => n.startsWith('jdtower-') && n !== CACHE).map(n => caches.delete(n)));
    await self.clients.claim();
  })());
});

// Data files carry ?v=<version>; the offline copy is kept once per path
function keyOf(req) {
  const u = new URL(req.url);
  u.search = '';
  return u.toString();
}

async function fromCache(req) {
  const cache = await caches.open(CACHE);
  const hit = await cache.match(req);
  const fresh = fetch(req.url, {cache: 'no-cache'}).then(res => {
    if (res && res.ok) cache.put(req, res.clone());
    return res;
  }).catch(() => null);
  return hit || (await fresh) || Response.error();
}

async function fromNetwork(req) {
  const cache = await caches.open(CACHE);
  try {
    // a navigation keeps its own request; everything else revalidates against the server
    const res = await (req.mode === 'navigate' ? fetch(req) : fetch(req.url, {cache: 'no-cache'}));
    if (res && res.ok) cache.put(keyOf(req), res.clone());
    return res;
  } catch (e) {
    const hit = await cache.match(keyOf(req));
    if (hit) return hit;
    throw e;
  }
}

self.addEventListener('fetch', ev => {
  const req = ev.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;
  // scores and the leaderboard are live and never answered from a cache
  if (url.pathname.startsWith('/tower/api/')) return;
  ev.respondWith(HEAVY.test(url.pathname) ? fromCache(req) : fromNetwork(req));
});
