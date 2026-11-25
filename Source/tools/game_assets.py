"""Bounded read-only access to the existing asset package and its CPK directories."""
from pathlib import Path
import zipfile,struct,json,functools
ROOT=Path(__file__).resolve().parents[2]
class Reader:
 def __init__(self):
  self.zip=zipfile.ZipFile(ROOT/'Build/assets.pak');self.position=0
  self.index=json.loads((ROOT/'Build/logs/romfs-file-index.json').read_text())
 @functools.lru_cache(maxsize=16)
 def block(self,i):return self.zip.read(f'romfs/blocks/{i:06d}.bin')
 def seek(self,offset):self.position=offset
 def read(self,n):
  chunks=[]
  while n:
   part=self.block(self.position//262144)[self.position%262144:self.position%262144+n]
   if not part:raise EOFError()
   chunks.append(part);self.position+=len(part);n-=len(part)
  return b''.join(chunks)
f=Reader()
def table(pos):
 f.seek(pos); magic,_,size=struct.unpack('<4sIQ',f.read(16)); raw=f.read(size)
 if not raw.startswith(b'@UTF'):
  key=0x655f; out=bytearray();
  for b in raw: out.append(b^(key&255)); key=(key*0x4115)&65535
  raw=bytes(out)
 assert raw[:4]==b'@UTF',(pos,magic,raw[:8])
 _,sz,rows,strings,data,name,cols,row_width,count=struct.unpack_from('>4sIIIIIHHI',raw)
 def str_(off):
  start=8+strings+off; return raw[start:raw.index(0,start)].decode('utf-8',errors='replace')
 def parse(t,off):
  fm={0:'B',1:'b',2:'H',3:'h',4:'I',5:'i',6:'Q',7:'q',8:'f',9:'d',10:'I',11:'II'}[t]
  value=struct.unpack_from('>'+fm,raw,off); value=str_(value[0]) if t==10 else value if t==11 else value[0]
  return value,off+struct.calcsize('>'+fm)
 cs=[]; off=32
 for _ in range(cols):
  tag,name=struct.unpack_from('>BI',raw,off);off+=5; n=str_(name); tag,t=tag&0xf0,tag&15; v=None
  if tag==0x30: v,off=parse(t,off)
  cs.append((n,tag,t,v))
 out=[]
 for j in range(count):
  off=8+rows+j*row_width; d={}
  for n,tag,t,v in cs:
   if tag==0x50: v,off=parse(t,off)
   d[n]=v
  out.append(d)
 return out
def decompress(src):
 if not src.startswith(b'CRILAYLA'): return src
 size,packed=struct.unpack_from('<II',src,8); data=bytearray(src[16+packed:16+packed+256]+bytes(size)); idx=len(data)-1
 pos=16+packed-1; bits=0; n=0
 def read(k):
  nonlocal pos,bits,n
  while n<k: bits=(bits<<8)|src[pos];pos-=1;n+=8
  n-=k;v=bits>>n;bits&=(1<<n)-1;return v
 while idx>=256:
  if not read(1): data[idx]=read(8);idx-=1;continue
  offset=read(13)+3;length=3
  for k in [2,3,5,8]:
   v=read(k);length+=v
   if v<(1<<k)-1: break
  else:
   while True:
    v=read(8);length+=v
    if v<255:break
  while length:
   data[idx]=data[idx+offset];idx-=1;length-=1
 assert idx==255
 return data

def cpk_entries(name):
 archive=next(x for x in f.index if x['path']==name);meta=table(archive['offset'])[0];base=archive['offset']+min(meta['TocOffset'],meta['ContentOffset'])
 entries=table(archive['offset']+meta['TocOffset'])
 return [dict(path='/'.join(x for x in [row.get('DirName'),row.get('FileName')]if x),offset=base+row['FileOffset'],size=row['FileSize'],unpacked=row['ExtractSize'])for row in entries]
def read_entry(entry):
 f.seek(entry['offset']);data=bytes(decompress(f.read(entry['size'])));assert len(data)==entry['unpacked'];return data
