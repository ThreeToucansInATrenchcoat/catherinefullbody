"""Package the active release, including all supplied PC UI assets, never game data."""
from pathlib import Path
import hashlib,json,time,zipfile
ROOT=Path(__file__).resolve().parents[2];B=ROOT/'Build'
FILES=['Catherine.exe','Catherine DEV.cmd','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','BUILD-INFO.txt','assets-required.json','INSTALL-ASSETS.txt','ASSET-TOOLS-LICENSES.txt','CUTSCENES.txt','cutscene-catalog.json']
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
 names=FILES+(['native-verification.json'] if (B/'native-verification.json').is_file() else [])
 files=[B/n for n in names]+sorted(p for p in(B/'CathAssets').rglob('*')if p.is_file());temporary=B/'Catherine-Release.zip.building'
 with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6)as z:
  for p in files:z.write(p,p.relative_to(B).as_posix())
 with zipfile.ZipFile(temporary)as z:
  assert z.testzip()is None
  assert sha(z.read('Catherine.exe'))==sha((B/'Catherine.exe').read_bytes())
  for p in (ROOT/'keysource').rglob('*'):
   if p.is_file():assert z.read('CathAssets/Prompts/'+p.relative_to(ROOT/'keysource').as_posix())==p.read_bytes()
  for i,n in enumerate(['1 (2).png','2 (2).png','3.png','4.png'],1):assert z.read(f'CathAssets/Tutorials/PS5/{i}.png')==(Path('C:/Users/scott/Desktop/Old')/n).read_bytes()
  artwork=json.loads((B/'CathAssets/artwork-manifest.json').read_text())
  for record in artwork['tutorials']:
   source=ROOT/record['source_path'] if 'source_path'in record else Path('C:/Users/scott/Desktop')/('Old'if record['path'].startswith('Tutorials/PS5/')else'ps4-and-keyboard-mouse-tutorial-images')/record['source']
   assert sha(z.read('CathAssets/'+record['path']))==record['sha256'] and z.read('CathAssets/'+record['path'])==source.read_bytes()
  for family in ['KeyboardMouse','Xbox','PS5','Switch','PS4']:assert len(z.read(f'CathAssets/Tutorials/{family}/tutorial.bc3'))==4*1024*1024
  assert 'assets.pak'not in z.namelist()and 'save.dat'not in z.namelist()and 'catherine-settings.ini'not in z.namelist()
 # Windows antivirus may briefly hold the newly completed archive open.
 for attempt in range(10):
  try:temporary.replace(B/'Catherine-Release.zip');break
  except PermissionError:
   if attempt==9:raise
   time.sleep(.5)
 report=dict(passed=True,exe_sha256=sha((B/'Catherine.exe').read_bytes()),entries=len(files),cath_assets_files=len(files)-len(FILES),tutorial_originals_exact=20,prompt_originals_exact=sum(p.is_file()for p in(ROOT/'keysource').rglob('*')))
 (B/'asset-setup/release-validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
if __name__=='__main__':main()
