"""Read original modules and shaders in place; compare outputs and prove inputs untouched."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2];B=ROOT/'Build'
def main():
 root=B/'asset-setup/read-in-place-real';root.mkdir(exist_ok=True);m=json.loads((B/'assets-required.json').read_text());needed={'exefs/'+x['name']for x in m['modules']}|{s['path']for s in m['shaders']if s['kind']=='raw'}
 m['files']=[f for f in m['files']if f['path']in needed]
 originals={}
 offset=0
 for f in m['files']:
  if f['path'].startswith('romfs/'):f['romfsOffset']=offset;offset+=f['size']
  p=Path('C:/catherine/go')/f['path'];originals[p]=(hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mtime_ns)
 m.update(romfsSize=offset,romfsMetadata=[],packageSha256=[],canonicalPackageSha256='');(root/'assets-required.json').write_text(json.dumps(m))
 pack=root/'assets.pak'
 if pack.exists():pack.unlink()
 p=subprocess.run([str(B/'asset-setup/publish/CatherineAssetSetup.exe'),'--root',str(root),'--headless','--scan','C:/catherine/go/exefs','--scan','C:/catherine/go/romfs/data/shaders_binary']);assert p.returncode==0,p.returncode
 z=zipfile.ZipFile(pack);reference=zipfile.ZipFile(B/'assets.pak');names=[x['name']+'.image'for x in m['modules']]+['spirv/'+s['hash']+'.spv'for s in m['shaders']];bad=[n for n in names if z.read(n)!=reference.read(n)];assert not bad,bad
 assert all(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==sha and p.stat().st_mtime_ns==mtime for p,(sha,mtime)in originals.items()),'Original files changed'
 assert not(root/'Pending').exists();result=dict(passed=True,original_files_unchanged=len(originals),modules_match=len(m['modules']),shaders_match=len(m['shaders']),pending_created=False);(root/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':main()
