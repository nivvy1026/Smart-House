const CACHE_NAME = "smarthouse-shell-v1";

const APP_SHELL = [
  "./index.html",
  "./manifest.json",
  "./icons/icon-192.png",
  "./icons/icon-512.png"
];

// Cache the static shell on install
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => cache.addAll(APP_SHELL))
  );
  self.skipWaiting();
});

// Clean up old cache versions on activate
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(
        keys.map((key) => {
          if (key !== CACHE_NAME) return caches.delete(key);
        })
      )
    )
  );
  self.clients.claim();
});

// Only cache-serve same-origin app shell files.
// Everything else (MQTT over WebSockets, ThingSpeak's HTTPS API) passes
// straight through to the network, untouched and never cached, since that
// data is live and must never be served stale.
self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  const isSameOrigin = url.origin === self.location.origin;

  if (!isSameOrigin || event.request.method !== "GET") {
    return; // let the browser handle it normally
  }

  event.respondWith(
    caches.match(event.request).then((cached) => {
      return (
        cached ||
        fetch(event.request).then((response) => {
          // Keep the shell cache fresh with whatever was just fetched
          const responseClone = response.clone();
          caches.open(CACHE_NAME).then((cache) => {
            cache.put(event.request, responseClone);
          });
          return response;
        })
      );
    })
  );
});
