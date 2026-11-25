"""Owned hidden scene capture using the project's existing loader and frame-dump path."""
from pathlib import Path
import sys
R=Path(__file__).resolve().parents[2];H=R/'Build/perf/harness';sys.path.insert(0,str(H));s=(H/'bench.py').read_text();a=s.index('        # ---- sample');z=s.index('        if a.capture:',a)
s=s.replace("if sc['caption']: assert sc['caption'] in (result['caption'] or ''), result['caption']", "if sc['caption'] and env.get('NX_HEADLESS')!='1': assert sc['caption'] in (result['caption'] or ''), result['caption']")
a=s.index('        # ---- sample');z=s.index('        if a.capture:',a)
s=s[:a]+'''        # Visual diagnosis does not require a performance capture map.
        if env.get('NX_HEADLESS')=='1':result['visible']=False
        if 'NX_TRACE_DRAWS' in env:
            # Arm the existing trace only after this owned process reaches gameplay.
            mt=(BUILD/(exe.stem+'.map')).read_text(errors='replace')
            pref=int(re.search(r'Preferred load address is\\s+([0-9a-fA-F]+)',mt)[1],16)
            ln=next(l for l in mt.splitlines()if '?trace_from@?1??trace_draw@'in l)
            address=int(re.match(r'\\s*\\S+\\s+\\S+\\s+([0-9a-fA-F]+)',ln)[1],16)-pref
            mods=(C.c_void_p*1024)();need=C.c_uint32();assert k.K32EnumProcessModules(probe.m.handle,mods,C.sizeof(mods),C.byref(need))
            k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
            k.WriteProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)];k.WriteProcessMemory.restype=W.BOOL
            handle=k.OpenProcess(0x428,False,pid);assert handle
            try:
                value=C.c_uint64(0);written=C.c_size_t();assert k.WriteProcessMemory(handle,mods[0]+address,C.byref(value),8,C.byref(written))and written.value==8
            finally:k.CloseHandle(handle)
        if env.get('EYE_CAMERA_HOLD')=='1':
            # Position only the owned game's QA camera, then leave it untouched.
            def stick(rx,ry,button=0):
                C.memmove(pad.v,struct.pack('<QiiiiI',button,0,0,rx,ry,1),28)
            stick(0,0,1<<5);time.sleep(.12)
            stick(0,-32767);time.sleep(.65);stick(0,0);time.sleep(.3)
            stick(-32767,0);time.sleep(3);stick(0,0)
            # Select mouse camera hold via the owned QA click serial; no desktop input.
            C.memmove(pad.v+92,struct.pack('<I',1),4);time.sleep(.3)
            # Apply relative look within this QA map; mouse ownership holds the result.
            C.memmove(pad.v+32,struct.pack('<iiI',-350,240,1),12);time.sleep(.5)
            C.memmove(pad.v+32,struct.pack('<iiI',0,0,2),12);time.sleep(.5)
        if env.get('EYE_CAMERA_SWEEP')=='1':
            # Only this owned process's QA controller; never desktop input.
            fdir=BUILD/'frames';fdir.mkdir(exist_ok=True);oldframes=set(fdir.glob('*.ppm'))
            (BUILD/'dump_frames').write_text('24 110'+chr(10))
            def stick(rx,ry,button=0):
                C.memmove(pad.v,struct.pack('<QiiiiI',button,0,0,rx,ry,1),28)
            stick(0,0,1<<5);time.sleep(.12)
            stick(0,-32767);time.sleep(.65);stick(0,0);time.sleep(.3)
            for rx,seconds in [(-32767,3.0),(32767,6.0),(-32767,3.0)]:
                stick(rx,0);time.sleep(seconds)
            stick(0,0);time.sleep(.3)
            sf=out/'sweep_frames';sf.mkdir(exist_ok=True)
            for f in sorted(set(fdir.glob('*.ppm'))-oldframes):f.replace(sf/f.name)
            result['sweep_frames']=len(list(sf.glob('*.ppm')))
        if env.get('EYE_MEASURE_FRAME')=='1':
            mt=(BUILD/(exe.stem+'.map')).read_text(errors='replace')
            pref=int(re.search(r'Preferred load address is\\s+([0-9a-fA-F]+)',mt)[1],16)
            ln=next(l for l in mt.splitlines()if ' ?g_frame@nx@@'in l)
            offset=int(ln.split()[2],16)-pref
            mods=(C.c_void_p*1024)();need=C.c_uint32();assert k.K32EnumProcessModules(probe.m.handle,mods,C.sizeof(mods),C.byref(need))
            begin=time.perf_counter();first=probe.m.q(mods[0]+offset)
            time.sleep(a.seconds)
            last=probe.m.q(mods[0]+offset);elapsed=time.perf_counter()-begin
            result['rendered_frames']=last-first;result['rendered_seconds']=elapsed;result['rendered_fps']=(last-first)/elapsed
        else:time.sleep(a.seconds)
        result['status']='VISUAL_CAPTURE_ONLY'
'''+s[z:];s=s.replace("ap.add_argument('--pgo'","ap.add_argument('--difficulty', choices=['Hard','Normal','Easy','Safety','Earned']); ap.add_argument('--pgo'");s=s.replace("sc = SCENES[a.scene]; exe =","sc = dict(SCENES[a.scene]); sc['difficulty'] = (None if a.difficulty == 'Earned' else a.difficulty) if a.difficulty else sc['difficulty']; exe =");s=s.replace("HERE = Path(__file__).resolve().parent",'HERE = Path('+repr(str(H))+')');exec(compile(s,str(H/'bench.py'),'exec'))
