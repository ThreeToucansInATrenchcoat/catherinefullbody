"""Isolated direct-level CPU/GPU benchmark; leaves the user's saves/settings alone."""
import argparse, ctypes as C, csv, hashlib, json, os, shutil, struct, subprocess, sys, time
from pathlib import Path

R = Path(__file__).resolve().parents[2]
B = R / 'Build'
sys.path.insert(0, 'C:/catherine/old/Build/perf/harness')
import bench as b


def link(src, dst):
    src,dst=Path(src),Path(dst)
    if not dst.exists():
        try: os.link(src,dst)
        except OSError as error:
            if getattr(error,'winerror',None) not in (1,17,50) and error.errno!=18: raise
            shutil.copyfile(src,dst)


class Util(C.Structure):
    _fields_ = [('gpu', C.c_uint), ('memory', C.c_uint)]


class CounterValue(C.Structure):
    _fields_ = [('status', C.c_uint32), ('value', C.c_double)]


class CpuCounters:
    def __init__(self):
        self.pdh = C.WinDLL('pdh.dll')
        self.query = C.c_void_p()
        self.pdh.PdhOpenQueryW.argtypes = [C.c_wchar_p, C.c_size_t, C.POINTER(C.c_void_p)]
        self.pdh.PdhAddEnglishCounterW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_size_t, C.POINTER(C.c_void_p)]
        self.pdh.PdhCollectQueryData.argtypes = [C.c_void_p]
        self.pdh.PdhGetFormattedCounterValue.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.POINTER(CounterValue)]
        self.pdh.PdhCloseQuery.argtypes = [C.c_void_p]
        assert self.pdh.PdhOpenQueryW(None, 0, C.byref(self.query)) == 0
        self.counters = {}
        for name, path in [('utility', r'\Processor Information(_Total)\% Processor Utility'),
                           ('time', r'\Processor(_Total)\% Processor Time')]:
            handle = C.c_void_p()
            assert self.pdh.PdhAddEnglishCounterW(self.query, path, 0, C.byref(handle)) == 0
            self.counters[name] = handle
        self.pdh.PdhCollectQueryData(self.query)

    def read(self):
        self.pdh.PdhCollectQueryData(self.query)
        result = {}
        for name, counter in self.counters.items():
            value = CounterValue()
            status = self.pdh.PdhGetFormattedCounterValue(counter, 0x200 | 0x8000, None, C.byref(value))
            result[name] = value.value if status == 0 and value.status in [0, 1] else None
        return result

    def close(self): self.pdh.PdhCloseQuery(self.query)


class GpuMemoryCounters:
    """WDDM per-process dedicated/shared bytes, sampled through English PDH counters."""
    class Value(C.Structure):
        _fields_ = [('status', C.c_uint32), ('value', C.c_int64)]
    def __init__(self, pid):
        class Item(C.Structure):
            _fields_ = [('name', C.c_wchar_p), ('value', GpuMemoryCounters.Value)]
        self.Item = Item
        self.pid = 'pid_' + str(pid) + '_'
        self.pdh = C.WinDLL('pdh.dll')
        self.query = C.c_void_p()
        self.pdh.PdhOpenQueryW.argtypes = [C.c_wchar_p, C.c_size_t, C.POINTER(C.c_void_p)]
        self.pdh.PdhAddEnglishCounterW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_size_t, C.POINTER(C.c_void_p)]
        self.pdh.PdhCollectQueryData.argtypes = [C.c_void_p]
        self.pdh.PdhGetFormattedCounterArrayW.argtypes = [C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32), C.c_void_p]
        self.pdh.PdhCloseQuery.argtypes = [C.c_void_p]
        assert self.pdh.PdhOpenQueryW(None, 0, C.byref(self.query)) == 0
        self.counters = {}
        for key, label in [('dedicated', 'Dedicated Usage'), ('shared', 'Shared Usage')]:
            handle = C.c_void_p()
            assert self.pdh.PdhAddEnglishCounterW(self.query, '\\GPU Process Memory(*)\\' + label, 0, C.byref(handle)) == 0
            self.counters[key] = handle
        self.samples = []
        self.last = 0
    def read(self, phase='load'):
        now = time.monotonic()
        if now - self.last < .4: return None
        self.last = now
        if self.pdh.PdhCollectQueryData(self.query) != 0: return None
        values = dict(phase=phase, at=now)
        for key, handle in self.counters.items():
            size, count = C.c_uint32(), C.c_uint32()
            self.pdh.PdhGetFormattedCounterArrayW(handle, 0x400 | 0x1000, C.byref(size), C.byref(count), None)
            if not size.value: return None
            buffer = C.create_string_buffer(size.value)
            if self.pdh.PdhGetFormattedCounterArrayW(handle, 0x400 | 0x1000, C.byref(size), C.byref(count), buffer) != 0: return None
            items = C.cast(buffer, C.POINTER(self.Item))
            found = [items[i].value.value for i in range(count.value)
                     if items[i].name.startswith(self.pid) and items[i].value.status in (0, 1)]
            if not found: return None
            values[key] = sum(found)
        self.samples.append(values)
        return values
    def summary(self):
        assert self.samples and max(row['dedicated'] for row in self.samples) > 0, 'Per-process GPU memory counter unavailable'
        sampled = [row for row in self.samples if row['phase'] == 'sample']
        assert sampled
        return dict(gpu_memory_dedicated_peak_bytes=max(row['dedicated'] for row in self.samples),
                    gpu_memory_dedicated_sample_peak_bytes=max(row['dedicated'] for row in sampled),
                    gpu_memory_shared_peak_bytes=max(row['shared'] for row in self.samples),
                    gpu_memory_samples=self.samples,
                    gpu_memory_source='Windows WDDM GPU Process Memory, dedicated/shared usage summed over this PID only')
    def close(self): self.pdh.PdhCloseQuery(self.query)


def gpu_reader():
    try:
        nv = C.WinDLL('nvml.dll')
        assert nv.nvmlInit_v2() == 0
        device = C.c_void_p()
        nv.nvmlDeviceGetHandleByIndex_v2.argtypes = [C.c_uint, C.POINTER(C.c_void_p)]
        assert nv.nvmlDeviceGetHandleByIndex_v2(0, C.byref(device)) == 0
        nv.nvmlDeviceGetUtilizationRates.argtypes = [C.c_void_p, C.POINTER(Util)]
        def read():
            value = Util()
            return value.gpu if nv.nvmlDeviceGetUtilizationRates(device, C.byref(value)) == 0 else None
        return read
    except (OSError, AssertionError):
        return lambda: None


def cpu_time(handle):
    times = [C.c_uint64() for _ in range(4)]
    assert b.k.GetProcessTimes(handle, *(C.byref(x) for x in times))
    return (times[2].value + times[3].value) * 1e-7


def cpu_cycles(handle):
    value = C.c_uint64()
    assert b.k.QueryProcessCycleTime(handle, C.byref(value))
    return value.value


def foreground(hwnd):
    if b.u.GetForegroundWindow() == hwnd: return
    tid = b.u.GetWindowThreadProcessId(b.u.GetForegroundWindow(), None)
    me = b.k.GetCurrentThreadId()
    attached = b.u.AttachThreadInput(me, tid, True) if tid != me else False
    try:
        b.u.ShowWindow(hwnd, 5)
        b.u.BringWindowToTop(hwnd)
        b.u.SetForegroundWindow(hwnd)
        if b.u.GetForegroundWindow() != hwnd:
            # Windows may deny activation until this process supplies an input event.
            b.u.keybd_event(0x12, 0, 0, 0)
            b.u.keybd_event(0x12, 0, 2, 0)
            b.u.SetForegroundWindow(hwnd)
    finally:
        if attached: b.u.AttachThreadInput(me, tid, False)


def prefer_fast_cpu_sets(process):
    k=C.WinDLL('kernel32',use_last_error=True)
    k.GetSystemCpuSetInformation.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32),C.c_void_p,C.c_uint32]
    k.SetProcessDefaultCpuSets.argtypes=[C.c_void_p,C.POINTER(C.c_uint32),C.c_uint32]
    needed=C.c_uint32();handle=int(process._handle)
    k.GetSystemCpuSetInformation(None,0,C.byref(needed),handle,0)
    data=C.create_string_buffer(needed.value)
    if not k.GetSystemCpuSetInformation(data,needed,C.byref(needed),handle,0):raise C.WinError(C.get_last_error())
    sets=[];at=0
    while at<needed.value:
        size,kind=struct.unpack_from('<II',data,at)
        if size<8:raise RuntimeError('Invalid CPU set size')
        if kind==0 and size>=32:
            ident,group,logical,core,cache,numa,eff,flags=struct.unpack_from('<IHBBBBBB',data,at+8)
            if not flags&2 or flags&4:sets.append((ident,eff))
        at+=size
    if not sets:return []
    best=max(eff for _,eff in sets);ids=[ident for ident,eff in sets if eff==best]
    if len(ids)==len(sets):return []
    values=(C.c_uint32*len(ids))(*ids)
    if not k.SetProcessDefaultCpuSets(handle,values,len(ids)):raise C.WinError(C.get_last_error())
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default='catherine2.exe')
    ap.add_argument('--fixture-root',type=Path,help='Owned scratch directory for isolated saves, snapshots and outputs')
    ap.add_argument('--tag', required=True)
    ap.add_argument('--level', type=int, default=10101)
    ap.add_argument('--fps', type=int, default=0)
    ap.add_argument('--shadow-resolution', choices=['Auto', '2048', '4096', '8192'])
    ap.add_argument('--gpu-memory', action='store_true', help='Record per-process peak dedicated/shared GPU memory, including loading')
    ap.add_argument('--seconds', type=float, default=20)
    ap.add_argument('--settle', type=float, default=5)
    ap.add_argument('--profile', type=float, default=0)
    ap.add_argument('--capture', action='store_true')
    ap.add_argument('--fast-cores',action='store_true',help='Compare the owned game using the faster CPU-set class on hybrid processors')
    ap.add_argument('--movement', action='store_true',help='Exercise a short owned controller sequence after the FPS sample')
    ap.add_argument('--pgo', action='store_true', help='Reset/dump the isolated instrumented build profile around the sample')
    ap.add_argument('--env', action='append', default=[])
    a = ap.parse_args()
    d = a.fixture_root or (B / 'catherine-cpu-test'); d.mkdir(parents=True,exist_ok=True)
    out = d / 'runs' / (time.strftime('%Y%m%d-%H%M%S') + '-' + a.tag)
    out.mkdir(parents=True)
    b.BUILD = d
    for name in ['assets-required.json', 'assets-verification.json', 'dev-checkpoints.pak',
                 'CatherineInterpolation.dll', 'open_svpflow.dll', 'svpflow1_vs.dll', 'svpflow2_vs.dll',
                 'pipelines_10de_2b85.bin']:
        if (B / name).exists(): link(B / name, d / name)
    shutil.copytree(B / 'CathAssets', d / 'CathAssets', copy_function=link,dirs_exist_ok=True)
    # Snapshot baseline by hard link: build.py publishes replacements by rename.
    for ext in ['.exe', '.map']:
        src = B / Path(a.exe).with_suffix(ext)
        dst = d / ('Catherine-' + a.tag + ext)
        link(src, dst)
    exe = d / ('Catherine-' + a.tag + '.exe')
    link(exe.with_suffix('.map'), out / 'exe.map')
    shutil.copyfile(B / 'save.dat', d / 'save.dat')
    (d / 'catherine-settings.ini').write_text('[Catherine]\nVersion=7\ndevmenu=true\nFps=%d\nInternalUpscaling=0\nAntiAliasing=0\nMovieInterpolation=0\nFullscreen=false\nWindowHeight=720\nVSync=false\nTripleBuffering=false\nAltBuildMode=false\n[Game]\nPuzzleDifficulty=2\nPuzzleType=0\n' % a.fps)
    if a.shadow_resolution:
        ini = d / 'catherine-settings.ini'
        ini.write_text(ini.read_text().replace('[Catherine]\n', '[Catherine]\nShadowResolution=' + a.shadow_resolution + '\n', 1))
    protected = {n: hashlib.sha256((B / n).read_bytes()).hexdigest() for n in ['save.dat', 'catherine-settings.ini']}
    env = {k:v for k,v in os.environ.items() if not k.startswith(('NX_', 'CATHERINE_'))}
    env.update(NX_MUTE='1', CATHERINE_PERF_CAPTURE='1',
               CATHERINE_DEV_INFINITE_HEALTH='1', TEMP=str(d), TMP=str(d))
    for item in a.env:
        k, v = item.split('=', 1); env[k] = v
    p = pad = probe = cap = memory = None
    result = dict(exe=exe.name, tag=a.tag, level=a.level, fps_cap=a.fps,
                  exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), env=a.env)
    result['shadow_resolution'] = a.shadow_resolution
    read_gpu = gpu_reader()
    counters = CpuCounters()
    b.k.GetProcessTimes.argtypes = [C.c_void_p] + [C.POINTER(C.c_uint64)] * 4
    b.k.QueryProcessCycleTime.argtypes = [C.c_void_p, C.POINTER(C.c_uint64)]
    b.u.AttachThreadInput.argtypes = [C.c_uint32, C.c_uint32, C.c_int]
    b.u.BringWindowToTop.argtypes = [C.c_void_p]
    b.u.SetForegroundWindow.argtypes = [C.c_void_p]
    b.u.ShowWindow.argtypes = [C.c_void_p, C.c_int]
    try:
        with (out / 'game.log').open('wb') as log:
            p = subprocess.Popen([str(exe), '--assets-pak', str(B / 'assets.pak'), '--dev-level', str(a.level)],
                                 cwd=d, env=env, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            if a.gpu_memory: memory = GpuMemoryCounters(p.pid)
            if a.fast_cores:result['preferred_cpu_sets']=prefer_fast_cpu_sets(p)
            deadline = time.monotonic() + 240
            while time.monotonic() < deadline:
                assert p.poll() is None, ('game exited at startup', p.returncode)
                if memory: memory.read()
                h = b.k.OpenFileMappingW(2, False, f'Local\\CatherineBenchPad-{p.pid}')
                if h:
                    b.k.CloseHandle(h); break
                time.sleep(.25)
            else: raise TimeoutError('startup did not create BenchPad')
            pad = b.Pad(p.pid); probe = b.Probe(p.pid, exe)
            probe.floor = ((a.level - 10000) // 100, a.level % 100)
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                assert p.poll() is None, ('game exited', p.returncode)
                if memory: memory.read()
                status = probe.loader_state()
                if status['phase'] == 7: raise RuntimeError(status)
                if status['phase'] == 6 and probe.boss_active()[0]: break
                time.sleep(.15)
            else: raise TimeoutError(('level not active', status, probe.puzzle()))
            if memory:
                until = time.monotonic() + a.settle
                while time.monotonic() < until:
                    memory.read(); time.sleep(.15)
            else:
                time.sleep(a.settle)
            all_windows = [(h,b.wtext(h),b.wtext(h,True),bool(b.u.IsWindowVisible(h))) for h in b.windows_of(p.pid)]
            print('windows:',all_windows,flush=True)
            result['windows'] = all_windows
            windows = [h for h,title,cls,visible in all_windows if cls.endswith('Recomp')]
            for h in windows:
                foreground(h)
            time.sleep(1)
            cap = b.open_capture(p.pid)
            freq, capacity, count0 = b.header(cap[1])
            counters.read()
            if a.pgo:
                pk=C.WinDLL('kernel32');pk.OpenEventW.argtypes=[C.c_uint32,C.c_int,C.c_wchar_p];pk.OpenEventW.restype=C.c_void_p
                pk.SetEvent.argtypes=[C.c_void_p];pk.CloseHandle.argtypes=[C.c_void_p];pk.WaitForSingleObject.argtypes=[C.c_void_p,C.c_uint32]
                reset=pk.OpenEventW(2,False,f'Local\\CatherinePGOReset-{p.pid}');assert reset,'PGO reset event missing'
                pk.SetEvent(reset);pk.CloseHandle(reset)
            begin = b.qpc(); cpu0 = cpu_time(probe.m.handle); cycles0 = cpu_cycles(probe.m.handle); start = time.monotonic()
            checks = []; gpu = []; foreground_checks = []; cpu_samples = []
            while time.monotonic() - start < a.seconds:
                assert p.poll() is None, 'game exited during sample'
                checks.append(probe.boss_active())
                if memory: memory.read('sample')
                gpu.append(read_gpu())
                cpu_samples.append(counters.read())
                if windows: foreground(windows[0])
                foreground_checks.append(b.u.GetForegroundWindow() in windows)
                time.sleep(.5)
            elapsed = time.monotonic() - start; cpu = cpu_time(probe.m.handle) - cpu0
            cycles = cpu_cycles(probe.m.handle) - cycles0
            end = b.qpc(); _, _, count1 = b.header(cap[1]); assert count1 < capacity
            rows = [b.ENTRY.unpack(C.string_at(cap[1] + 64 + i * 32, 32)) for i in range(count0, count1)]
            rows = [r for r in rows if begin <= r[1] < end]
            with (out / 'frames.csv').open('w', newline='') as f:
                w = csv.writer(f); w.writerow(['seq','qpc','present','vk','tid']); w.writerows(rows)
            result.update(b.summarize(rows, freq))
            result.update(cpu_seconds=cpu, wall_seconds=elapsed, cpu_total_percent=100*cpu/elapsed/os.cpu_count(),
                          cpu_core_equivalents=cpu/elapsed, cpu_ms_per_frame=1000*cpu/len(rows),
                          cpu_cycles=cycles, cpu_million_cycles_per_frame=cycles/len(rows)/1e6,
                          system_cpu_utility_percent=sum(c['utility'] for c in cpu_samples[1:] if c['utility'] is not None)/max(1,len(cpu_samples)-1),
                          system_cpu_time_percent=sum(c['time'] for c in cpu_samples[1:] if c['time'] is not None)/max(1,len(cpu_samples)-1),
                          gpu_device_percent=sum(x for x in gpu if x is not None)/max(1,len([x for x in gpu if x is not None])),
                          gpu_samples=gpu, foreground_fraction=sum(foreground_checks)/len(foreground_checks), checks=checks,
                          status='measured' if all(c[0] for c in checks) and all(foreground_checks) else 'invalid')
            if memory: result.update(memory.summary())
            print(json.dumps({k:v for k,v in result.items() if k not in ['checks','gpu_samples','gpu_memory_samples']}), flush=True)
            if a.pgo:
                dump=pk.OpenEventW(2,False,f'Local\\CatherinePGODump-{p.pid}');done=pk.OpenEventW(0x100000,False,f'Local\\CatherinePGODone-{p.pid}')
                assert dump and done,'PGO dump events missing'
                pk.SetEvent(dump);assert pk.WaitForSingleObject(done,15000)==0,'PGO dump timed out'
                pk.CloseHandle(dump);pk.CloseHandle(done);result['pgo_dumped']=True
            if a.movement:
                motion=[]
                def sample_motion(label):
                    outer,actor=probe.puzzle()
                    motion.append(dict(label=label,outer=outer,actor=actor))
                    return actor
                initial=sample_motion('initial')
                for label,button in [('up',1<<13),('up',1<<13),('left',1<<12),('right',1<<14),('up',1<<13)]:
                    C.memset(pad.v,0,40);C.memmove(pad.v,button.to_bytes(8,'little'),8);time.sleep(.15);C.memset(pad.v,0,40);time.sleep(.3);sample_motion(label)
                time.sleep(.5);last=sample_motion('settled')
                result['movement']=motion
                result['movement_changed']=any(x['actor'].get('xyz')!=initial.get('xyz') or x['actor'].get('action')!=initial.get('action') or x['actor'].get('direction')!=initial.get('direction') for x in motion[1:])
                result['movement_valid']=all(x['outer']==32 and x['actor'].get('status')=='valid' and (x['actor'].get('stage_number'),x['actor'].get('substage'))==probe.floor and not x['actor'].get('state',0)&0x80000000 for x in motion)
                print('movement',result['movement_changed'],result['movement_valid'],flush=True)
            if a.capture:
                fdir=d/'frames'; before={p:p.stat().st_mtime_ns for p in fdir.glob('*.ppm')}
                (d/'dump_frames').write_text('1 1\n')
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    got={p for p in fdir.glob('*.ppm') if p not in before or p.stat().st_mtime_ns!=before[p]}
                    if got:
                        time.sleep(.5)
                        for path in got: shutil.copyfile(path,out/path.name)
                        break
                    time.sleep(.1)
            if a.profile:
                q = subprocess.run(['C:/catherine/old/Build/perf/harness/stacksampler.exe', str(p.pid), str(a.profile), str(out/'stacks.csv'), '12', '2'], capture_output=True, text=True)
                result['profile'] = [q.returncode, q.stdout, q.stderr]
                print('profile', result['profile'], flush=True)
    except Exception as e:
        result.update(status='failed', error=repr(e)); raise
    finally:
        if cap: b.k.UnmapViewOfFile(cap[1]); b.k.CloseHandle(cap[0])
        if pad: pad.close()
        if probe: probe.m.close()
        if p and p.poll() is None: p.terminate(); p.wait(15)
        counters.close()
        if memory: memory.close()
        result['user_files_unchanged'] = all(hashlib.sha256((B/n).read_bytes()).hexdigest()==v for n,v in protected.items())
        (out/'result.json').write_text(json.dumps(result,indent=2))
        print('results:',out,flush=True)


if __name__ == '__main__': main()
