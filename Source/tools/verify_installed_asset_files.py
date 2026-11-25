"""Compare every required RomFS original against the generated package's virtual filesystem."""
from pathlib import Path
import argparse,functools,hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('package');a=p.parse_args();manifest=json.loads((ROOT/'Build/assets-required.json').read_text());z=zipfile.ZipFile(a.package)
 @functools.lru_cache(maxsize=8)
 def block(i):return z.read(f'romfs/blocks/{i:06d}.bin')
 checked=0;bad=[]
 for f in sorted((f for f in manifest['files']if f['romfsOffset']>=0),key=lambda f:f['romfsOffset']):
  start=f['romfsOffset'];left=f['size'];h=hashlib.sha256()
  while left:
   data=block(start//262144);offset=start%262144;n=min(left,len(data)-offset);assert n>0;h.update(data[offset:offset+n]);start+=n;left-=n
  if h.hexdigest()!=f['sha256']:bad.append(f['path'])
  checked+=1
 result=dict(checked=checked,mismatches=bad,passed=not bad);(ROOT/'Build/asset-setup/all-original-files-check.json').write_text(json.dumps(result,indent=2));print(json.dumps(result));assert not bad
if __name__=='__main__':main()
