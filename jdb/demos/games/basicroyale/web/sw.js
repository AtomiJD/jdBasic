// Service worker for jdVOID. Two jobs: make the site installable, and keep the
// ten megabyte runtime out of the network path on every start.
const CACHE = 'jdvoid-v1';

// The runtime and its vendor bundle travel only on a --wasm deploy, so they are
// served from the cache and refreshed behind the player's back. Everything else
// is asked for first, because a deploy has to reach the players at once.
const HEAVY = /\/play\/(jdbasic\.(js|wasm)|vendor\/)/;

self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', ev => {
  ev.waitUntil((async () => {
    const names = await caches.keys();
    await Promise.all(names.filter(n => n !== CACHE).map(n => caches.delete(n)));
    await self.clients.claim();
  })());
});

async function fromCache(req) {
  const cache = await caches.open(CACHE);
  const hit = await cache.match(req);
  const fresh = fetch(req).then(res => {
    if (res && res.ok) cache.put(req, res.clone());
    return res;
  }).catch(() => null);
  return hit || (await fresh) || Response.error();
}

async function fromNetwork(req) {
  const cache = await caches.open(CACHE);
  try {
    const res = await fetch(req);
    if (res && res.ok) cache.put(req, res.clone());
    return res;
  } catch (e) {
    const hit = await cache.match(req);
    if (hit) return hit;
    throw e;
  }
}

self.addEventListener('fetch', ev => {
  const req = ev.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;
  // the match server answers per second and must never be replayed from a cache
  if (url.pathname.startsWith('/api/')) return;
  ev.respondWith(HEAVY.test(url.pathname) ? fromCache(req) : fromNetwork(req));
});
