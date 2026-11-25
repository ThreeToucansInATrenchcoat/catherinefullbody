"""Read-only content comparison of two already extracted game folders."""
from pathlib import Path
import argparse,hashlib,json,time
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--updated',type=Path,required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
base=args.base.resolve(strict=True);updated=args.updated.resolve(strict=True);out=args.out.resolve();assert base!=updated and not out.is_relative_to(base)and not out.is_relative_to(updated);out.mkdir(parents=True,exist_ok=True)
def inventory(root):return {p.relative_to(root).as_posix():(p,p.stat())for p in root.rglob('*')if p.is_file()}
A=inventory(base);B=inventory(updated);start=time.monotonic();processed=0;rows=[]
def digest(path,before):
 h=hashlib.sha256()
 with path.open('rb')as f:
  while chunk:=f.read(4*1024*1024):h.update(chunk)
 after=path.stat();assert (before.st_size,before.st_mtime_ns)==(after.st_size,after.st_mtime_ns),f'File changed during comparison: {path}'
 return h.hexdigest()
for name in sorted(A.keys()|B.keys()):
 a=A.get(name);b=B.get(name);ha=digest(*a)if a else None;hb=digest(*b)if b else None
 processed+=(a[1].st_size if a else 0)+(b[1].st_size if b else 0)
 status='added'if not a else'removed'if not b else'identical'if ha==hb else'changed'
 rows.append({'path':name,'status':status,'base_bytes':a[1].st_size if a else None,'updated_bytes':b[1].st_size if b else None,'base_sha256':ha,'updated_sha256':hb})
 if(a and a[1].st_size>=100*1024*1024)or(b and b[1].st_size>=100*1024*1024):
  print(json.dumps({'file':name,'status':status,'read_GB':round(processed/1e9,2),'seconds':round(time.monotonic()-start,1)}),flush=True)
changes=[r for r in rows if r['status']!='identical'];result={'base':str(base),'updated':str(updated),'method':'Full SHA-256 of every file in both trees; metadata files included. No source mutation.',
 'base_files':len(A),'updated_files':len(B),'base_bytes':sum(x[1].st_size for x in A.values()),'updated_bytes':sum(x[1].st_size for x in B.values()),
 'identical_files':sum(r['status']=='identical'for r in rows),'changed_files':changes,'elapsed_seconds':time.monotonic()-start}
(out/'comparison.json').write_text(json.dumps(result,indent=2));(out/'all-file-hashes.json').write_text(json.dumps(rows,indent=2))
lines=['CATHERINE: EXTRACTED BASE / UPDATED FILE COMPARISON','',f'Base: {base}',f'Updated: {updated}',
 f'Files: {len(A):,} / {len(B):,}',f"Identical contents: {result['identical_files']:,}",
 'Method: SHA-256 read over every file in both trees, including equal-size files.','', 'DIFFERENCES:']
for r in changes:lines +=[r['path'],f"  {r['status']}: {r['base_bytes']} -> {r['updated_bytes']} bytes",f"  Base SHA-256: {r['base_sha256']}",f"  Updated SHA-256: {r['updated_sha256']}"]
lines+=['','No files in either input directory were modified.','']
(out/'comparison.txt').write_text('\n'.join(lines));print(json.dumps(result,indent=2),flush=True)
