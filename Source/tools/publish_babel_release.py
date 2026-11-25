"""Publish the validated candidate without rewriting hard-linked old releases."""
from pathlib import Path
import hashlib,json,os,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';OLD=ROOT/'Old/Releases'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
validation=BUILD/'perf/runs/20261123-124022-babel-pair-validation/validated-results.json'
assert json.loads(validation.read_text())['complete']
regression=json.loads((BUILD/'logs/babel-regression.json').read_text())
samples=regression['samples'];assert len(samples)==4
candidate=BUILD/'catherine1.exe';digest=sha(candidate)
assert all(x['sha256']==digest for x in samples if x['label']=='babel')
assert subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process -Name catherine,catherine1,catherine2 -ErrorAction SilentlyContinue).Count"],capture_output=True,text=True).stdout.strip()=='0'
known=json.loads((BUILD/'logs/handoff-build-state-20261122.json').read_text())
settings_before='4b03ef45fa2f4e4df92a80446a8b6fb97f44073303d060fe5c34928a499e55e8'
save_after_exploration='0add211dbb4e28bb79311bdc0c5688203292d6c648df23dab076abcba543798c'
original=BUILD/'perf/backup/save.dat';save=BUILD/'save.dat';settings=BUILD/'catherine-settings.ini'
assert sha(original)==known['save_sha256'] and sha(settings)==settings_before and sha(save)==save_after_exploration
assert original.read_bytes()[:0xc8000]==save.read_bytes()[:0xc8000], 'save-slot bytes differ; do not replace'
# The older backup proves slot preservation, but its system flags predate this
# session. Retain current system data rather than replacing it with older data.
OLD.mkdir(exist_ok=True)
prefix='Catherine-before-Babel-interpolation-20261123'
archives=[('Catherine.exe','exe'),('Catherine.map','map'),('Catherine.pdb','pdb'),('Catherine-Release.zip','zip'),('BUILD-INFO.txt','txt')]
for source,extension in archives:
 target=(OLD/f'{prefix}.{extension}').resolve();assert target.is_relative_to(ROOT.resolve()) and not target.exists(),target
for source,extension in archives:
 path=BUILD/source
 if path.exists():os.replace(path,OLD/f'{prefix}.{extension}')
for source,target in [('catherine1.exe','Catherine.exe'),('catherine1.map','Catherine.map'),('catherine1.pdb','Catherine.pdb')]:os.replace(BUILD/source,BUILD/target)
lines=['Catherine Full Body - Babel interpolation - 23 November 2026',
 'PGO + ThinLTO release (python build.py --pgo).',f'SHA256 {digest}','',
 'All 13 Babel choices: Blue Cap, Red Cap, Vincent casual, Vincent nightwear,',
 'Katherine, Catherine, Rin, Orlando, Jonny, Toby, Erica, Boss and Joker.',
 'Native player-owned joint transforms are smoothed before inverse binds. Rigid',
 'attachments use those same joint samples. Authoritative movement and poses',
 'remain unchanged. Faster native channels retain their existing updates.',
 'Late 30 Hz publications are timed from their actual source changes.',
 'Exact native copy/upload provenance prevents a second rigid interpolation.',
 '',
 'Validation: 13 single-player entries at 60 FPS; all 13 in BOTH player slots',
 'at 30 and 60 FPS, including duplicate-character pairs. All 26 pair/rate cases',
 'passed; complete captured skin palettes contained no nonfinite bone values.',
 '30 FPS made zero calls through the new smoothing path. Unit checks cover',
 'late samples at 240 Hz, existing faster channels, owner isolation, inverse-bind',
 'coherence through turns and GPU upload overwrite rejection.',
 'The final outside-scope atomic fast path also passes the owner/upload fixture.',
 '',
 'Short matched hidden uncapped regression checks (8 seconds, foreground load):']
for scene in ('boss','bar'):
 old=next(x for x in samples if x['scene']==scene and x['label']=='previous');new=next(x for x in samples if x['scene']==scene and x['label']=='babel')
 lines.append(f"{scene}: previous {old['fps']:.1f}, Babel release {new['fps']:.1f} rendered FPS.")
lines+=['Boss checks used Safety difficulty. These are short comparisons, not',
 'guarantees of normal-launch FPS.', '',
 'Previous statue, rotating-chain and indexed-quad eye fixes are retained.',
 'All game tests were owned, hidden and muted; no desktop mouse or focus used.',
 'Original settings restored. Saved-game slot bytes match the pre-existing backup.',
 'The first exploratory Babel entry triggered a system autosave; later tests',
 'restored their startup save snapshots. The older backup remains intact.',
 'Assets and the independent DirectX 12 plugin were not modified.',
 f'Evidence: {validation.relative_to(ROOT)}',
 '          Build/logs/babel-regression.json',
 '          Build/logs/babel-pair-validation-node-report.json','']
(BUILD/'BUILD-INFO.txt').write_text('\n'.join(lines))
entries=['Catherine.exe','CatherineInterpolation.dll','open_svpflow.dll','svpflow1_vs.dll','svpflow2_vs.dll','dev-checkpoints.pak','BUILD-INFO.txt']
temporary=BUILD/'Catherine-Release.zip.tmp'
with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6)as archive:
 for name in entries:archive.write(BUILD/name,name)
os.replace(temporary,BUILD/'Catherine-Release.zip')
with zipfile.ZipFile(BUILD/'Catherine-Release.zip')as archive:
 assert hashlib.sha256(archive.read('Catherine.exe')).hexdigest()==digest
 assert archive.read('BUILD-INFO.txt')==(BUILD/'BUILD-INFO.txt').read_bytes()
for name in ['obj/babel_node_test.exe','obj/babel_bridge_test.exe']:
 path=(BUILD/name).resolve();assert path.is_relative_to(BUILD.resolve())
 if path.exists():path.unlink()
result={'exe_sha256':sha(BUILD/'Catherine.exe'),'zip_exe_matches':True,'save_sha256':sha(save),'settings_sha256':sha(settings),'save_slots_match_original_backup':original.read_bytes()[:0xc8000]==save.read_bytes()[:0xc8000],'exploratory_system_autosave':True,'original_settings_restored':sha(settings)==settings_before,'roster_complete':True}
(BUILD/'logs/babel-release-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
