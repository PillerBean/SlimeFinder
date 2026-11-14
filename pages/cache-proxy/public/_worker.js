// SlimeFinder R2 cache gateway (Pages Functions advanced mode).
// Public URL: https://slimefinder-cache.pages.dev
// Reads only GET/HEAD, whitelists versioned cache paths, and reports quota status.

const ALLOWED = /^\/v3\.12\.3\/(manifest\.json|manifest\.ndjson|(?:local|downloaded)\/[A-Za-z0-9._-]+\.slmz)$/;

function jsonResponse(status, obj, extraHeaders = {}) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'cache-control': 'no-store',
      ...extraHeaders,
    },
  });
}

function contentTypeFor(key) {
  if (key.endsWith('.json')) return 'application/json; charset=utf-8';
  return 'application/octet-stream';
}

function quotaHeaders(state, pct) {
  const headers = {};
  if (state && state !== 'ok') {
    headers['x-slimefinder-quota'] = state;
    if (typeof pct === 'number') headers['x-slimefinder-quota-pct'] = String(pct);
  }
  return headers;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === '/' || url.pathname === '/health') {
      return jsonResponse(200, { ok: true, service: 'slimefinder-cache' });
    }
    if (!ALLOWED.test(url.pathname)) {
      return jsonResponse(404, { ok: false, error: 'not_found' });
    }
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return jsonResponse(405, { ok: false, error: 'method_not_allowed' }, { allow: 'GET, HEAD' });
    }
    if (!env.CACHE_BUCKET) {
      return jsonResponse(503, { ok: false, error: 'bucket_binding_missing' });
    }

    let quota = null;
    try {
      if (env.QUOTA_KV) quota = await env.QUOTA_KV.get('status', 'json');
    } catch (_) {
      quota = null;
    }
    const state = quota?.state || 'ok';
    const pct = typeof quota?.pct === 'number' ? quota.pct : null;

    if (state === 'exhausted') {
      if (url.pathname.endsWith('manifest.json') || url.pathname.endsWith('manifest.ndjson')) {
        return jsonResponse(503, {
          ok: false,
          error: 'r2_quota_exhausted',
          message: 'R2 本月服务额度已用尽，需要等待下个月恢复；期间只能尝试网盘下载。',
        }, quotaHeaders(state, pct));
      }
      return jsonResponse(429, {
        ok: false,
        error: 'r2_quota_exhausted',
        message: 'R2 本月服务额度已用尽，需要等待下个月恢复；期间只能尝试网盘下载。',
      }, quotaHeaders(state, pct));
    }

    // 流式清单：从现有 manifest.json 生成 NDJSON，客户端按行递增显示。
    if (url.pathname.endsWith('/manifest.ndjson')) {
      const manifestObject = await env.CACHE_BUCKET.get('v3.12.3/manifest.json');
      if (!manifestObject) return jsonResponse(404, { ok: false, error: 'manifest_not_found' });
      const manifest = JSON.parse(await manifestObject.text());
      const packs = Array.isArray(manifest.packs) ? manifest.packs : [];
      const header = { ...manifest, kind: 'header', totalPacks: packs.length };
      delete header.packs;
      const encoder = new TextEncoder();
      const headers = new Headers();
      headers.set('content-type', 'application/x-ndjson; charset=utf-8');
      headers.set('cache-control', 'no-cache');
      headers.set('x-slimefinder-total-packs', String(packs.length));
      for (const [k, v] of Object.entries(quotaHeaders(state, pct))) headers.set(k, v);
      const stream = new ReadableStream({
        start(controller) {
          controller.enqueue(encoder.encode(JSON.stringify(header) + '\n'));
          for (let i = 0; i < packs.length; i += 16) {
            let chunk = '';
            for (let j = i; j < Math.min(packs.length, i + 16); ++j) {
              chunk += JSON.stringify({ kind: 'pack', ...packs[j] }) + '\n';
            }
            controller.enqueue(encoder.encode(chunk));
          }
          controller.close();
        },
      });
      return new Response(stream, { headers });
    }

    const key = url.pathname.slice(1); // bucket key without leading slash
    const object = await env.CACHE_BUCKET.get(key);
    if (!object) {
      return jsonResponse(404, { ok: false, error: 'object_not_found', key });
    }

    const headers = new Headers();
    headers.set('content-type', contentTypeFor(key));
    headers.set('etag', object.httpEtag || '');
    headers.set('cache-control', key.endsWith('manifest.json')
      ? 'no-cache'
      : 'public, max-age=31536000, immutable');
    for (const [k, v] of Object.entries(quotaHeaders(state, pct))) headers.set(k, v);

    if (request.method === 'HEAD' || !object.body) {
      headers.set('content-length', String(object.size));
      return new Response(null, { headers });
    }
    return new Response(object.body, { headers });
  },
};
