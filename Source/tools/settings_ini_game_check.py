"""Verify native startup, Config preview/discard/apply and restart in a private game directory."""
import configparser
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / "Build"
D = B / "ini-config-game-test"
sys.path.insert(0, str(B / "perf/harness"))
import bench as bench

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def ini(path):
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(path, encoding="utf-8")
    return config

def run_game(exe, expected, exercise):
    env = {k:v for k,v in os.environ.items() if not k.startswith(("NX_", "CATHERINE_"))}
    env.update(NX_HEADLESS="1", NX_MUTE="1", CATHERINE_PERF_BACKGROUND="1")
    with (D / "game.log").open("wb") as log:
        p = subprocess.Popen([str(exe), "--assets-pak", str(B/"assets.pak")], cwd=D, env=env,
                             stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        pad = probe = None
        try:
            def wait(check, label, seconds=20):
                deadline = time.monotonic() + seconds
                while not check():
                    assert p.poll() is None and time.monotonic() < deadline, label
                    time.sleep(0.1)
            wait(lambda: "for fullscreen movie" in (D/"game.log").read_text(errors="replace"), "startup", 150)
            pad = bench.Pad(p.pid)
            probe = bench.Probe(p.pid,exe)
            def reach_piano():
                if probe.menu_ready(): return True
                pad.key(32, .08)
                return False
            wait(reach_piano, "title menu", 100)
            time.sleep(2)
            game = probe.m.q(probe.m.q(0x7000cc9f60))
            def native(off):
                return probe.m.read(game+off,1)[0]
            for off, raw in expected.items():
                assert native(off) == raw, (hex(off), native(off),raw)
            if not exercise:
                return
            print("Native startup values match edited INI.", flush=True)
            text = exe.with_suffix(".map").read_text()
            preferred = int(re.search(r"Preferred load address is\s+([0-9a-fA-F]+)",text)[1],16)
            mods = (C.c_void_p*1024)()
            needed = C.c_uint32()
            assert bench.k.K32EnumProcessModules(probe.m.handle,mods,C.sizeof(mods),C.byref(needed))
            def symbol(name):
                rows = [line for line in text.splitlines() if name in line]
                assert len(rows)==1, name
                return mods[0] + int(rows[0].split()[2], 16) - preferred
            pc = symbol(" catherine_config_pc.llvm.")
            page_addr = symbol(" catherine_piano_mouse.5 ")
            bright_x = symbol(" pc_brightness.1 ")
            bright_y = symbol(" pc_brightness.2 ")
            def dword(addr):
                return int.from_bytes(probe.m.read(addr,4),"little")
            def menu():
                return probe.m.q(pc)
            serial=0
            def click(x,y):
                nonlocal serial
                serial+=1
                C.memmove(pad.v+80,struct.pack("<iiIII",int(x),int(y),serial,serial,1),20)
                time.sleep(.45)
            if dword(page_addr)==0xb2:
                pad.key(16)
                wait(lambda:dword(page_addr)==0xb0,"main menu")
            assert dword(page_addr)==0xb0,hex(dword(page_addr))
            def brightness(step):
                click(300,650)
                wait(lambda:menu() and dword(menu()+12)==4,"open Config")
                for _ in range(6):
                    if dword(menu()+8)==1: break
                    click(780,100)
                    time.sleep(.6)
                assert dword(menu()+8)==1, "Display tab"
                time.sleep(.7)
                x, y = (struct.unpack("<f", probe.m.read(addr,4))[0] for addr in (bright_x,bright_y))
                click(x+24*step,y)
                print("Display brightness",step,native(0x2fb),flush=True)
            config_before = (D/"catherine-settings.ini").read_bytes()
            progress_before = digest(D/"save.dat")
            brightness(12)
            assert native(0x2fb)==18
            click(1560,1000)
            click(960,385)
            wait(lambda:not menu(),"discard")
            assert native(0x2fb)==expected[0x2fb]
            assert (D/"catherine-settings.ini").read_bytes()==config_before
            print("Config discard restored native brightness and original INI.",flush=True)
            time.sleep(1.3)
            brightness(14)
            assert native(0x2fb)==36
            click(1760,1000)
            time.sleep(.45)
            print("Save question state", dword(menu()+12), dword(menu()+20), flush=True)
            pad.key(32,.14)
            wait(lambda:ini(D/"catherine-settings.ini")["Game"]["Brightness"] == "70", "persist brightness")
            wait(lambda:not menu(),"save")
            assert ini(D/"catherine-settings.ini")["Game"]["Brightness"]=="70"
            assert digest(D/"save.dat")==progress_before,"Config save touched progress"
            print("Config Apply saved to INI with an identical progress file.",flush=True)
        finally:
            if pad: pad.close()
            if probe: probe.m.close()
            if p.poll() is None: p.terminate()
            p.wait(15)

def main():
    D.mkdir(exist_ok=True)
    bench.BUILD=D
    files=["save.dat","assets-required.json","assets-verification.json","CatherineInterpolation.dll",
           "open_svpflow.dll","svpflow1_vs.dll","svpflow2_vs.dll","dev-checkpoints.pak"]
    for name in files: shutil.copyfile(B/name,D/name)
    shutil.copytree(B/"CathAssets",D/"CathAssets",dirs_exist_ok=True)
    protected = {k:digest(B/k) for k in ["save.dat","catherine-settings.ini"]}
    source_exe = B / (sys.argv[1] if len(sys.argv)>1 else "catherine1.exe")
    assert "catherine_save_native_config" in source_exe.with_suffix(".map").read_text()
    exe = D/"catherine1.exe"
    shutil.copyfile(source_exe,exe)
    shutil.copyfile(source_exe.with_suffix(".map"),exe.with_suffix(".map"))
    config = ini(B/"ini-config-test/installed.migrated.ini")
    config["Catherine"].update({"devmenu":"false","Fps":"60","MovieInterpolation":"0"})
    config["Game"].update({"Brightness":"75","Subtitles":"false","Vibration":"false","BacksideCamera":"false",
                           "Guidelines":"false","PuzzleTutorial":"false","RetryAssist":"false","VoiceLanguage":"1",
                           "InvertCameraX":"1","InvertCameraY":"1"})
    with (D/"catherine-settings.ini").open("w",encoding="utf-8") as out:
        config.write(out,space_around_delimiters=False)
    expected = {off:1 for off in (0x300,0x2f4,0x2f9,0x2fa,0x2fe,0x2ff,0x301,0x2f6,0x2f5)}
    expected[0x2fb]=45
    try:
        run_game(exe,expected,True)
        expected[0x2fb]=36
        run_game(exe,expected,False)
        result={"native_startup":True,"native_ini_restart":True,"config_discard":True,"config_apply":True,"config_preserved_progress":True}
        (D/"validation.json").write_text(json.dumps(result,indent=2))
        print(result, flush=True)
    finally:
        exe.unlink(missing_ok=True)
        assert protected=={k:digest(B/k) for k in protected}

if __name__=="__main__":
    main()
