"""Standalone settings round-trip, legacy migration, native Config and malformed-value checks."""
import configparser
import os
import tempfile
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
    command = [argument for argument in unit["command"] if argument != "-DNDEBUG"]
    subprocess.run(command[:command.index("-c")] + [str(ROOT / "Source/tools/settings_ini_check.cpp"),
                   "-luser32", "-o", str(EXE)], cwd=OUT, check=True)
    protected = {name: (BUILD / name).read_bytes() for name in ("save.dat", "catherine-settings.ini")
                 if (BUILD / name).is_file()}
    try:
        for value, enabled in [(None, False), ("invalid", False), ("false", False), ("0", False), ("true", True), ("TRUE", True), ("1", True)]:
            INI.write_text("[Catherine]\n" + ("devmenu=" + value + "\n" if value is not None else ""))
            check("dev-opt-in" if enabled else "dev-default-off")
            assert load()["Catherine"].getboolean("devmenu") == enabled
        for value, expected in (("", 0), ("Auto", 0), ("AUTO", 0), ("0", 0),
                                ("2048", 2048), ("4096", 4096), ("8192", 8192),
                                ("Original", 2048), ("invalid", 0), ("16384", 0), ("-1", 0)):
            INI.write_text("[Catherine]\n" + ("ShadowResolution=" + value + "\n" if value else ""))
            check("shadow-resolution-" + str(expected))
            assert load()["Catherine"]["ShadowResolution"] == (str(expected) if expected else "Auto")
            check("shadow-resolution-" + str(expected))
        for text, expected in (("", True), ("true", True), ("TRUE", True),
                               ("1", True), ("false", False), ("FALSE", False),
                               ("0", False), ("invalid", True)):
            INI.write_text("[Catherine]\n" + ("NativeGpuSkinning=" + text + "\n" if text else ""))
            check("native-skinning-" + str(int(expected)))
            assert load()["Catherine"].getboolean("NativeGpuSkinning", fallback=True) == expected
            if expected:
                assert not load()["Catherine"].get("NativeGpuSkinning")
                assert "; NativeGpuSkinning=false" in INI.read_text()
            check("native-skinning-" + str(int(expected)))
        # First launch supplies the commented switch; subsequent launches leave
        # an already documented default INI alone, including personal comments.
        INI.write_text("[Catherine]\n")
        check("native-skinning-startup")
        text = INI.read_text()
        assert "; Uncomment this line to disable GPU skinning (restart required):\n; NativeGpuSkinning=false" in text
        assert not load()["Catherine"].get("NativeGpuSkinning")
        text += "\n; personal note kept on launch\n"
        INI.write_text(text)
        unchanged = INI.read_bytes()
        check("native-skinning-startup")
        assert INI.read_bytes() == unchanged
        # The documented edit must really activate CPU skinning and survive save.
        INI.write_text(text.replace("; NativeGpuSkinning=false", "NativeGpuSkinning=false", 1))
        check("native-skinning-0")
        assert not load()["Catherine"].getboolean("NativeGpuSkinning")
        # A comment in a different section does not document [Catherine].
        INI.write_text("[Catherine]\n[Game]\n; NativeGpuSkinning=false\n")
        check("native-skinning-startup")
        assert "; NativeGpuSkinning=false" in INI.read_text().split("[Game]")[0]
        check("upscale-policy")
        for value in range(5):
            INI.write_text(f"[Catherine]\nInternalUpscaling={value}\n")
            check(f"upscale-{value}")
            assert load()["Catherine"].getint("InternalUpscaling") == value
            assert "; 2=1920x1080, 3=2560x1440, 4=3840x2160" in INI.read_text()
            check(f"upscale-{value}")
        for invalid in ("oops", "-1", "5"):
            INI.write_text(f"[Catherine]\nInternalUpscaling={invalid}\n")
            check("upscale-1")
        for value in (False, True):
            INI.write_text(f"[Catherine]\nAltBuildMode={str(value).upper()}\n")
            check(f"storage-{int(value)}")
            assert load()["Catherine"].getboolean("AltBuildMode") == value
            check(f"storage-{int(value)}")
        for vsync in (False, True):
            for triple in (False, True):
                INI.write_text(f"[Catherine]\nVSync={str(vsync).upper()}\nTripleBuffering={int(triple)}\n")
                mode = f"presentation-{int(vsync)}{int(triple)}"
                check(mode)
                config = load()["Catherine"]
                assert config.getboolean("VSync") == vsync
                assert config.getboolean("TripleBuffering") == triple
                check(mode)
        INI.write_text("[Catherine]\n")
        check("presentation-00")
        check("cursor-policy")
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
VSync=invalid
TripleBuffering=invalid
AltBuildMode=invalid
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
        check("intro-first")
        check("intro-repeat")
        assert load()["Progress"]["BabelIntroSeen"] == "true"
        assert load()["Progress"]["ColosseumIntroSeen"] == "true"
        assert load()["Progress"]["JokerIntroSeen"] == "true"


        if all(name in protected for name in ("save.dat", "catherine-settings.ini")):
            # Exercise the actual installation's effective values without writing to it.
            (OUT / "installed.original.ini").write_bytes(protected["catherine-settings.ini"])
            INI.write_bytes(protected["catherine-settings.ini"])
            system = protected["save.dat"][0xc8000:0xc8000+0x9810]
            assert len(system) == 0x9810
            (OUT / "legacy-system.bin").write_bytes(system)
            check("migrate-install-copy")
            shutil.copyfile(INI, OUT / "installed.migrated.ini")
        else:
            print("Private installation migration skipped: no local game save/settings; synthetic migration checks still run.")
        # Launcher-managed settings must never rewrite the adjacent ordinary INI.
        ordinary_ini = INI.read_bytes()
        with tempfile.TemporaryDirectory(prefix="launcher data café ", dir=OUT) as directory:
            data_ini = Path(directory) / "catherine-settings.ini"
            data_ini.write_text("[Catherine]\nNativeGpuSkinning=false\nInternalUpscaling=4\n", encoding="utf-8")
            environment = dict(os.environ, RECOMPLAUNCHER_DATA=directory)
            for mode in ("native-skinning-0", "upscale-4"):
                subprocess.run([str(EXE), mode], cwd=OUT, env=environment, check=True, timeout=15)
            assert "NativeGpuSkinning=false" in data_ini.read_text(encoding="utf-8")
            assert INI.read_bytes() == ordinary_ini
        print("Launcher data path keeps settings isolated, including Unicode and spaces.")
    finally:
        EXE.unlink(missing_ok=True)
        assert all((BUILD / k).read_bytes() == v for k,v in protected.items())
    print("Versions 1-7, both input profiles, native settings, locked campaign choices, invalid INI checks passed.")

if __name__ == "__main__":
    main()
