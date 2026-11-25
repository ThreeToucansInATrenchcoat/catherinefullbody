"""Compare members of already extracted CPK asset archives, without mutation."""
from pathlib import Path
import argparse,hashlib,json
import game_assets as cpk
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--updated',type=Path,required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
def index(stream):
 cpk.f=stream;header=cpk.table(0)[0];base=min(header['TocOffset'],header['ContentOffset']);rows=cpk.table(header['TocOffset'])
 return {'/'.join(x for x in[r.get('DirName'),r.get('FileName')]if x):{'offset':base+r['FileOffset'],'stored':r['FileSize'],'unpacked':r['ExtractSize']}for r in rows}
def read(stream,entry):stream.seek(entry['offset']);value=stream.read(entry['stored']);assert len(value)==entry['stored'];return value
with args.base.open('rb')as a,args.updated.open('rb')as b:
 A=index(a);B=index(b);changed=[];same=0
 for name in sorted(A.keys()|B.keys()):
  va=read(a,A[name])if name in A else None;vb=read(b,B[name])if name in B else None
  if va==vb:same+=1;continue
  da=cpk.decompress(va)if va is not None else None;db=cpk.decompress(vb)if vb is not None else None
  changed.append({'path':name,'status':'added'if va is None else'removed'if vb is None else'encoding_only'if da==db else'changed','base':A.get(name),'updated':B.get(name),
   'base_content_sha256':hashlib.sha256(da).hexdigest()if da is not None else None,'updated_content_sha256':hashlib.sha256(db).hexdigest()if db is not None else None})
result={'archive':args.base.name,'base_members':len(A),'updated_members':len(B),'identical_stored_members':same,'different_members':changed}
args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
