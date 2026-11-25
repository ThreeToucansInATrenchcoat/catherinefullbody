"""Owned hidden Babel QA session. JSON commands on stdin; no desktop input."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,os,queue,re,struct,subprocess,sys,threading,time
ROOT=Path(__file__).resolve().parents[2];BUILD=ROOT/'Build';sys.path.insert(0,str(BUILD/'perf/harness'))
import bench as b
p=argparse.ArgumentParser();p.add_argument('--fps',type=int,default=60);p.add_argument('--exe',default='Catherine.exe');p.add_argument('--tag',default='babel');p.add_argument('--lifetime',type=float,default=1800);p.add_argument('--env',action='append',default=[]);p.add_argument('--p2',action='store_true');args=p.parse_args()
assert subprocess.run(['powershell','-NoProfile','-Command',"@(Get-Process -Name catherine,catherine1,catherine2 -ErrorAction SilentlyContinue).Count"],capture_output=True,text=True).stdout.strip()=='0','another game is running'
out=BUILD/'perf/runs'/f'{time.strftime("%Y%m%d-%H%M%S")}-{args.tag}';out.mkdir(parents=True)
settings=BUILD/'catherine-settings.ini';saved=settings.read_bytes();savepath=BUILD/'save.dat';savebytes=savepath.read_bytes();savehash=hashlib.sha256(savebytes).hexdigest()
(out/'save-before.dat').write_bytes(savebytes)
settings.write_bytes(re.sub(rb'(?m)^Fps=-?\d+',f'Fps={args.fps}'.encode(),saved))
if args.p2:
    pair=re.sub(rb'(?m)^P1Device=[^\r\n]*',b'P1Device=keyboard',settings.read_bytes())
    settings.write_bytes(re.sub(rb'(?m)^P2Device=[^\r\n]*',b'P2Device=qa:controller-2',pair))
env={k:v for k,v in os.environ.items()if not k.startswith(('NX_','CATHERINE_'))};env.update(NX_HEADLESS='1',NX_MUTE='1',CATHERINE_PERF_BACKGROUND='1',CATHERINE_DEV_INFINITE_HEALTH='1')
for item in args.env:
    key,value=item.split('=',1);env[key]=value
log=(out/'start.log').open('wb');proc=pad=probe=None

def say(value):
    with (out/'events.jsonl').open('a') as events:events.write(json.dumps(value)+'\n')
    print(json.dumps(value),flush=True)
def snap():
    m=probe.m;result={'pid':proc.pid,'run':str(out)}
    try:
        menu=m.q(probe.menu);result['piano']=hex(menu)
        if menu:result['piano_rows']=list(struct.unpack('<3h',m.read(menu+0xb0,6)));result['piano_context']=m.read(probe.menu+8,24).hex()
    except OSError:pass
    try:result['puzzle']=probe.puzzle()
    except OSError:pass
    tasks=[]
    try:
        context=m.q(0x7000cced98);node=m.q(context+0x1eb0);seen=set()
        while node and node not in seen and len(seen)<128:
            seen.add(node);fn=m.q(node+0x30);work=m.q(node+0x48)
            row={'fn':hex(fn),'work':hex(work)}
            if work:
                try:row['state']=m.read(work+0x640,0x70).hex()
                except OSError:pass
            tasks.append(row);node=m.q(node+0x78)
    except OSError:pass
    result['task_count']=len(tasks)
    try:
        registry=m.q(0x7000ccadf0);actors=[]
        for i in range(2):
            actor=m.q(registry+8*i)
            if not actor:continue
            model=m.q(actor+0x20);row={'player':i+1,'actor':hex(actor),'model':hex(model),'class':list(struct.unpack('<4I',m.read(actor+0xe0,16)))}
            if model:
                row['xyz']=list(struct.unpack('<3f',m.read(model+0x5d0,12)))
                row['model_ids']=list(struct.unpack('<4I',m.read(model+0x5a0,16)))
                row['name']=m.read(model+0x1c,96).split(b'\0')[0].decode(errors='replace')
                parent=m.q(model+0x468);table=m.q(parent+0x138)if parent else 0
                row['parent']=hex(parent);row['table']=hex(table)
                if table:row['bones']=struct.unpack('<I',m.read(table,4))[0]
            actors.append(row)
        result['actors']=actors
    except OSError:pass
    return result

def capture():
    directory=BUILD/'frames';directory.mkdir(exist_ok=True);before=set(directory.glob('*.ppm'));(BUILD/'dump_frames').write_text('1 1\n');end=time.monotonic()+15
    while time.monotonic()<end:
        new=set(directory.glob('*.ppm'))-before
        if new:
            time.sleep(.2);source=sorted(new)[-1];target=out/f'capture-{time.time_ns()}.ppm';source.replace(target)
            from PIL import Image
            image=Image.open(target);image.thumbnail((1500,1000));png=target.with_suffix('.png');image.save(png);return str(png)
        time.sleep(.1)
    return None
try:
    exe=BUILD/args.exe;proc=subprocess.Popen([str(exe),'--assets-pak',str(BUILD/'assets.pak')],cwd=BUILD,env=env,stdout=log,stderr=subprocess.STDOUT)
    pad=b.Pad(proc.pid);b.k.UnmapViewOfFile(pad.v);pad.v=b.k.MapViewOfFile(pad.h,2,0,0,0);assert pad.v
    probe=b.Probe(proc.pid,exe);start=time.monotonic()
    while 'for fullscreen movie' not in (out/'start.log').read_text(errors='replace'):
        if time.monotonic()-start>90:raise TimeoutError('startup movie')
        time.sleep(.1)
    time.sleep(.75)
    while not probe.menu_ready():
        if time.monotonic()-start>150:raise TimeoutError('main menu')
        pad.key(32,.08);time.sleep(.12)
    time.sleep(1);say({'ready':snap()})
    commands=queue.Queue()
    def reader():
        for line in sys.stdin:commands.put(line)
        commands.put('{"op":"close"}')
    threading.Thread(target=reader,daemon=True).start();deadline=time.monotonic()+args.lifetime
    while time.monotonic()<deadline and proc.poll()is None:
        try:line=commands.get(timeout=.2)
        except queue.Empty:continue
        try:
            request=json.loads(line);op=request['op']
            if op=='close':break
            if op=='key':pad.key(int(request['vk']),float(request.get('hold',.1)));time.sleep(float(request.get('delay',.3)))
            elif op=='skip_intro':
                # Invoke only while a Babel intro movie is on screen. Never send
                # skip keys blindly during gameplay, where Escape pauses instead.
                pad.key(27,.08);time.sleep(.15);pad.key(17,.08);time.sleep(.3)
            elif op=='wait':time.sleep(min(55,float(request.get('seconds',1))))
            elif op=='pad':
                C.memmove(pad.v,struct.pack('<QiiiiI',request.get('buttons',0),request.get('lx',0),request.get('ly',0),request.get('rx',0),request.get('ry',0),1),28)
                if 'p2' in request:
                    second=request['p2'];C.memmove(pad.v+104,struct.pack('<QiiiiII',second.get('buttons',0),second.get('lx',0),second.get('ly',0),second.get('rx',0),second.get('ry',0),1,int(second.get('connected',True))),32)
                time.sleep(float(request.get('seconds',.1)))
            elif op=='read':
                address=int(request['address'],0);size=min(8192,int(request.get('size',128)));say({'read':hex(address),'hex':probe.m.read(address,size).hex()});continue
            elif op=='capture':say({'capture':capture(),'state':snap()});continue
            elif op!='state':raise ValueError(op)
            say(snap())
        except Exception as e:say({'error':repr(e)})
except Exception as e:say({'fatal':repr(e)})
finally:
    if pad:pad.close()
    if probe:probe.m.close()
    if proc and proc.poll()is None:proc.terminate();proc.wait(10)
    log.close();settings.write_bytes(saved);savepath.write_bytes(savebytes);unchanged=hashlib.sha256(savepath.read_bytes()).hexdigest()==savehash
    result={'closed_owned_pid':proc.pid if proc else None,'settings_restored':settings.read_bytes()==saved,'save_unchanged':unchanged,'run':str(out)};(out/'session-result.json').write_text(json.dumps(result,indent=2));say(result)
