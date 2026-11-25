"""PGO variant builds from Source/build-manifest.json (normal Build/obj and catherine.exe untouched).

python pgo_build.py gen   -> Build/catherine1.exe  (instrumented, objects in Build/obj-pgo/gen)
python pgo_build.py use   -> Build/catherine2.exe  (optimized with Build/perf/pgo/merged.profdata)
"""
import concurrent.futures as cf, json, os, re, subprocess, sys, time
from pathlib import Path

R = Path('C:/catherine/recomp'); B = R / 'Build'; P = B / 'perf' / 'pgo'
M = json.loads((R / 'Source/build-manifest.json').read_text())
mode = sys.argv[1]; assert mode in ('gen', 'use', 'csgen')
jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 12
flags = sys.argv[3].split(',') if len(sys.argv) > 3 else []
lto = 'lto' in flags or mode == 'csgen'; o3 = 'o3' in flags; cs = 'cs' in flags
out_name = sys.argv[4] if len(sys.argv) > 4 else ('catherine1' if mode == 'gen' else 'catherine2')
assert out_name in ('catherine1', 'catherine2')
objdir = B / 'obj-pgo' / (mode + ('-lto' if lto else '') + ('-o3' if o3 else '') + ('-cs' if cs else '')); objdir.mkdir(parents=True, exist_ok=True)
profdata = P / ('merged_cs.profdata' if cs else 'merged.profdata')
extra = (['-fprofile-instr-generate'] if mode == 'gen' else
         ['-fprofile-instr-use=' + str(profdata).replace('\\', '/'), '-Wno-profile-instr-out-of-date',
          '-Wno-profile-instr-unprofiled', '-Wno-profile-instr-missing', '-Wno-backend-plugin'])
if mode == 'csgen': extra = extra + ['-fcs-profile-generate']
if lto: extra = extra + ['-flto=thin']
if o3: extra = extra + ['-O3']
if mode != 'gen': assert profdata.exists(), profdata
stamp = objdir / 'stamps.json'
old = json.loads(stamp.read_text()) if stamp.exists() else {}
key_extra = extra + ([str(profdata.stat().st_mtime_ns)] if mode != 'gen' else [])

def unit_job(u):
    cmd = list(u['command']); o = cmd.index('-o')
    dst = objdir / Path(u['object']).name
    cmd[o + 1] = str(dst).replace('\\', '/')
    cmd = cmd[:1] + [x for x in extra if x != '-O3'] + cmd[1:] + (['-O3'] if o3 else [])
    sig = json.dumps([cmd, key_extra])
    # recompile when the normal object is newer (its sources changed) or flags changed
    if dst.exists() and old.get(str(u['id'])) == sig and dst.stat().st_mtime_ns >= Path(u['object']).stat().st_mtime_ns:
        return u['id'], sig, 'CACHED', ''
    q = subprocess.run(cmd, cwd=R, capture_output=True, text=True, creationflags=0x08004000)
    return u['id'], sig, 'PASS' if q.returncode == 0 else 'FAIL', (q.stdout + q.stderr)[-3000:]

t0 = time.time(); stamps = {}; fails = []
with cf.ThreadPoolExecutor(jobs) as ex:
    for n, (i, sig, st, log) in enumerate(ex.map(unit_job, M['units']), 1):
        if st == 'FAIL': fails.append((i, log))
        else: stamps[str(i)] = sig
        if n % 25 == 0: print(f'{mode}: {n}/{len(M["units"])} ({time.time() - t0:.0f}s)', flush=True)
stamp.write_text(json.dumps(stamps))
if fails:
    for i, log in fails[:5]: print('FAIL', i, log)
    raise SystemExit(1)

clang = M['linker']
extra_objs = []
if mode in ('gen', 'csgen'):
    hook = objdir / 'pgo_hook.obj'
    q = subprocess.run([clang.replace('clang++.EXE', 'clang++.exe'), '--target=x86_64-pc-windows-msvc', '-O2', '-std=c++20',
                        '-fms-runtime-lib=static', '-c', str(P / 'pgo_hook.cpp'), '-o', str(hook)], capture_output=True, text=True)
    assert q.returncode == 0, q.stderr
    extra_objs.append(hook)
rsp = (B / 'Catherine.rsp').read_text()
rsp = rsp.replace('C:/catherine/recomp/Build/obj/', str(objdir).replace('\\', '/') + '/')
rsp = rsp.replace(str(objdir).replace(chr(92), '/') + '/catherine.res', 'C:/catherine/recomp/Build/obj/catherine.res')
rsp = rsp.replace('Build/Catherine.map', f'Build/{out_name}.map').replace('Build/Catherine.pdb', f'Build/{out_name}.pdb')
rsp_path = objdir / 'link.rsp'
rsp_path.write_text(rsp + '\n' + '\n'.join(f'"{str(o).replace(chr(92), "/")}"' for o in extra_objs) + '\n' +
                    ('"-fprofile-instr-generate"\n' if mode == 'gen' else '') + ('"-fcs-profile-generate"' + chr(10) + '"-Wl,/lto-cs-profile-generate"' + chr(10) if mode == 'csgen' else '') + ('"-flto=thin"' + chr(10) + '"-Wl,/opt:lldltojobs=16"' + chr(10) if lto else '') + ('"-Wl,/opt:lldlto=3"' + chr(10) if lto and o3 else ''))
out = B / f'{out_name}.exe'
q = subprocess.run([clang, '@' + str(rsp_path), '-o', str(out)], cwd=R, capture_output=True, text=True, creationflags=0x08004000)
(objdir / 'link.log').write_text(q.stdout + q.stderr)
assert q.returncode == 0, q.stderr[-3000:]
print('Built', out, f'{time.time() - t0:.0f}s', flush=True)
