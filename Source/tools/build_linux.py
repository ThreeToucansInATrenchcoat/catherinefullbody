"""Compile the active Windows recipe's source set with the native Linux ABI."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import struct


WINDOWS_ROOT = "C:/catherine/recomp/"
WINDOWS_VULKAN_INCLUDE = "-IC:/recomp/wetrix_dc/VulkanSDK/Include"
WINDOWS_FLAGS = {
    "--target=x86_64-pc-windows-msvc",
    "-fms-runtime-lib=static",
    "-DVK_USE_PLATFORM_WIN32_KHR",
    "-mno-incremental-linker-compatible",
    "-fasync-exceptions",
}
WINDOWS_LIBRARIES = {
    "-lsynchronization", "-lbcrypt", "-luser32", "-lgdi32", "-lshell32",
    "-lxinput", "-lxaudio2", "-lole32", "-lmfplat", "-lmfuuid", "-lwmcodecdspuuid",
}


def localize(arg, root):
    return arg.replace(WINDOWS_ROOT, f"{root.as_posix()}/")


def make_overlays(root, manifest, output):
    overlays = {}
    for unit in manifest["units"]:
        args = unit["command"]
        for index, arg in enumerate(args):
            if arg != "-ivfsoverlay":
                continue
            path = args[index + 1]
            if path in overlays:
                continue
            source = Path(localize(path, root))
            dest = output / f"{len(overlays):02}-{source.name}"
            dest.write_text(localize(source.read_text(encoding="utf-8"), root), encoding="utf-8")
            overlays[path] = str(dest)
    return overlays


def native_command(unit, root, objects, overlays, compilers, vulkan_include, ffmpeg_environment):
    recipe = unit["command"]
    cplusplus = Path(unit["source"]).suffix in {".cpp", ".cxx", ".cc"}
    command = [compilers[cplusplus], "-pthread", "-march=x86-64-v3"]
    command += ["-I", str(objects / "include")]
    if Path(unit["source"]).name == "movie_decoder.cpp":
        command += subprocess.check_output(
            ["pkg-config", "--cflags", "libavcodec", "libavutil", "libswscale"], text=True, env=ffmpeg_environment
        ).split()
    if not cplusplus:
        command += ["-std=gnu17"]
    if vulkan_include:
        command += ["-I", vulkan_include]
    # Clang's Linux driver canonicalizes the absolute source paths used by the VFS.
    command.extend(
        overlays.get(arg, localize(arg, root))
        for arg in recipe[1:]
        if arg not in WINDOWS_FLAGS and arg not in (WINDOWS_VULKAN_INCLUDE, "-march=x86-64-v3")
    )
    obj = objects / (Path(unit["object"]).stem + ".o")
    command[command.index("-o") + 1] = str(obj)
    return command, obj


def game_icon_png(icon_source):
    ico = icon_source.read_bytes()
    count = struct.unpack_from("<H", ico, 4)[0]
    pngs = []
    for index in range(count):
        width, height, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", ico, 6 + 16 * index)
        payload = ico[offset:offset + size]
        if payload.startswith(b"\x89PNG\r\n\x1a\n") and len(payload) == size:
            pngs.append(((width or 256) * (height or 256), payload))
    if not pngs:
        raise SystemExit(f"No PNG image in {icon_source}.")
    return max(pngs, key=lambda entry: entry[0])[1]


def install_desktop_files(icon_png, build):
    icon = build / "catherine.png"
    icon.write_bytes(icon_png)

    executable = str(build / "catherine")
    escaped = "".join("\\\\" + char if char in '\\"$`' else "%%" if char == "%" else char
                      for char in executable)
    launcher = build / "Catherine.desktop"
    launcher.write_text(
        "[Desktop Entry]\nType=Application\nName=Catherine Full Body\n"
        f'Exec="{escaped}"\nIcon={icon}\nTerminal=false\nCategories=Game;\n',
        encoding="utf-8",
    )
    launcher.chmod(0o755)


def build_setup_payload(root, build, objects, logs, compiler, dotnet, icon_png):
    setup = build / "asset-setup-linux"
    publish = setup / "embedded"
    environment = dict(os.environ, DOTNET_CLI_HOME=str(setup), DOTNET_CLI_TELEMETRY_OPTOUT="1",
                       DOTNET_GENERATE_ASPNET_CERTIFICATE="false")
    command = [
        dotnet, "publish", str(root / "Source/asset_setup_linux/AssetSetupLinux.csproj"),
        "-c", "Release", "--self-contained", "-r", "linux-x64", "-o", str(publish), "--nologo",
        "-p:PublishSingleFile=true", "-p:IncludeNativeLibrariesForSelfExtract=true",
        "-p:EnableCompressionInSingleFile=true",
    ]
    result = subprocess.run(command, capture_output=True, text=True, env=environment, cwd=root)
    (logs / "asset-setup-build.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit(f"Linux asset installer build failed. See {logs / 'asset-setup-build.log'}.")
    executable = (publish / "CatherineAssetSetup").relative_to(root)
    icon = objects / "catherine-window.png"
    icon.write_bytes(icon_png)
    assembly = objects / "asset-setup-embedded.s"
    assembly.write_text(
        '.section .rodata\n'
        '.globl catherine_asset_setup_start, catherine_asset_setup_end\n'
        '.hidden catherine_asset_setup_start, catherine_asset_setup_end\n'
        'catherine_asset_setup_start:\n'
        f'.incbin {json.dumps(executable.as_posix())}\n'
        'catherine_asset_setup_end:\n'
        '.globl catherine_game_icon_start, catherine_game_icon_end\n'
        '.hidden catherine_game_icon_start, catherine_game_icon_end\n'
        'catherine_game_icon_start:\n'
        f'.incbin {json.dumps(icon.relative_to(root).as_posix())}\n'
        'catherine_game_icon_end:\n'
        '.section .note.GNU-stack,"",@progbits\n'
    )
    obj = assembly.with_suffix(".o")
    subprocess.run([compiler, "-c", str(assembly), "-o", str(obj)], cwd=root, check=True)
    return obj


def run_linux(root, manifest, options):
    if options.pgo:
        raise SystemExit("Linux builds do not yet support the Windows PGO recipe.")
    output = options.output or "catherine"
    if output not in {"catherine", "catherine1", "catherine2"}:
        raise SystemExit("Linux output must be catherine, catherine1 or catherine2.")
    build = root / "Build"
    local_dotnet = build / "dotnet-sdk" / "dotnet"
    dotnet = str(local_dotnet) if local_dotnet.is_file() else shutil.which("dotnet")
    if not dotnet and not options.check_dependencies:
        raise SystemExit("The .NET 10 SDK is required to embed the Linux asset installer.")
    ffmpeg_prefix = build / "ffmpeg" / "install"
    ffmpeg_lib = ffmpeg_prefix / "lib"
    bundled_ffmpeg = (ffmpeg_lib / "pkgconfig/libavcodec.pc").is_file()
    ffmpeg_environment = dict(os.environ)
    if bundled_ffmpeg:
        ffmpeg_environment["PKG_CONFIG_LIBDIR"] = str(ffmpeg_lib / "pkgconfig")
        ffmpeg_environment.pop("PKG_CONFIG_PATH", None)
    elif os.environ.get("CATHERINE_REQUIRE_BUNDLED_FFMPEG") == "1":
        raise SystemExit("Build the bundled FFmpeg with Source/tools/build_bundled_ffmpeg.sh first.")
    objects = build / "obj-linux"
    overlays_dir = objects / "vfs"
    logs = build / "logs" / "linux"
    overlays_dir.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    compilers = (os.environ.get("CC", "clang"), os.environ.get("CXX", "clang++"))
    for compiler in compilers:
        if not shutil.which(compiler):
            raise SystemExit(f"Native compiler unavailable: {compiler}")
    vulkan_include = os.environ.get("CATHERINE_VULKAN_INCLUDE")
    if vulkan_include and not (Path(vulkan_include) / "vulkan/vulkan.h").is_file():
        raise SystemExit("CATHERINE_VULKAN_INCLUDE must point to the parent of vulkan/vulkan.h.")
    # A few selected units still refer to other project files by the Windows
    # name in an include directive. Map that name inside the build directory.
    include = objects / "include" / WINDOWS_ROOT
    include.mkdir(parents=True, exist_ok=True)
    source_link = include / "Source"
    if not source_link.is_symlink():
        source_link.symlink_to(root / "Source", target_is_directory=True)
    overlays = make_overlays(root, manifest, overlays_dir)

    def compile_unit(unit):
        command, obj = native_command(unit, root, objects, overlays, compilers, vulkan_include, ffmpeg_environment)
        if options.check_dependencies:
            command.remove("-c")
            index = command.index("-o")
            del command[index:index + 2]
            command += ["-M", "-MF", str(logs / f"{obj.stem}.d"), "-MT", str(obj)]
        else:
            depfile = obj.with_suffix(".d")
            recipe_stamp = obj.with_suffix(".command")
            command += ["-MMD", "-MF", str(depfile), "-MT", "catherine_unit"]
            if (
                not options.rebuild and obj.is_file() and depfile.is_file()
                and recipe_stamp.is_file() and recipe_stamp.read_text() == shlex.join(command)
            ):
                dependencies = depfile.read_text().replace(chr(92) + chr(10), " ").partition(":")[2]
                sources = set(shlex.split(dependencies))
                # Retain the physical backing files of the Windows VFS recipe as
                # well as the headers actually reached by the Linux preprocessor.
                sources.update(localize(path, root) for path in unit["dependencies"])
                recipe = unit["command"]
                sources.update(localize(recipe[i + 1], root)
                               for i, arg in enumerate(recipe) if arg == "-ivfsoverlay")
                mtime = obj.stat().st_mtime_ns
                if dependencies and all(path.is_file() and path.stat().st_mtime_ns <= mtime
                                        for path in [Path(__file__), *map(Path, sources)]):
                    return None
        completed = subprocess.run(command, cwd=root, capture_output=True, text=True, errors="replace")
        log = logs / f"{obj.stem}.log"
        log.write_text(shlex.join(command) + "\n" + completed.stdout + completed.stderr)
        if completed.returncode:
            if not options.check_dependencies:
                obj.unlink(missing_ok=True)
                depfile.unlink(missing_ok=True)
            return {"id": unit["id"], "source": unit["source"][len(WINDOWS_ROOT):], "log": str(log)}
        if not options.check_dependencies:
            obj.with_suffix(".command").write_text(shlex.join(command))
        return None

    with ThreadPoolExecutor(max_workers=options.jobs) as pool:
        failures = [result for result in pool.map(compile_unit, manifest["units"]) if result]
    (logs / "failures.json").write_text(json.dumps(failures, indent=2) + "\n")
    if failures:
        total = len(manifest["units"])
        action = "dependency checks passed" if options.check_dependencies else "units compiled"
        raise SystemExit(f"{total - len(failures)}/{total} {action}. See {logs / 'failures.json'}.")
    if options.check_dependencies:
        print("All Linux source dependencies resolve.")
        return

    icon_png = game_icon_png(root / "Source/resources/catherine.ico")
    payload = build_setup_payload(root, build, objects, logs, compilers[0], dotnet, icon_png)
    rsp = build / "Catherine.rsp"
    args = [line.strip().strip('"') for line in rsp.read_text().splitlines() if line.strip()]
    link = [compilers[1], "-pthread"]
    for arg in args:
        if arg in WINDOWS_FLAGS | WINDOWS_LIBRARIES or arg.startswith("-Wl,/") or arg.endswith(".res"):
            continue
        link.append(str(objects / (Path(arg).stem + ".o")) if arg.endswith(".obj") else arg)
    # Link to a fresh output so a failed link preserves any previous build.
    temporary = objects / (output + ".link")
    if bundled_ffmpeg:
        # RPATH also locates dependencies between the bundled libraries.
        link += ["-L" + str(ffmpeg_lib), "-Wl,--disable-new-dtags,-rpath,$ORIGIN/ffmpeg-runtime"]
    link += [str(payload), "-ldl", "-lavcodec", "-lavutil", "-lswscale", "-lSDL2", "-lcrypto", "-lcairo", "-o", str(temporary)]
    result = subprocess.run(link, cwd=root, capture_output=True, text=True, errors="replace")
    (logs / "link.log").write_text(shlex.join(link) + "\n" + result.stdout + result.stderr)
    if result.returncode:
        temporary.unlink(missing_ok=True)
        raise SystemExit(f"Linux link failed. See {logs / 'link.log'}.")
    if bundled_ffmpeg:
        runtime = build / "ffmpeg-runtime"
        runtime.mkdir(exist_ok=True)
        for library in ("avcodec", "avutil", "swscale"):
            files = [path for path in ffmpeg_lib.glob(f"lib{library}.so.*") if path.name.count(".") == 2]
            if len(files) != 1:
                raise SystemExit(f"Expected one bundled {library} SONAME in {ffmpeg_lib}.")
            shutil.copy2(files[0], runtime / files[0].name)
    temporary.replace(build / output)
    shutil.copytree(root / "Source" / "CathAssets", build / "CathAssets", dirs_exist_ok=True)
    shutil.copy2(root / "Source" / "resources" / "assets-required.json", build)
    if output == "catherine":
        install_desktop_files(icon_png, build)
    print(f"Built {build / output}")
