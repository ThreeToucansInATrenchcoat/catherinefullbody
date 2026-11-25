"""Translate local Catherine SPIR-V shaders to DXIL and record D3D12 bindings."""

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


PROFILES = {
    "vert": "vs_6_0",
    "frag": "ps_6_0",
    "comp": "cs_6_0",
}
SHADER_NAME = re.compile(r"[0-9a-f]{16}\.spv")
BINDING = re.compile(r"(cb|[tus])(\d+)(?:,space(\d+))?")
BINDING_KINDS = {"cb": "cbv", "t": "srv", "u": "uav", "s": "sampler"}
STAGE_IDS = {"vert": 0, "frag": 1, "comp": 2}
RANGE_IDS = {"srv": 0, "uav": 1, "cbv": 2, "sampler": 3}


def invoke(tool, *args):
    result = subprocess.run(
        [tool, *map(str, args)], capture_output=True, encoding="utf-8", errors="replace"
    )
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}")
    return result


def dxil_bindings(assembly):
    # DXC's listing contains the live bindings after optimization. SPIRV-Cross
    # reflection keeps original NVN names, including bindings DXC eliminated.
    lines = iter(assembly.splitlines())
    for line in lines:
        if line.strip() == "; Resource Bindings:":
            break
    else:
        raise ValueError("DXC output has no Resource Bindings table")
    for line in lines:
        if "HLSL Bind" in line:
            break
    else:
        raise ValueError("DXC output has no HLSL Bind column")
    if not next(lines, "").startswith("; -"):
        raise ValueError("Cannot locate the DXC resource table separator")
    bindings = []
    for line in lines:
        row = line.removeprefix(";").split()
        if not row:
            return bindings
        if len(row) != 7 or not (match := BINDING.fullmatch(row[-2])):
            raise ValueError(f"Unrecognized DXC resource row: {line}")
        kind, index, space = match.groups()
        bindings.append(
            {
                "name": row[0],
                "kind": BINDING_KINDS[kind],
                "register": int(index),
                "space": int(space or 0),
                "count": int(row[-1]) if row[-1] != "unbounded" else None,
            }
        )
    raise ValueError("DXC resource table ended unexpectedly")


def translate(source, out, cross, dxc, vertex_clip):
    result = {"hash": source.stem}
    step = "source read"
    try:
        spirv = source.read_bytes()
        digest = hashlib.sha256(spirv).digest()
        result["spirv_sha256"] = digest.hex()
        with tempfile.TemporaryDirectory(prefix=f".{source.stem}-", dir=out) as temp:
            work = Path(temp)
            snapshot = work / "shader.spv"
            snapshot.write_bytes(spirv)
            step = "reflection"
            reflection = json.loads(invoke(cross, snapshot, "--reflect").stdout)
            entries = reflection["entryPoints"]
            if len(entries) != 1 or entries[0]["name"] != "main":
                raise ValueError("Expected one entry point named main")
            stage = entries[0]["mode"]
            result["stage"] = stage
            profile = PROFILES.get(stage)
            if not profile:
                raise ValueError(f"Unsupported execution model {stage}")
            result["profile"] = profile

            flags = ["--hlsl", "--shader-model", "60"]
            if stage == "vert":
                if vertex_clip is None:
                    raise ValueError("Vertex conversion requires --triangle-vertex-clip")
                # NVN declares PointSize even for triangle draws. This mode
                # drops the point-size output and is only valid for triangles.
                flags.append("--hlsl-enable-compat")
                if vertex_clip == "nvn":
                    flags.append("--fixup-clipspace")

            step = "HLSL translation"
            hlsl = work / "shader.hlsl"
            dxil = work / "shader.dxil"
            asm = work / "shader.asm"
            invoke(cross, snapshot, *flags, "--output", hlsl)
            step = "DXC compilation"
            compiled = invoke(dxc, "-T", profile, "-E", "main", "-Fo", dxil, "-Fc", asm, hlsl)
            step = "DXIL reflection"
            bindings = dxil_bindings(asm.read_text(encoding="utf-8", errors="replace"))
            if any(binding["count"] is None for binding in bindings):
                raise ValueError("Unbounded descriptor ranges require a dedicated binding layout")
            step = "shader packaging"
            payload = dxil.read_bytes()
            if len(bindings) > 4096 or len(payload) > 64 * 1024 * 1024:
                raise ValueError("Shader exceeds the native loader limits")
            package = work / "shader.dx12shader"
            with package.open("wb") as file:
                file.write(struct.pack("<4sIII32s", b"CDX1", STAGE_IDS[stage], len(bindings), len(payload), digest))
                for binding in bindings:
                    file.write(struct.pack("<IIII", RANGE_IDS[binding["kind"]], binding["register"],
                                           binding["space"], binding["count"]))
                file.write(payload)
            package.rename(out / f"{source.stem}.dx12shader")
            result.update(
                status="ok",
                bindings=bindings,
                reflection=reflection,
            )
            if compiled.stderr.strip():
                result["diagnostics"] = compiled.stderr.strip()
    except (ValueError, OSError, KeyError, struct.error) as error:
        result.update(status="failed", step=step, diagnostics=str(error))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Local directory of Catherine .spv files")
    parser.add_argument("--out", type=Path, required=True, help="New directory for shader packages and their manifest")
    parser.add_argument("--shader", action="append", help="Convert this hash only. Repeat to select several")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--dxc", default="dxc")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument(
        "--triangle-vertex-clip",
        choices=("nvn", "zero-to-one"),
        help="Enable triangle-only vertex translation with raw NVN or already corrected clip Z",
    )
    parser.add_argument("--allow-incomplete", action="store_true", help="Return success for a partial census")
    args = parser.parse_args()
    cross = shutil.which(args.spirv_cross)
    dxc = shutil.which(args.dxc)
    if not cross or not dxc:
        parser.error("SPIRV-Cross and DXC must be installed or supplied by absolute path")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    source_dir = args.source.resolve()
    if args.shader:
        names = sorted(set(f"{shader}.spv" for shader in args.shader))
        if any(not SHADER_NAME.fullmatch(name) for name in names):
            parser.error("--shader requires a lowercase 16-digit Catherine shader hash")
        sources = [source_dir / name for name in names]
        missing = [source.name for source in sources if not source.is_file()]
        if missing:
            parser.error("Missing selected shaders: " + ", ".join(missing))
    else:
        sources = sorted(source for source in source_dir.glob("*.spv") if SHADER_NAME.fullmatch(source.name))
    if not sources:
        parser.error(f"No Catherine shader files found in {source_dir}")
    output = args.out.resolve()
    if output == source_dir or source_dir in output.parents:
        parser.error("--out must be outside the SPIR-V source directory")
    try:
        output.mkdir(parents=True)
    except FileExistsError:
        parser.error(f"Output directory already exists: {output}")

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(
            pool.map(
                lambda source: translate(source, output, cross, dxc, args.triangle_vertex_clip),
                sources,
            )
        )
    counts = Counter((row.get("stage", "unknown"), row["status"]) for row in results)
    summary = {
        stage: {status: counts[(stage, status)] for status in ("ok", "failed")}
        for stage in sorted({key[0] for key in counts})
    }
    manifest = {
        "triangle_vertex_clip": args.triangle_vertex_clip,
        "summary": summary,
        "shaders": results,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return int(not args.allow_incomplete and any(row["status"] == "failed" for row in results))


if __name__ == "__main__":
    raise SystemExit(main())
