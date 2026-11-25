"""Exercise the shipped installer against small synthetic files; never user game files."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,struct,tempfile
ROOT=Path(__file__).resolve().parents[2]
EXE=ROOT/'Build/asset-setup/publish/CatherineAssetSetup.exe'
def main():
 out=ROOT/'Build/asset-setup/tests';out.mkdir(exist_ok=True)
 with tempfile.TemporaryDirectory(dir=out) as tmp:
  root=Path(tmp);assets=root/'Assets';assets.mkdir();updates=root/'update-source';updates.mkdir()
  files=[]
  for i,name in enumerate(['game.bin','update1.bin','update2.bin']):
   data=(name*4).encode();relative='romfs/'+name;files.append(dict(path=relative,size=len(data),sha256=hashlib.sha256(data).hexdigest(),kind='game' if i==0 else 'update',romfsOffset=i*100));(assets/name if i==0 else updates/name).write_bytes(data)
  manifest=dict(version=1,files=files,modules=[],shaders=[],romfsSize=300,romfsMetadata=[],packageSha256=[],canonicalPackageSha256='')
  path=root/'assets-required.json';path.write_text(json.dumps(manifest))
  def run(*args):return subprocess.run([str(EXE),'--root',str(root),'--headless',*args],capture_output=True).returncode
  assert run('--scan-only')==1
  missing=(root/'assets-missing.txt').read_text();assert 'Update files are required.' in missing and 'update1.bin' in missing and 'game.bin' not in missing
  assert (assets/'game.bin').exists() and not (root/'Pending').exists()
  # Wrong contents must not be staged or moved.
  (assets/'update1.bin').write_bytes(b'x'*files[1]['size']);assert run('--scan-only')==1;assert (assets/'update1.bin').exists()
  originals={p:(p.read_bytes(),p.stat().st_mtime_ns)for folder in [assets,updates]for p in folder.rglob('*')if p.is_file()}
  assert run('--scan',str(updates))==0
  assert all(p.exists() and p.read_bytes()==data and p.stat().st_mtime_ns==mtime for p,(data,mtime)in originals.items()),'source files changed'
  assert not (root/'Pending').exists(),'must not stage originals'
  pack=root/'assets.pak';first=hashlib.sha256(pack.read_bytes()).hexdigest()
  with zipfile.ZipFile(pack) as z:
   assert z.testzip() is None
   rom=z.read('romfs/blocks/000000.bin');assert len(rom)==300
   for i,f in enumerate(files):assert rom[i*100:i*100+f['size']]==(assets/'game.bin'if i==0 else updates/Path(f['path']).name).read_bytes()
   records=[]
   for e in sorted(z.infolist(),key=lambda e:e.filename):
    if e.filename=='catherine-pak.manifest':continue
    n=e.filename.encode();records.append(struct.pack('<H',len(n))+n+struct.pack('<QIH',e.file_size,e.CRC,e.compress_type))
   import zlib
   assert f'DIRECTORY_CRC32 {zlib.crc32(b"".join(records)):08x}' in z.read('catherine-pak.manifest').decode()
  pack.unlink();assert run()==0;assert hashlib.sha256(pack.read_bytes()).hexdigest()==first,'not deterministic'
  assert all(p.exists() and p.read_bytes()==data and p.stat().st_mtime_ns==mtime for p,(data,mtime)in originals.items()),'source files changed after resumed build'
  manifest['canonicalPackageSha256']=first;path.write_text(json.dumps(manifest));(root/'assets-setup.log').unlink();assert run()==0;assert 'may be corrupt' not in (root/'assets-setup.log').read_text()
  (root/'assets-setup.log').unlink();assert run()==0;assert 'cached package verification' in(root/'assets-setup.log').read_text()
  # Same length and restored modification time must still invalidate the cache.
  import os
  valid=pack.read_bytes();stamp=pack.stat();changed=bytearray(valid);changed[10]^=1;pack.write_bytes(changed);os.utime(pack,ns=(stamp.st_atime_ns,stamp.st_mtime_ns));(root/'assets-setup.log').unlink();assert run()==0;log=(root/'assets-setup.log').read_text();assert 'may be corrupt'in log and 'cached package verification'not in log
  pack.write_bytes(valid);assert run()==0
  with pack.open('ab') as f:f.write(b'damage')
  assert run()==0,'hash mismatch must still permit launch';assert 'may be corrupt' in (root/'assets-setup.log').read_text()
  path.unlink();assert run()==0,'existing pack without manifest should warn and continue'
 (out/'result.json').write_text(json.dumps(dict(passed=True,checks=['update-only prompt','remembered source folders','all source bytes and timestamps preserved','no staging folder created','invalid source preserved','RomFS layout','ZIP CRC','directory CRC','deterministic package','matching hash','cached repeat verification','same-size restored-mtime change invalidates cache','mismatch continues','missing manifest warning']),indent=2))
 print('Asset installer checks passed')
if __name__=='__main__':main()
