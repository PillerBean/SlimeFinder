#!/usr/bin/env python3
"""Upload the prepared 8192 R2 cache using S3 SigV4 credentials from ~/.slimefinder/r2.env."""
import argparse, base64, concurrent.futures, datetime, hashlib, hmac, os, pathlib, sys, time, urllib.error, urllib.parse, urllib.request, xml.etree.ElementTree as ET

def load_env():
    p = pathlib.Path.home() / '.slimefinder' / 'r2.env'
    vals = {}
    for line in p.read_text(encoding='utf-8', errors='replace').splitlines():
        line=line.strip()
        if not line or line.startswith('#') or '=' not in line: continue
        k,v=line.split('=',1); vals[k.strip()]=v.strip()
    need=['R2_ACCOUNT_ID','R2_BUCKET','R2_ACCESS_KEY_ID','R2_SECRET_ACCESS_KEY']
    for k in need:
        if not vals.get(k): raise SystemExit('missing '+k)
    return vals

def sign(method, host, canonical_uri, canonical_query, payload, access, secret):
    now=datetime.datetime.now(datetime.timezone.utc)
    amz=now.strftime('%Y%m%dT%H%M%SZ'); day=now.strftime('%Y%m%d')
    payload_hash=hashlib.sha256(payload).hexdigest()
    canonical_headers=f'host:{host}\nx-amz-content-sha256:{payload_hash}\nx-amz-date:{amz}\n'
    signed='host;x-amz-content-sha256;x-amz-date'
    creq=f'{method}\n{canonical_uri}\n{canonical_query}\n{canonical_headers}\n{signed}\n{payload_hash}'
    scope=f'{day}/auto/s3/aws4_request'
    sts=f'AWS4-HMAC-SHA256\n{amz}\n{scope}\n{hashlib.sha256(creq.encode()).hexdigest()}'
    def sg(k,m): return hmac.new(k,m.encode(),hashlib.sha256).digest()
    k=sign_key=('AWS4'+secret).encode(); k=sg(k,day); k=sg(k,'auto'); k=sg(k,'s3'); k=sg(k,'aws4_request')
    sig=hmac.new(k,sts.encode(),hashlib.sha256).hexdigest()
    return {'Authorization':f'AWS4-HMAC-SHA256 Credential={access}/{scope}, SignedHeaders={signed}, Signature={sig}',
            'x-amz-content-sha256':payload_hash,'x-amz-date':amz,'Host':host}

def request(env, method, key='', query='', payload=b''):
    host=f"{env['R2_ACCOUNT_ID']}.r2.cloudflarestorage.com"
    canonical_uri='/'+env['R2_BUCKET']+('/'+urllib.parse.quote(key,safe='-_.~/') if key else '')
    url=f'https://{host}{canonical_uri}'+(('?'+query) if query else '')
    hdr=sign(method,host,canonical_uri,query,payload,env['R2_ACCESS_KEY_ID'],env['R2_SECRET_ACCESS_KEY'])
    req=urllib.request.Request(url,data=payload,method=method,headers=hdr)
    return urllib.request.urlopen(req,timeout=120)

def list_keys(env,prefix):
    keys=[]; start_after=''
    while True:
        q='list-type=2&prefix='+urllib.parse.quote(prefix,safe='')
        if start_after:
            q+='&start-after='+urllib.parse.quote(start_after,safe='')
        with request(env,'GET','',q,b'') as r: data=r.read()
        root=ET.fromstring(data); ns={'s3':'http://s3.amazonaws.com/doc/2006-03-01/'}
        page=[]
        for c in root.findall('s3:Contents',ns):
            k=c.findtext('s3:Key',default='',namespaces=ns)
            if k: page.append(k)
        keys.extend(page)
        trunc=root.findtext('s3:IsTruncated',default='false',namespaces=ns).lower()=='true'
        if not trunc or not page: break
        start_after=page[-1]
    return keys

def delete_key(env,key):
    try:
        with request(env,'DELETE',key): pass
        return key,200
    except Exception as e:
        return key,getattr(e,'code',None)

def put_file(env,key,path):
    data=pathlib.Path(path).read_bytes()
    last=None
    for attempt in range(4):
        try:
            with request(env,'PUT',key,payload=data) as r: return key,r.status
        except Exception as e:
            last=e
            try:
                body=e.read(300).decode('utf-8','replace') if hasattr(e,'read') else ''
            except Exception: body=''
            print('PUTERR',key,type(e).__name__,repr(e),getattr(e,'reason',None),body,flush=True)
            if attempt<3: time.sleep(0.5*(attempt+1))
    return key,getattr(last,'code',None)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--root',default='dist/netdisk-8192/all')
    ap.add_argument('--manifest',default='dist/r2-8192/manifest.json')
    ap.add_argument('--prefix',default='v3.12.3/')
    ap.add_argument('--workers',type=int,default=4)
    ap.add_argument('--delete-old',action='store_true')
    ap.add_argument('--skip-existing',action='store_true')
    ap.add_argument('--limit',type=int,default=0)
    args=ap.parse_args(); env=load_env(); root=pathlib.Path(args.root)
    files=sorted((root/'local').glob('*.slmz'))
    print('packs',len(files),'delete_old',args.delete_old,flush=True)
    if args.delete_old:
        keys=list_keys(env,args.prefix); print('old_objects',len(keys),flush=True)
        errs=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
            for key,st in ex.map(lambda k: delete_key(env,k),keys):
                if st not in (200,204,404): errs.append((key,st))
        print('deleted errors',len(errs),errs[:3],flush=True)
    if args.skip_existing:
        existing=set(list_keys(env,args.prefix+'local/'))
        files=[p for p in files if args.prefix+'local/'+p.name not in existing]
        print('missing_packs',len(files),flush=True)
    limited = args.limit > 0 and len(files) > args.limit
    if limited:
        files = files[:args.limit]
        print('limited_to',len(files),'manifest_deferred',flush=True)
    jobs=[(args.prefix+'local/'+p.name,str(p)) for p in files]
    done=0; errs=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        for key,st in ex.map(lambda j: put_file(env,j[0],j[1]),jobs):
            done+=1
            if st not in (200,201): errs.append((key,st))
            if done%32==0 or done==len(jobs): print('uploaded',done,'/',len(jobs),'errors',len(errs),flush=True)
    print('packs_done',done,'errors',errs[:5],'error_count',len(errs),flush=True)
    if errs or limited:
        print('MANIFEST NOT UPLOADED: pack errors or limited batch remains',flush=True)
        return 1
    key,st=put_file(env,args.prefix+'manifest.json',args.manifest)
    print('manifest_upload',key,st,flush=True)
    return 0 if st in (200,201) else 1

if __name__=='__main__': sys.exit(main())
