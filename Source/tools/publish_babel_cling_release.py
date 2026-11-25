"""Publish the PGO candidate only after all 13 hanging-hop tests complete."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';OLD=ROOT/'Old/Releases'
p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);args=p.parse_args()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
validation=json.loads((args.run/'cling-validation.json').read_text());candidate=BUILD/'catherine1.exe';digest=sha(candidate)
assert validation['complete'] and validation['exe_sha256']==digest and len(validation['cases'])==26
assert all(not c['large_opposite_jumps']and not c['nonfinite_snapshots']for c in validation['cases'])
extra=json.loads((args.run/'blue-final-240/validation.json').read_text());assert extra['fps']==240 and extra['native_hanging_samples']>100 and not extra['large_opposite_jumps']
assert subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process -Name catherine,catherine1,catherine2 -ErrorAction SilentlyContinue).Count"],capture_output=True,text=True).stdout.strip()=='0'
save_hash='0add211dbb4e28bb79311bdc0c5688203292d6c648df23dab076abcba543798c'
settings_hash='4b03ef45fa2f4e4df92a80446a8b6fb97f44073303d060fe5c34928a499e55e8'
assert sha(BUILD/'save.dat')==save_hash and sha(BUILD/'catherine-settings.ini')==settings_hash
previous=sha(BUILD/'Catherine.exe');assert previous=='c86a8c2d2bb3eff10cdaacf6ba1e51e3a83487ae9eb421c167ec15801355b976'
regression=json.loads((BUILD/'logs/babel-regression.json').read_text());samples=regression['samples']
assert len(samples)==4 and all(r['sha256']==digest if r['label']=='babel'else r['sha256']==previous for r in samples)
assert regression['save_restored']and regression['settings_restored']
prefix='Catherine-before-Babel-hanging-fix-20261123';OLD.mkdir(exist_ok=True)
for name,ext in [('Catherine.exe','exe'),('Catherine.map','map'),('Catherine.pdb','pdb'),('Catherine-Release.zip','zip'),('BUILD-INFO.txt','txt')]:
 target=(OLD/f'{prefix}.{ext}').resolve();assert target.is_relative_to(ROOT.resolve())and not target.exists()
for name,ext in [('Catherine.exe','exe'),('Catherine.map','map'),('Catherine.pdb','pdb'),('Catherine-Release.zip','zip'),('BUILD-INFO.txt','txt')]:
 if(BUILD/name).exists():os.replace(BUILD/name,OLD/f'{prefix}.{ext}')
for source,target in [('catherine1.exe','Catherine.exe'),('catherine1.map','Catherine.map'),('catherine1.pdb','Catherine.pdb')]:os.replace(BUILD/source,BUILD/target)
lines=['Catherine Full Body - Babel hanging-hop correction - 23 November 2026',
 'PGO + ThinLTO release (python build.py --pgo).',f'SHA256 {digest}','',
 'Fix: sample the composed native character-node worlds before inverse binds.',
 'A hanging hop rebases the model grid anchor by one block while root motion',
 'cancels that step. Independently smoothing that anchor caused visible backward',
 'jumps. Already continuous native channels now retain their continuous motion;',
 'held 30 Hz node worlds continue to interpolate. Rigid/skin copies share samples.',
 '',
 'All 13 Babel choices tested genuinely hanging, with repeated left/right hops',
 'and direction reversals at both 30 and 60 FPS (26 cases): Blue Cap, Red Cap,',
 'Vincent casual, Vincent nightwear, Katherine, Catherine, Rin, Orlando, Jonny,',
 'Toby, Erica, Boss and Joker. Native hanging state and lateral travel asserted.',
 'Captured native/render pose observations show no large opposite hop jumps.',
 'Blue Cap also passed a separate 240 FPS hanging-hop check.',
 'CPU staging-buffer observations exclude reads during asynchronous population;',
 'rejection counts are retained in the validation. These are not GPU captures.',
 'Unit regression reproduces grid-anchor rebasing alongside native-fast motion.',
 '',
 'Short matched hidden uncapped performance checks (foreground load varies):']
for scene in ('boss','bar'):
 old=next(r for r in samples if r['scene']==scene and r['label']=='previous');new=next(r for r in samples if r['scene']==scene and r['label']=='babel')
 lines.append(f"{scene}: previous {old['fps']:.1f}, corrected {new['fps']:.1f} rendered FPS.")
lines+=['These short runs are not guarantees of normal-launch FPS. Boss used Safety.',
 '', 'Tests ran hidden and muted, without desktop mouse/focus control.',
 'The current save and settings were restored byte-for-byte after all tests.',
 'Prior boss, statue, chain and eye fixes are retained. Graphics migration',
 'remains parked. Assets and the independent DirectX 12 plugin are untouched.',
 f'Evidence: {args.run.resolve()}', '          Build/logs/babel-regression.json','']
(BUILD/'BUILD-INFO.txt').write_text('\n'.join(lines))
entries=['Catherine.exe','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','BUILD-INFO.txt']
temporary=BUILD/'Catherine-Release.zip.tmp'
with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6)as archive:
 for name in entries:archive.write(BUILD/name,name)
os.replace(temporary,BUILD/'Catherine-Release.zip')
with zipfile.ZipFile(BUILD/'Catherine-Release.zip')as archive:assert hashlib.sha256(archive.read('Catherine.exe')).hexdigest()==digest
for name in ('obj/babel_node_test.exe','obj/babel_bridge_test.exe'):
 target=(BUILD/name).resolve();assert target.is_relative_to(BUILD.resolve())
 if target.exists():target.unlink()
result={'exe_sha256':digest,'zip_exe_matches':True,'save_sha256':sha(BUILD/'save.dat'),'settings_sha256':sha(BUILD/'catherine-settings.ini'),'hanging_characters':13,'hanging_cases':26}
(BUILD/'logs/babel-cling-release-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
