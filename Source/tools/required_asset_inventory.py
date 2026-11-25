"""Inventory already extracted files; never opens containers, keys or encryption tools."""
from pathlib import Path
import argparse,collections,hashlib,json,re,zipfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--extracted',type=Path,default=Path('C:/catherine/extracted'));p.add_argument('--romfs-source',type=Path,help='Optional merged RomFS directory, separate from the base ExeFS root');p.add_argument('--comparison-report',type=Path);args=p.parse_args();source=args.extracted.resolve();romfs_source=args.romfs_source.resolve()if args.romfs_source else source/'romfs';out=ROOT/'Build/logs/asset-input-audit';out.mkdir(exist_ok=True)
with zipfile.ZipFile(ROOT/'Build/assets.pak')as package:
 metadata=json.loads(package.read('extraction-manifest.json'))
 required_generated=[n for n in package.namelist()if n in('main.image','subsdk0.image','subsdk1.image','sdk.image','spirv/results.tsv','catherine-pak.manifest')or n.startswith(('romfs/blocks/','spirv/'))]
 package_size=(ROOT/'Build/assets.pak').stat().st_size
romfs=json.loads((ROOT/'Build/logs/romfs-file-index.json').read_text());expected={r['path'].lstrip('/'):r['size']for r in romfs}
actual={f.relative_to(romfs_source).as_posix():f.stat().st_size for f in romfs_source.rglob('*')if f.is_file()}
comparison={'missing':sorted(expected.keys()-actual.keys()),'extra':sorted(actual.keys()-expected.keys()),'size_mismatches':[{'path':k,'expected':expected[k],'actual':actual[k]}for k in expected.keys()&actual.keys()if expected[k]!=actual[k]]}
modules=[]
for name,expected_hash in metadata['base_modules_sha256'].items():
 path=source/'exefs'/name;digest=hashlib.sha256(path.read_bytes()).hexdigest()if path.exists()else None
 modules.append({'path':f'exefs/{name}','required_sha256':expected_hash,'provided_sha256':digest,'matches_required':digest==expected_hash,'provided_is_update_main':name=='main'and digest==metadata['update_main_sha256']})
excluded={'romfs/.gitkeep':'empty source-control placeholder','romfs/data/movie/crimv_viewer_error_log.txt':'video-viewer diagnostic log','exefs/rtld':'original loader; host directly maps the four supported modules','exefs/main.npdm':'original process metadata; not loaded by this port','extraction.json':'extraction-tool metadata'}
originals=sorted([m['path']for m in modules]+['romfs/'+k for k in actual if 'romfs/'+k not in excluded])
english_candidate=[k for k in originals if not k.startswith('romfs/data/sound_JP/')]
groups=collections.defaultdict(lambda:{'files':0,'bytes':0})
for k,size in actual.items():
 if 'romfs/'+k in excluded:continue
 label='/'.join(k.split('/')[:2]);groups[label]['files']+=1;groups[label]['bytes']+=size
localized=[];language_totals=collections.defaultdict(lambda:{'files':0,'cpk_payload_bytes':0})
for name in('Base','Keyfree','Puzzle','Model','Patch'):
 entries=json.loads((ROOT/f'Build/logs/{name}-cpk-index.json').read_text())
 for entry in entries:
  langs=[lang for lang in 'EFGIS'if re.search(r'(?:^|/)'+lang+r'(?=[_/])|_'+lang+r'(?=/|\.)',entry['path'])]
  if len(langs)!=1:continue
  lang=langs[0];item={'archive':f'romfs/data/{name}.cpk','path':entry['path'],'language':lang,'stored_bytes':entry['size'],'unpacked_bytes':entry['unpacked']};localized.append(item)
  language_totals[lang]['files']+=1;language_totals[lang]['cpk_payload_bytes']+=entry['size']
foreign=[e for e in localized if e['language']in'FGIS']
report={'scope':'Full-feature input contract from static loader/asset inventory; not a proven global minimum across every gameplay branch.',
 'separate_update_comparison':str(args.comparison_report.resolve())if args.comparison_report else'pending separately extracted update from user',
 'extracted_root':str(source),'romfs_source':str(romfs_source),'package_bytes':package_size,'romfs_comparison':comparison,'modules':modules,'required_original_file_count':len(originals),'original_groups':groups,
 'not_required_original_files':excluded,'english_audio_only_candidate_count':len(english_candidate),'japanese_audio_bytes':groups['data/sound_JP']['bytes'],
 'language_totals':language_totals,'other_ui_language_candidate_count':len(foreign),'other_ui_language_cpk_payload_bytes':sum(e['stored_bytes']for e in foreign),
 'limitations':['Japanese audio omission requires an English-only package/configuration; not validated by deleting files in this audit.',
 'Non-English localized members live inside retained CPK files; removing them requires CPK rebuilding.',
 'CPK payload bytes are not guaranteed ZIP installation savings.',
 'Retain all 147 USM movies; movie2 is not established as an alternative-language folder.',
 'The current executable reads a packaged ROMFS image, not this extracted directory directly.',
 'Do not delete ROMFS ZIP blocks: package validation requires every block and the directory checksum.',
 'RomFS comparison checks paths and sizes, not a full-content hash of the large files.']}
(out/'inventory.json').write_text(json.dumps(report,indent=2))
(out/'required-decrypted-files.txt').write_text(f'# Full-feature original inputs, paths relative to an extracted fileset.\n# ExeFS source: {source / "exefs"}; must match BASE module hashes in inventory.json.\n# RomFS source: {romfs_source}; merged base+update set. No encryption/key/container inputs.\n'+'\n'.join(originals)+'\n')
(out/'english-audio-only-candidate-files.txt').write_text('# Proposed English-audio-only original inputs; requires an importer/repack and English-only voice configuration.\n# This is a candidate, not a removal-tested minimum. Other UI languages remain inside the CPK files.\n'+'\n'.join(english_candidate)+'\n')
(out/'generated-package-entries.txt').write_text('# Generated runtime package entries; derived during asset preparation, not additional original files.\n'+'\n'.join(sorted(required_generated))+'\n')
(out/'nested-language-candidates.json').write_text(json.dumps(foreign,indent=2))
lines=['CATHERINE PORT: ALREADY EXTRACTED INPUT REQUIREMENTS','',
 'This checklist starts AFTER the user has supplied already extracted/decrypted files.',
 'This audit does not open containers or keys, run decryption, rebuild archives, or change installed assets.',
 '', '1. BASE-GAME EXECUTABLE MODULES',
 'Four original NSO inputs are required to recreate main.image/subsdk0.image/subsdk1.image/sdk.image.',
 'The port uses BASE executable code and BASE PLUS UPDATE game data.',
 f'ExeFS source: {source / "exefs"}',f'Merged RomFS source: {romfs_source}']
main=next(m for m in modules if m['path']=='exefs/main')
if main['provided_is_update_main']:lines.append('IMPORTANT: The supplied main matches the update main, not the required base main.')
elif main['matches_required']:lines.append('The supplied main matches the required base executable.')
else:lines.append('IMPORTANT: The supplied main does not match the required base executable.')
for m in modules:lines +=[m['path'],f"  Required SHA-256: {m['required_sha256']}",f"  Provided matches: {m['matches_required']}"]
lines+=['','2. MERGED ROMFS: RETAIN THESE ORIGINAL CONTAINERS AND TREES']
for group,stat in sorted(groups.items()):lines.append(f"romfs/{group}: {stat['files']} files; {stat['bytes']:,} bytes")
lines+=['',f'Full-feature checklist: {len(originals):,} original files including four base ExeFS modules.',
 'Both movie folders are required for the conservative full-game set: 80 + 67 USM files.',
 'Keep all five CPKs intact; they contain shared gameplay assets as well as localized members.',
 'Keep all 3,050 original shader files (1,234 .vs and 1,816 .ps).',
 '', '3. LANGUAGE SLIMMING CANDIDATES',
 f"Japanese voices: romfs/data/sound_JP/**, 310 files, {groups['data/sound_JP']['bytes']:,} bytes.",
 'English-only voice packaging could omit this tree, but the voice option must be constrained/handled.',
 f"French/German/Italian/Spanish: {len(foreign):,} named members inside Base/Keyfree/Puzzle/Model CPKs; {sum(e['stored_bytes']for e in foreign):,} bytes of stored CPK payload.",
 'Language path codes: E=English, F=French, G=German, I=Italian, S=Spanish.',
 'These are internal CPK members, not whole containers that can be omitted.',
 'Full paths are in nested-language-candidates.json. Repacking and dependency checks are required.',
 'Do not treat movie2 or alternate movie numbers as disposable language copies.',
 '', '4. ORIGINAL FILES NOT REQUIRED BY THIS PORT']
for path,reason in excluded.items():lines.append(f'{path}: {reason}')
lines+=['', '5. GENERATED RUNTIME ASSETS',
 'The executable expects assets.pak containing four module .image files,',
 '1,855 translated SPIR-V shaders plus spirv/results.tsv, a ROMFS block set,',
 'and catherine-pak.manifest. The original files above must be imported/converted;',
 'the current executable does not directly load an extracted RomFS directory.',
 'Keep every ROMFS block required by the manifest. Arbitrary block deletion is rejected at startup.',
 'romfs/index.json and extraction-manifest.json are inventory/provenance metadata, not runtime loader requirements.',
 '', '6. VERIFICATION AND LIMITS',
 f"Extracted RomFS path/size check: missing={len(comparison['missing'])}, extra={len(comparison['extra'])}, size mismatches={len(comparison['size_mismatches'])}.",
 'No asset archive, game source behavior, save or settings were changed.',
 'This is a defensible full-feature file contract, not a proven global mathematical minimum.',
 f'Completed separate base/update comparison: {args.comparison_report.resolve()}'if args.comparison_report else'A separate extracted update comparison is pending; exact update-changed assets are not yet identified.',
 'No file-deletion gameplay validation was performed.','']
(out/'required-files-report.txt').write_text('\n'.join(lines))
print(json.dumps({'report':str(out/'required-files-report.txt'),'original_files':len(originals),'english_audio_candidate_files':len(english_candidate),'foreign_cpk_members':len(foreign),'foreign_cpk_payload_bytes':sum(e['stored_bytes']for e in foreign),'base_main_present':modules[0]['matches_required'],'romfs_comparison':comparison},indent=2))
