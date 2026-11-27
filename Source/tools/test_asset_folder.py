"""Directory/PAK conversion and real native-reader checks on disposable fixtures."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "Build"
SETUP = BUILD / "asset-setup/publish/CatherineAssetSetup.exe"
OUT = BUILD / "asset-setup/folder-tests"
OUT.mkdir(parents=True, exist_ok=True)
NATIVE = OUT / "asset-folder-reader-test.exe"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compile_reader():
    manifest = json.loads((ROOT / "Source/build-manifest.json").read_text())
    unit = next(u for u in manifest["units"] if u["source"].endswith("/asset_pak.cpp"))
    command = unit["command"][:unit["command"].index("-c")]
    if manifest.get("native_release"):
        command += ["-flto=thin", "-fuse-ld=lld"]
    objects = [u["object"] for u in manifest["units"] if "/third_party/miniz/" in u["source"]]
    subprocess.run(command + ["-DCATHERINE_PAK_TESTING", "-UNDEBUG",
        "-I" + str(ROOT / "Source/imports/base"), str(ROOT / "Source/tests/asset_folder_reader_test.cpp"),
        *objects, "-o", str(NATIVE)], check=True, cwd=ROOT)


def fixture(root):
    files = [("romfs/data/first.bin", 128, bytes(range(251)) * 1600),
             ("romfs/data/sub/second.bin", 410000, b"second" * 58000),
             ("romfs/data/last.bin", 800000, b"Z" * 50000)]
    image = bytearray(850123)
    metadata = [(0, b"test RomFS header\0"), (850000, b"M" * 123)]
    for _, offset, data in files:
        image[offset:offset + len(data)] = data
    for offset, data in metadata:
        image[offset:offset + len(data)] = data
    image[80:88] = b"gapdata!"  # Valid source packages can contain nonzero alignment padding.
    modules = {n: bytes(range(256)) for n in ("main", "subsdk0", "subsdk1", "sdk")}
    manifest = dict(version=1, files=[dict(path=n, size=len(data), sha256=sha(data), romfsOffset=o)
        for n, o, data in files], modules=[dict(name=n, size=len(d), imageSha256=sha(d)) for n, d in modules.items()],
        shaders=[dict(hash="0123456789abcdef", kind="raw", path=files[0][0], offset=0, length=20)],
        romfsSize=len(image), romfsMetadata=[dict(offset=o, data=__import__('base64').b64encode(d).decode()) for o, d in metadata],
        packageSha256=[], canonicalPackageSha256="")
    entries = {n + ".image": data for n, data in modules.items()}
    entries.update({f"romfs/blocks/{i // 262144:06d}.bin": image[i:i + 262144] for i in range(0, len(image), 262144)})
    spv = struct.pack("<5I", 0x07230203, 0x10000, 0, 1, 0)
    entries.update({"spirv/0123456789abcdef.spv": spv, "spirv/results.tsv": b"hash\tresources\n",
        "spirv/extra.original.spv": spv, "extraction-manifest.json": b'{"source":"fixture"}',
        "romfs/index.json": b"[]"})
    write_pack(root / "assets.pak", entries, len(image))
    manifest["packageSha256"] = [sha((root / "assets.pak").read_bytes())]
    (root / "assets-required.json").write_text(json.dumps(manifest))
    (root / "romfs-reference.bin").write_bytes(image)
    return manifest, entries, files


def write_pack(path, entries, size):
    records = []
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries.items():
            z.writestr(name, data)
            name_bytes = name.encode()
            records.append((name, struct.pack("<H", len(name_bytes)) + name_bytes + struct.pack("<QIH", len(data), zlib.crc32(data), 8)))
        crc = zlib.crc32(b"".join(r for _, r in sorted(records)))
        z.writestr("catherine-pak.manifest", f"CATHERINE_PAK 1\nROMFS_SIZE {size}\nBLOCK_SIZE 262144\nENTRY_COUNT {len(entries)}\nDIRECTORY_CRC32 {crc:08x}\n")


def mode(root, value):
    (root / "catherine-settings.ini").write_text(f"[cAtHeRiNe]\naltbuildmode={value}\n")


def setup(root, *args, ok=True):
    result = subprocess.run([str(SETUP), "--root", str(root), "--headless", *map(str, args)], capture_output=True, timeout=60)
    if ok and result.returncode:
        raise AssertionError((root / "assets-errors.log").read_text())
    if not ok:
        assert result.returncode != 0, "Invalid conversion unexpectedly succeeded"


def native(root, what="folder"):
    path = root / "assets.pak" if what == "pak" else root / "assets"
    subprocess.run([str(NATIVE), what, str(path), str(root / "romfs-reference.bin")], check=True, cwd=root)


def main():
    compile_reader()
    with tempfile.TemporaryDirectory(dir=OUT) as temporary:
        base = Path(temporary)
        root = base / "roundtrip"; root.mkdir()
        manifest, entries, files = fixture(root)
        native(root, "pak")
        mode(root, "TrUe")
        setup(root)
        assert not (root / "assets.pak").exists()
        assert not (root / "assets/romfs/blocks").exists()
        assert not (root / "assets/romfs/romfs.bin").exists()
        for name, _, data in files:
            assert (root / "assets" / name).read_bytes() == data
        for name in entries:
            if not name.startswith("romfs/blocks/"):
                assert (root / "assets" / name).read_bytes() == entries[name]
        native(root)
        setup(root)  # Folder-only launch: no source prompts and no PAK reconstruction.
        assert not (root / "assets.pak").exists()
        mode(root, "false")
        setup(root)
        assert (root / "assets").is_dir()
        native(root, "pak")
        with zipfile.ZipFile(root / "assets.pak") as z:
            assert z.testzip() is None
            for name, data in entries.items():
                assert z.read(name) == data, name
        setup(root)  # A rebuilt package is valid, including on the next launch.
        assert "cached package verification" in (root / "assets-setup.log").read_text()
        mode(root, "true")
        setup(root)  # Existing matching folder can be reused/merged safely.
        assert not (root / "assets.pak").exists()
        native(root)
        # Same-size module corruption is caught by the native reader's CRC.
        module = root / "assets/main.image"; before = module.read_bytes()
        module.write_bytes(bytes([before[0] ^ 1]) + before[1:])
        native(root, "badmodule")
        mode(root, "false"); setup(root, ok=False)
        assert not (root / "assets.pak").exists()
        module.write_bytes(before)
        # Missing/truncated raw assets never become a replacement PAK.
        raw = root / "assets" / files[0][0]; original = raw.read_bytes(); raw.write_bytes(original[:-1])
        setup(root, ok=False); assert not (root / "assets.pak").exists()
        native(root, "invalid"); raw.write_bytes(original)

        explicit = base / "explicit"; explicit.mkdir()
        fixture(explicit); mode(explicit, "true")
        before = (explicit / "assets.pak").read_bytes()
        setup(explicit, "--packed")
        assert (explicit / "assets.pak").read_bytes() == before and not (explicit / "assets").exists()

        corrupt = base / "corrupt"; corrupt.mkdir()
        m, e, _ = fixture(corrupt); mode(corrupt, "true")
        # Even a self-consistent ZIP CRC/directory cannot hide corrupt game data.
        block = bytearray(e["romfs/blocks/000000.bin"]); block[200] ^= 1
        e["romfs/blocks/000000.bin"] = block
        write_pack(corrupt / "assets.pak", e, m["romfsSize"])
        before = (corrupt / "assets.pak").read_bytes()
        setup(corrupt, ok=False)
        assert (corrupt / "assets.pak").read_bytes() == before
        assert not (corrupt / "assets/.catherine-folder.json").exists()
        assert not list(corrupt.glob(".assets-extract-*"))

        conflict = base / "conflict"; conflict.mkdir()
        _, _, f = fixture(conflict); mode(conflict, "1")
        target = conflict / "assets" / f[0][0]; target.parent.mkdir(parents=True); target.write_bytes(b"do not replace")
        pack_before = (conflict / "assets.pak").read_bytes()
        setup(conflict, ok=False)
        assert target.read_bytes() == b"do not replace" and (conflict / "assets.pak").read_bytes() == pack_before

        # First-launch setup moves only validated sources, retaining their paths.
        fresh = base / "fresh"; fresh.mkdir(); source = base / "source"; source.mkdir()
        data = {"romfs/a.bin": b"A" * 120, "romfs/nested/b.bin": b"B" * 80, "romfs/left/same.bin": b"S" * 32, "romfs/right/same.bin": b"S" * 32}
        m = dict(version=1, files=[dict(path=n, size=len(d), sha256=sha(d), romfsOffset=i * 150)
            for i, (n, d) in enumerate(data.items())], modules=[], shaders=[], romfsSize=600, romfsMetadata=[], packageSha256=[])
        (fresh / "assets-required.json").write_text(json.dumps(m)); mode(fresh, "true")
        for name, content in data.items():
            (source / Path(name).name).write_bytes(content)
        (source / "unrelated.txt").write_text("keep")
        setup(fresh, "--scan", source, "--scan-only")
        assert all((source / Path(n).name).exists() for n in data)
        (source / "b.bin").write_bytes(b"wrong")
        setup(fresh, "--scan", source, ok=False)
        assert (source / "a.bin").exists()
        (source / "b.bin").write_bytes(data["romfs/nested/b.bin"])
        # Resume after one file was already moved by an interrupted installation.
        first = fresh / "assets/romfs/a.bin"; first.parent.mkdir(parents=True)
        shutil.move(source / "a.bin", first)
        setup(fresh, "--scan", source)
        assert not (fresh / "assets.pak").exists()
        for name, content in data.items():
            assert (fresh / "assets" / name).read_bytes() == content
            assert not (source / Path(name).name).exists()
        assert (source / "unrelated.txt").read_text() == "keep"
        mode(fresh, "false"); setup(fresh)
        assert (fresh / "assets.pak").exists()
        assert all((fresh / "assets" / n).exists() for n in data)

    print("Folder round-trip, native reads, extras, validated moves, interrupted-import resume, corrupt PAK preservation, collision preservation and missing-file checks passed.")


if __name__ == "__main__":
    main()
