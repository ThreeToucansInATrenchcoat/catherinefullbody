"""Developer tool: record hashes and layout recipes, never original game payloads."""
from pathlib import Path
import json,hashlib,struct,base64,sys
sys.path.insert(0,str(Path(__file__).parent))
import game_assets
ROOT=Path(__file__).resolve().parents[2]
AUDIT=ROOT/'Build/logs/asset-input-audit'
def main():
 hashes={x['path']:x for x in json.loads((AUDIT/'extract-vs-update-20261124/all-file-hashes.json').read_text())}
 required=[x for x in (AUDIT/'required-decrypted-files.txt').read_text().splitlines() if x and not x.startswith('#')]
 offsets={'romfs/'+x['path']:x['offset'] for x in game_assets.f.index}
 files=[]
 for path in required:
  row=hashes[path];side='base' if path.startswith('exefs/') else 'updated'
  files.append(dict(path=path,size=row[side+'_bytes'],sha256=row[side+'_sha256'],kind='update' if path in ['romfs/data/Base.cpk','romfs/data/movie2/mov429.usm'] else 'game',romfsOffset=offsets.get(path,-1)))
 modules=[]
 for name,addr,size in [('main',0x7000000000,63758336),('subsdk0',0x7003e00000,9715712),('subsdk1',0x7004800000,7426048),('sdk',0x7005000000,13611008)]:
  data=game_assets.f.zip.read(name+'.image');assert len(data)==size
  modules.append(dict(name=name,base=addr,size=size,imageSha256=hashlib.sha256(data).hexdigest()))
 metadata=[]
 for offset,size in [(0,80),(14940185280,322596)]:
  game_assets.f.seek(offset);data=game_assets.f.read(size);metadata.append(dict(offset=offset,data=base64.b64encode(data).decode()))
 shaders={}
 def control(b):return {i:struct.unpack_from('<I',b,i)[0] for i in range(0x6fc,min(len(b),0x800),4)}
 for p in sorted(Path('C:/catherine/WithUpdate/romfs/data/shaders_binary').iterdir()):
  b=p.read_bytes();ctrl=b.find(bytes.fromhex('34127698'));code=b.find(bytes.fromhex('78563412'),ctrl+0x7f8);size=struct.unpack_from('<I',b,ctrl+0x704)[0];h=hashlib.sha1(b[code:code+size]).hexdigest()[:16]
  shaders.setdefault(h,dict(hash=h,kind='raw',path='romfs/data/shaders_binary/'+p.name,offset=code,length=size,control=control(b[ctrl:code])))
 embedded=json.loads((AUDIT/'shader-source-locations.json').read_text())['embedded']
 for h,row in embedded.items():shaders[h]=dict(hash=h,kind='module',path=row['module'],offset=row['offset'],length=row['size'],control={})
 for h,row in shaders.items():
  for directory in [ROOT/'Old/shader-tools/archive/input',ROOT/'Old/base/shader_misses']:
   p=directory/('ctrl_'+h+'.bin')
   if p.exists():row['control']=control(p.read_bytes());break
  if not row['control']:raise ValueError('No control metadata for '+h)
 target=ROOT/'Build/assets-required.json'
 previous=json.loads(target.read_text()) if target.exists() else {}
 result=dict(version=1,files=files,modules=modules,romfsSize=14940507876,romfsMetadata=metadata,shaders=sorted(shaders.values(),key=lambda x:x['hash']),packageSha256=previous.get('packageSha256',[]),canonicalPackageSha256=previous.get('canonicalPackageSha256',''))
 target.write_text(json.dumps(result,indent=1)+'\n',encoding='utf-8')
 (ROOT/'Source/resources/assets-required.json').write_bytes(target.read_bytes())
 print('files',len(files),'shaders',len(shaders),'metadata',sum(len(base64.b64decode(x['data']))for x in metadata))
if __name__=='__main__':main()
