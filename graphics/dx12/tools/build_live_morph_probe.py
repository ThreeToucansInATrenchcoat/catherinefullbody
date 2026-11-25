"""Relink an isolated Catherine candidate with live D3D12 morph validation."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


DECLARATION = '''
#include <cstdint>

extern "C" long catherine_dx12_morph_probe(const char*, uint32_t, uint32_t, float,
                                          const uint8_t*, const uint8_t*, uint8_t*) noexcept;
'''
ANCHOR = """        run_cpu_morph(reinterpret_cast<const float*>(src_bytes), initial, destination, count, weight,
                      dest_stride / 4, base_stride / 4);"""
BEFORE = """
        static const bool probe_dx12 = std::getenv("CATHERINE_DX12_MORPH_PROBE_DIR") != nullptr;
        static unsigned probe_attempts = 0;
        std::vector<uint8_t> dx12_output;
        long dx12_status = 1;
        if (probe_dx12 && probe_attempts < 8 && (weight_bits & 0x7fffffffu)) {
            ++probe_attempts;
            const auto* bytes = reinterpret_cast<const uint8_t*>(destination);
            dx12_output.assign(bytes, bytes + dst_used);
            dx12_status = catherine_dx12_morph_probe(cs.hash.c_str(), count, dest_stride, weight,
                                                    src_bytes, separate_base, dx12_output.data());
        }
"""
AFTER = """
        if (!dx12_output.empty()) {
            const bool equal = dx12_status == 0 && std::memcmp(dx12_output.data(), destination, dst_used) == 0;
            if (equal) std::memcpy(destination, dx12_output.data(), dst_used);
            log("DX12 morph live: %s shader=%s vertices=%u stride=%u bytes=%llu hr=0x%08lx",
                equal ? "MATCH" : "FALLBACK", cs.hash.c_str(), count, dest_stride,
                static_cast<unsigned long long>(dst_used), static_cast<unsigned long>(dx12_status));
        }
"""


def sha(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def quoted(arg):
    return f'"{arg}"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-rsp", type=Path, required=True)
    parser.add_argument("--writer-receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="New isolated build directory")
    args = parser.parse_args()
    baseline = args.baseline_rsp.resolve(strict=True)
    output = args.out.resolve()
    output.mkdir(parents=True)

    receipt = json.loads(args.writer_receipt.read_text(encoding="utf-8"))
    compile_writer = receipt["compile"][0].copy()
    source_index = compile_writer.index("-c") + 1
    object_index = compile_writer.index("-o") + 1
    source = Path(compile_writer[source_index])
    original_object = Path(compile_writer[object_index])
    if sha(source) != receipt["source_sha256"][str(source)] or sha(original_object) != receipt["object_sha256"]:
        raise ValueError("The NVN writer does not match the recorded source and object")
    source_object = quoted(original_object.as_posix())
    response = baseline.read_text(encoding="utf-8")
    if response.count(source_object) != 1:
        raise ValueError("The response file does not select the verified NVN writer")
    text = source.read_text(encoding="utf-8")
    if text.count(ANCHOR) != 1:
        raise ValueError("Expected one CPU morph call in the verified writer")

    patched = output / source.name
    patched.write_text(DECLARATION + text.replace(ANCHOR, BEFORE + ANCHOR + "\n" + AFTER), encoding="utf-8")
    writer = output / "nvn_written_dx12_probe.obj"
    compile_writer[source_index] = str(patched)
    compile_writer[object_index] = str(writer)
    module = Path(__file__).resolve().parents[1]
    compiler = compile_writer[0]
    objects = []
    commands = [compile_writer]
    for name in ("renderer.cpp", "binding_layout.cpp", "shader_binary.cpp", "morph_replay.cpp", "native_replay.cpp",
                 "integration/live_morph_probe.cpp"):
        path = module / name
        target = output / (path.stem + ".obj")
        objects.append(quoted(target.as_posix()))
        commands.append([compiler, "-std=c++20", "-O2", "-DNDEBUG", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN",
                         "-fms-runtime-lib=static", "-I" + str(module), "-c", str(path), "-o", str(target)])

    rsp = output / "Catherine-dx12-probe.rsp"
    response = response.replace(source_object, quoted(writer.as_posix()))
    lines = []
    for line in response.splitlines():
        flag = line.strip('"')
        if flag.startswith("-Wl,"):
            options = [
                option for option in flag[4:].split(",")
                if not option.lower().startswith(("/map:", "/pdb:"))
            ]
            if options:
                lines.append(quoted("-Wl," + ",".join(options)))
        else:
            lines.append(line)
    lines.extend(objects)
    lines.extend(["-ld3d12", "-ld3dcompiler", "-ldxgi", quoted("-Wl,/map:" + (output / "Catherine.map").as_posix()),
                  quoted("-Wl,/pdb:" + (output / "Catherine.pdb").as_posix())])
    rsp.write_text("\n".join(lines) + "\n", encoding="utf-8")
    exe = output / "Catherine.exe"
    commands.append([compiler, "@" + str(rsp), "-o", str(exe)])
    with (output / "build.log").open("wb") as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, cwd=output)
    provenance = {
        "baseline_rsp": str(baseline), "baseline_rsp_sha256": sha(baseline),
        "writer_source": str(source), "writer_source_sha256": sha(source), "writer_object_sha256": sha(original_object),
        "candidate": str(exe), "candidate_sha256": sha(exe), "commands": commands,
    }
    (output / "build.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(provenance["candidate_sha256"])


if __name__ == "__main__":
    main()
