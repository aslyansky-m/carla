#!/usr/bin/env python3
"""Launch a cooked UE5 package with the opt-in legacy-map weather bridge."""
import argparse
import os
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", help="For example Town03_Opt or Town12/Town12")
    parser.add_argument("--package-root", required=True, type=Path)
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--graphics-adapter", type=int, default=0,
                        help="Vulkan adapter index (not necessarily the CUDA device index)")
    parser.add_argument("--native-sky", action="store_true",
                        help="Limited sun/fog fallback instead of the modern sky")
    args, extra = parser.parse_known_args()
    package = args.package_root.resolve()
    binary = package / "CarlaUnreal/Binaries/Linux/CarlaUnreal"
    relative = args.map.removeprefix("/Game/Carla/Maps/")
    map_path = Path(relative)
    if map_path.is_absolute() or ".." in map_path.parts:
        parser.error("Map must be relative to /Game/Carla/Maps")
    if not binary.is_file():
        parser.error(f"Executable is missing: {binary}")
    if not (package / "CarlaUnreal/Content/Carla/Maps" / map_path).with_suffix(".umap").is_file():
        parser.error(f"Cooked map is missing: {relative}")
    commands = ["r.LocalExposure 0"]
    remaining = []
    for argument in extra:
        if argument.lower().startswith("-execcmds="):
            commands.append(argument.split("=", 1)[1])
        else:
            remaining.append(argument)
    command = [
        str(binary), "CarlaUnreal", "/Game/Carla/Maps/" + relative,
        "-RenderOffScreen", "-nosound", "-unattended", "-NoSplash", "-NoCrashDialog",
        f"-carla-rpc-port={args.port}", f"-graphicsadapter={args.graphics_adapter}",
        "-ini:Engine:[Kismet]:bForceDisableCookedDependencyDeferring=True,"
        "[/Script/Engine.RendererSettings]:r.RayTracing=False,"
        "[/Script/Engine.RendererSettings]:r.Lumen.HardwareRayTracing=False",
        "-carla-force-native-sky" if args.native_sky else "-carla-legacy-lighting",
    ]
    if not args.native_sky:
        command += ["-carla-legacy-modern-sky", "-carla-modern-weather-bridge"]
    command += ["-ExecCmds=" + ",".join(commands), *remaining]
    print("Starting", relative, "on Vulkan adapter", args.graphics_adapter, flush=True)
    os.chdir(package)
    os.execv(str(binary), command)


if __name__ == "__main__":
    main()
