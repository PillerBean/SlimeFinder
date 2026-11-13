// SlimeFinder R2 cache gateway (Pages Functions advanced mode).
// Public URL: https://slimefinder-cache.pages.dev
// Reads only GET/HEAD, whitelists versioned cache paths, and reports quota status.

const ALLOWED = /^\/v3\.12\.3\/(manifest\.json|(?:local|downloaded)\/[A-Za-z0-9._-]+\.slmz)$/;

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
      if (url.pathname.endsWith('manifest.json')) {
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
