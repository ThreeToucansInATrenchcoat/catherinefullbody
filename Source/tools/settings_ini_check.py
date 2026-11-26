"""Standalone settings round-trip, legacy migration, native Config and malformed-value checks."""
import configparser
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "Build"
OUT = BUILD / "ini-config-test"
INI = OUT / "catherine-settings.ini"
EXE = OUT / "catherine2.exe"

def check(mode):
    subprocess.run([str(EXE), mode], cwd=OUT, check=True, timeout=15)

def load():
    config = configparser.ConfigParser()
    config.read(INI, encoding="utf-8")
    return config

def main():
    OUT.mkdir(exist_ok=True)
    unit = next(u for u in json.loads((ROOT / "Source/build-manifest.json").read_text())["units"]
                if u["source"].endswith("/catherine_refresh.cpp"))
    command = unit["command"]
    subprocess.run(command[:command.index("-c")] + [str(ROOT / "Source/tools/settings_ini_check.cpp"),
                   "-luser32", "-o", str(EXE)], cwd=OUT, check=True)
    protected = {name: (BUILD / name).read_bytes() for name in ("save.dat", "catherine-settings.ini")}
    try:
        for version in range(1, 7):
            encoding = "utf-16" if version == 5 else "utf-8"
            INI.write_text(f"[Catherine]\nVersion={version}\nBindings=FEDCBA9876543210\nP2Bindings=FEDCBA9876543210\n", encoding=encoding)
            check("check-pre6" if version < 6 else "check-v6")
            assert load()["Catherine"]["Version"] == "7"

        keys = [32,16,90,17,67,86,9,69,81,256,27,8,65,87,68,83,74,73,76,75,1,2,37,38,39,40,13]
        one, two = keys.copy(), keys.copy()
        one[0], one[7], one[8] = 91, 82, 87
        one[12:16] = map(ord, "SEFD")
        one[16] = 0
        two[17] = 303
        two[2] = ord("T")
        two[12:16] = (37,38,39,40)
        def packed(values): return "".join(f"{n:04X}" for n in values)
        INI.write_text(f"""[cAtHeRiNe]
Version=6
DeVmEnU=FaLsE
Fps=120
Fullscreen=1
WindowHeight=1440
AntiAliasing=4
MovieInterpolation=240
ButtonPrompts=3
Bindings=FEDCBA9876543021
P2Bindings=FEDCBA9876543210
MovementPreset=1
SwapSticks=1
P2MovementPreset=2
P2SwapSticks=1
Keyboard={packed(one)}
P2Keyboard={packed(two)}
P1Device=keyboard
P2Device=qa:Pad 2
""")
        check("check-migration")
        config = load()
        assert config["Player1.Keyboard"]["Grab"] == "VK91"
        assert config["Player2.Keyboard"]["Phone"] == "T"
        assert config["Game"]["Brightness"] == "80" and config["Game"]["Subtitles"] == "false"
        before = INI.read_bytes()
        check("check-roundtrip")
        assert before == INI.read_bytes()
        check("check-roundtrip")

        INI.write_text("""[Catherine]
Version=7
Fps=oops
devmenu=invalid
P1Device=none
P2Device=auto
Unrecognized=foo
[Player1.Keyboard]
Grab=missing-key
[Player2.Keyboard]
Grab=B
[Player1.Gamepad]
Pause=Triangle
[gAMe]
Brightness=200
vOiCeLaNgUaGe=1
""")
        check("check-invalid")

        # Exercise the actual installation's effective values without writing to it.
        (OUT / "installed.original.ini").write_bytes(protected["catherine-settings.ini"])
        INI.write_bytes(protected["catherine-settings.ini"])
        system = protected["save.dat"][0xc8000:0xc8000+0x9810]
        assert len(system) == 0x9810
        (OUT / "legacy-system.bin").write_bytes(system)
        check("migrate-install-copy")
        shutil.copyfile(INI, OUT / "installed.migrated.ini")
    finally:
        EXE.unlink(missing_ok=True)
        assert all((BUILD / k).read_bytes() == v for k,v in protected.items())
    print("Versions 1-7, both input profiles, native settings, locked campaign choices, invalid INI and private installation migration passed.")

if __name__ == "__main__":
    main()
