"""Exercise folder setup with real asset copies while preserving the installation."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
B = ROOT / "Build"
EXE = Path(os.environ.get("CATHERINE_SETUP_TEST_EXE", str(B / "asset-setup/publish/CatherineAssetSetup.exe")))


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(root):
    result = subprocess.run([str(EXE), "--root", str(root), "--headless"], timeout=1200)
    if result.returncode:
        raise AssertionError((root / "assets-errors.log").read_text())


def main():
    kind = sys.argv[1]
    root = B / "asset-setup" / ("altmode-real-" + (sys.argv[2] if len(sys.argv) > 2 else kind))
    root.mkdir(exist_ok=False)
    manifest = json.loads((B / "assets-required.json").read_text())
    protected = {name: digest(B / name) for name in ("save.dat", "catherine-settings.ini")}
    pack_stat = (B / "assets.pak").stat()
    if kind == "full":
        os.link(B / "assets.pak", root / "assets.pak")
    elif kind == "inputs":
        needed = {"exefs/" + m["name"] for m in manifest["modules"]} | {s["path"] for s in manifest["shaders"] if s["kind"] == "raw"}
        manifest["files"] = [f for f in manifest["files"] if f["path"] in needed]
        offset = 0
        originals = {}
        for f in manifest["files"]:
            source = Path("C:/catherine/go") / f["path"]
            originals[str(source)] = digest(source)
            target = root / "source" / f["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            if f["romfsOffset"] >= 0:
                f["romfsOffset"] = offset
                offset += f["size"]
        manifest.update(romfsSize=offset, romfsMetadata=[], packageSha256=[], canonicalPackageSha256="")
        (root / "assets-sources.json").write_text(json.dumps([str(root / "source")]))
    else:
        raise ValueError(kind)
    (root / "assets-required.json").write_text(json.dumps(manifest))
    (root / "catherine-settings.ini").write_text("[Catherine]\nAltBuildMode=true\n")
    run(root)
    assert not (root / "assets.pak").exists()
    assert (root / "assets/.catherine-layout.bin").is_file()
    assert not (root / "assets/romfs/blocks").exists()
    assert not (root / "assets/romfs/romfs.bin").exists()
    if kind == "inputs":
        assert not any(p.is_file() for p in (root / "source").rglob("*"))
        assert all(digest(Path(path)) == original for path, original in originals.items())
    with zipfile.ZipFile(B / "assets.pak") as reference:
        for m in manifest["modules"]:
            assert (root / "assets" / (m["name"] + ".image")).read_bytes() == reference.read(m["name"] + ".image")
        # The installer uses the current shader translator; compare its outputs
        # against the existing package's common shaders, reporting any differences.
        same = 0
        for s in manifest["shaders"]:
            name = "spirv/" + s["hash"] + ".spv"
            same += (root / "assets" / name).read_bytes() == reference.read(name)
        print("Module images match; matching shaders:", same, "/", len(manifest["shaders"]), flush=True)
        assert same == len(manifest["shaders"])
    run(root)  # A repeat launch needs neither source files nor a package.
    (root / "catherine-settings.ini").write_text("[Catherine]\nAltBuildMode=false\n")
    run(root)
    assert (root / "assets.pak").is_file() and (root / "assets").is_dir()
    run(root)
    assert "cached package verification" in (root / "assets-setup.log").read_text()
    if kind == "full":
        with zipfile.ZipFile(B / "assets.pak") as original, zipfile.ZipFile(root / "assets.pak") as rebuilt:
            assert set(original.namelist()) == set(rebuilt.namelist())
            for name in original.namelist():
                if name != "catherine-pak.manifest":
                    a, b = original.getinfo(name), rebuilt.getinfo(name)
                    assert a.file_size == b.file_size and a.CRC == b.CRC, name
    assert all(digest(B / name) == value for name, value in protected.items())
    after = (B / "assets.pak").stat()
    assert (after.st_ino, after.st_size, after.st_mtime_ns) == (pack_stat.st_ino, pack_stat.st_size, pack_stat.st_mtime_ns)
    result = dict(passed=True, mode=kind, original_installation_unchanged=True, modules_match=len(manifest["modules"]),
                  shaders_match=len(manifest["shaders"]), extracted_files=len(manifest["files"]), folder_and_rebuilt_pak=str(root))
    (root / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
