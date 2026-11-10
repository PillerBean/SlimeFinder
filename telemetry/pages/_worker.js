// SlimeFinder 匿名统计接收端（Cloudflare Worker + D1）
// 只接受用户主动开启后发送的统计事件；事件内容见 docs/privacy.md。
// 不保存 IP、不保存账号；payload 原样截断保存，便于核对字段。
//
// 路由：
//   POST /v1/event  JSON 事件 → 写入 D1
//   GET  /v1/ping   → 健康检查
//   GET  /v1/stats?token=... → 可选聚合（需要 STATS_TOKEN secret）

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === '/v1/ping') {
      return new Response('ok', { status: 200, headers: { 'content-type': 'text/plain' } });
    }
    if (url.pathname === '/v1/stats') return stats(request, env, url);
    if (url.pathname !== '/v1/event') return json({ error: 'not_found' }, 404);
    if (request.method !== 'POST') return json({ error: 'method_not_allowed' }, 405);
    const len = Number(request.headers.get('content-length') || '0');
    if (len > 4096) return json({ error: 'payload_too_large' }, 413);

    let p;
    try {
      p = await request.json();
    } catch {
      return json({ error: 'bad_json' }, 400);
    }
    if (!p || p.schema !== 1 || typeof p.event !== 'string' || p.event.length > 32) {
      return json({ error: 'bad_schema' }, 400);
    }
    const event = String(p.event);
    if (!['app_start', 'search', 'cache_build'].includes(event)) {
      return json({ error: 'bad_event' }, 400);
    }

    const app = obj(p.app);
    const os = obj(p.os);
    const hw = obj(p.hw);
    const s = obj(p.search);
    const sp = obj(s.speed);
    const rs = obj(s.results);
    const settings = JSON.stringify(obj(s.settings)).slice(0, 1800);
    const resultsJson = JSON.stringify(rs).slice(0, 1800);

    const row = {
      event,
      app_version: str(app.version, 24),
      channel: str(app.channel, 16),
      language: str(app.language, 8),
      platform: str(s.edition || p.platform, 16),
      os_name: str(os.name, 64),
      os_arch: str(os.arch, 16),
      cpu: str(hw.cpu, 96),
      cpu_threads: intOrNull(hw.cpu_threads, 1, 1024),
      ram_gb: intOrNull(hw.ram_gb, 1, 4096),
      gpu: str(hw.gpu, 96),
      gpu_available: boolInt(hw.gpu_available),
      edition: str(s.edition, 16),
      mode: str(s.mode, 16),
      algorithm: str(s.algorithm, 16),
      ring_scan: str(s.ring_scan, 16),
      seed: str(s.seed, 64),
      range_log10: intOrNull(s.range_log10, 0, 30),
      range_x: intOrNull(s.range?.max_x, -30000000, 30000000),
      range_z: intOrNull(s.range?.max_z, -30000000, 30000000),
      duration_ms: intOrNull(sp.total_ms ?? s.duration_ms, 0, 86400000),
      scan_ms: intOrNull(sp.scan_ms, 0, 86400000),
      match_ms: intOrNull(sp.match_ms, 0, 86400000),
      scanned_chunks: intOrNull(sp.scanned_chunks, 0, 1e18),
      chunks_per_sec: numOrNull(sp.chunks_per_sec),
      result_count: intOrNull(rs.count, 0, 1000000),
      total_found: intOrNull(rs.total_found, 0, 1e18),
      best_value: numOrNull(rs.best_value),
      best_x: intOrNull(rs.best_x, -30000000, 30000000),
      best_z: intOrNull(rs.best_z, -30000000, 30000000),
      cancelled: boolInt(s.cancelled),
      stopped_early: boolInt(s.stopped_early),
      gpu_used: boolInt(s.gpu_used),
      settings_json: settings,
      results_json: resultsJson,
      payload: JSON.stringify(p).slice(0, 3500)
    };

    try {
      await env.DB.prepare(
        `INSERT INTO telemetry
         (received_at, event, app_version, channel, language, platform, os_name, os_arch,
          cpu, cpu_threads, ram_gb, gpu, gpu_available, edition, mode, algorithm, ring_scan,
          seed, range_log10, range_x, range_z, duration_ms, scan_ms, match_ms, scanned_chunks,
          chunks_per_sec, result_count, total_found, best_value, best_x, best_z,
          cancelled, stopped_early, gpu_used, settings_json, results_json, payload)
         VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`
      ).bind(
        new Date().toISOString(), row.event, row.app_version, row.channel, row.language,
        row.platform, row.os_name, row.os_arch, row.cpu, row.cpu_threads, row.ram_gb,
        row.gpu, row.gpu_available, row.edition, row.mode, row.algorithm, row.ring_scan,
        row.seed, row.range_log10, row.range_x, row.range_z, row.duration_ms, row.scan_ms,
        row.match_ms, row.scanned_chunks, row.chunks_per_sec, row.result_count,
        row.total_found, row.best_value, row.best_x, row.best_z, row.cancelled,
        row.stopped_early, row.gpu_used, row.settings_json, row.results_json, row.payload
      ).run();
    } catch {
      return json({ error: 'store_failed' }, 500);
    }
    return new Response(null, { status: 204, headers: { 'access-control-allow-origin': '*' } });
  }
};

function obj(v) { return v && typeof v === 'object' && !Array.isArray(v) ? v : {}; }
function str(v, max) { return typeof v === 'string' ? v.slice(0, max) : ''; }
function boolInt(v) { return v === true ? 1 : 0; }
function intOrNull(v, lo, hi) {
  if (typeof v !== 'number' || !Number.isFinite(v)) return null;
  const n = Math.round(v);
  return n >= lo && n <= hi ? n : null;
}
function numOrNull(v) {
  return typeof v === 'number' && Number.isFinite(v) ? v : null;
}
function json(o, status) {
  return new Response(JSON.stringify(o), {
    status,
    headers: { 'content-type': 'application/json', 'access-control-allow-origin': '*' }
  });
}

async function stats(request, env, url) {
  const token = url.searchParams.get('token') || request.headers.get('x-stats-token') || '';
  if (!env.STATS_TOKEN || token !== env.STATS_TOKEN) return json({ error: 'forbidden' }, 403);
  const { results } = await env.DB.prepare(
    `SELECT event, app_version, platform, edition, mode, COUNT(*) AS n,
            AVG(duration_ms) AS avg_ms, AVG(chunks_per_sec) AS avg_cps,
            SUM(gpu_used) AS gpu_events, AVG(best_value) AS avg_best
     FROM telemetry GROUP BY event, app_version, platform, edition, mode
     ORDER BY n DESC LIMIT 100`
  ).all();
  return json({ rows: results }, 200);
}
