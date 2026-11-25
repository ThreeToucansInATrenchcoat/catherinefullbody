"""Publish the validated ground interpolation restoration by rename."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';OLD=ROOT/'Old/Releases'
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);args=p.parse_args();run=args.run.resolve()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
validation=json.loads((run/'running-validation.json').read_text());session=json.loads((run/'session-result.json').read_text());candidate=BUILD/'catherine1.exe';digest=sha(candidate)
assert validation['complete']and validation['exe_sha256']==digest and len(validation['cases'])==18
assert all(not c['large_opposite_jumps']for c in validation['cases'])
assert session['settings_restored']and session['save_unchanged']
assert sha(BUILD/'save.dat')==sha(run/'save-before.dat')
assert subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process -Name catherine,catherine1,catherine2 -ErrorAction SilentlyContinue).Count"],capture_output=True,text=True).stdout.strip()=='0'
assert sha(BUILD/'Catherine.exe')=='5ff2542e33dec64c53b81780a5876329f45552ddda04ba4640a287546a859915'
save_sha=sha(BUILD/'save.dat');settings_sha=sha(BUILD/'catherine-settings.ini')
prefix='Catherine-before-Babel-running-restore-20261123';OLD.mkdir(exist_ok=True)
files=[('Catherine.exe','exe'),('Catherine.map','map'),('Catherine.pdb','pdb'),('Catherine-Release.zip','zip'),('BUILD-INFO.txt','txt')]
for name,ext in files:
 target=(OLD/f'{prefix}.{ext}').resolve();assert target.is_relative_to(ROOT.resolve())and not target.exists()
for name,ext in files:
 if(BUILD/name).exists():os.replace(BUILD/name,OLD/f'{prefix}.{ext}')
for source,target in [('catherine1.exe','Catherine.exe'),('catherine1.map','Catherine.map'),('catherine1.pdb','Catherine.pdb')]:os.replace(BUILD/source,BUILD/target)
lines=['Catherine Full Body - Babel ground interpolation restoration - 23 November 2026',
 'PGO + ThinLTO release (python build.py --pgo).',f'SHA256 {digest}','',
 'Ground locomotion again separates held placement updates from faster pose',
 'updates. Hanging keeps the composed-world path that prevents grid-anchor',
 'rebasing jumps. The native hanging state selects one space for all parts per',
 'frame; changing movement family resets histories from the other space.',
 '', 'Validation: Blue Cap and Red Cap, at 30/60/240 FPS, each running, hanging',
 'with left/right hops, then climbing back and running again (18 cases).',
 'Captured pose-matched translations verify actual intermediate ground',
 'placements at 240 FPS; no large opposite-direction hop jumps were observed.',
 'CPU staging reads taken during allocation/population are excluded with counts',
 'preserved in the validation. These observations are not GPU captures.',
 'Unit checks cover held placement with fast poses, hanging anchor rebases,',
 'movement-family history resets, both owners and rigid/skin upload provenance.',
 '', 'Tests ran hidden and muted without desktop input or focus changes.',
 'The current save and settings were restored byte-for-byte after testing.',
 'Assets and the independent DX12 plugin were not modified.',
 f'Evidence: {run}','']
(BUILD/'BUILD-INFO.txt').write_text('\n'.join(lines))
entries=['Catherine.exe','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','BUILD-INFO.txt']
temporary=BUILD/'Catherine-Release.zip.tmp'
with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6)as archive:
 for name in entries:archive.write(BUILD/name,name)
os.replace(temporary,BUILD/'Catherine-Release.zip')
with zipfile.ZipFile(BUILD/'Catherine-Release.zip')as archive:assert hashlib.sha256(archive.read('Catherine.exe')).hexdigest()==digest
for name in('obj/babel_node_test.exe','obj/babel_bridge_test.exe'):
 target=(BUILD/name).resolve();assert target.is_relative_to(BUILD.resolve())
 if target.exists():target.unlink()
assert sha(BUILD/'save.dat')==save_sha and sha(BUILD/'catherine-settings.ini')==settings_sha
result={'exe_sha256':digest,'zip_exe_matches':True,'save_sha256':save_sha,'settings_sha256':settings_sha,'movement_cases':18}
(BUILD/'logs/babel-running-release-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
