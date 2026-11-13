// SlimeFinder R2 quota monitor.
// Runs from a Cron Trigger and writes a tiny status object to QUOTA_KV.
// Pages gateway reads that status; it never trusts a per-request counter.

const FREE_STORAGE = 10 * 1024 * 1024 * 1024;
const FREE_CLASS_A = 1000000;
const FREE_CLASS_B = 10000000;
const WARN = 0.80;
const STOP = 0.90;

function monthStartUtc(now = new Date()) {
  return new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1, 0, 0, 0));
}

function classify(actionType) {
  const a = String(actionType || '');
  if (a.startsWith('Get') || a === 'HeadObject') return 'B';
  return 'A';
}

async function queryGraphql(env, query, variables) {
  const resp = await fetch('https://api.cloudflare.com/client/v4/graphql', {
    method: 'POST',
    headers: {
      authorization: `Bearer ${env.CF_API_TOKEN}`,
      'content-type': 'application/json',
    },
    body: JSON.stringify({ query, variables }),
  });
  if (!resp.ok) throw new Error(`graphql_http_${resp.status}`);
  const body = await resp.json();
  if (body.errors) throw new Error(`graphql_errors_${JSON.stringify(body.errors).slice(0, 300)}`);
  return body.data;
}

async function updateQuota(env) {
  const now = new Date();
  const start = monthStartUtc(now).toISOString();
  const end = now.toISOString();
  const query = `query R2Quota($accountTag: string!, $start: Time, $end: Time, $bucket: string) {
    viewer { accounts(filter: {accountTag: $accountTag}) {
      r2OperationsAdaptiveGroups(limit: 10000, filter: {datetime_geq: $start, datetime_leq: $end, bucketName: $bucket}) {
        sum { requests } dimensions { actionType }
      }
      r2StorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $start, datetime_leq: $end, bucketName: $bucket}, orderBy: [datetime_DESC]) {
        max { payloadSize metadataSize } dimensions { datetime }
      }
    } }
  }`;
  const data = await queryGraphql(env, query, {
    accountTag: env.CF_ACCOUNT_ID,
    start,
    end,
    bucket: env.CACHE_BUCKET_NAME,
  });
  const account = data?.viewer?.accounts?.[0] || {};
  let classA = 0;
  let classB = 0;
  for (const row of account.r2OperationsAdaptiveGroups || []) {
    const n = Number(row?.sum?.requests || 0);
    if (classify(row?.dimensions?.actionType) === 'A') classA += n;
    else classB += n;
  }
  let storage = 0;
  for (const row of account.r2StorageAdaptiveGroups || []) {
    storage = Math.max(storage, Number(row?.max?.payloadSize || 0) + Number(row?.max?.metadataSize || 0));
  }
  const ratios = {
    storage: storage / FREE_STORAGE,
    classA: classA / FREE_CLASS_A,
    classB: classB / FREE_CLASS_B,
  };
  const pct = Math.max(...Object.values(ratios)) * 100;
  const state = pct >= STOP * 100 ? 'exhausted' : (pct >= WARN * 100 ? 'warn' : 'ok');
  const status = {
    state,
    pct: Math.round(pct * 100) / 100,
    storageBytes: storage,
    classA,
    classB,
    limits: { storage: FREE_STORAGE, classA: FREE_CLASS_A, classB: FREE_CLASS_B },
    updatedAt: now.toISOString(),
    monthStart: start,
  };
  if (env.QUOTA_KV) await env.QUOTA_KV.put('status', JSON.stringify(status));
  return status;
}

export default {
  async scheduled(event, env, ctx) {
    ctx.waitUntil(updateQuota(env));
  },
  async fetch(request, env) {
    if (new URL(request.url).pathname === '/health') {
      let status = null;
      try { status = await env.QUOTA_KV.get('status', 'json'); } catch (_) {}
      return Response.json({ ok: true, state: status?.state || 'ok', status });
    }
    return new Response('not found', { status: 404 });
  },
};
