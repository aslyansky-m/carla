"""Exercise native cache rejection against an owned CARLA 0.10 server.

Run each case in a separate process. No shared/package cache is modified.
The matching patched Python wheel is required; the old wheel may crash.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import time


def cache_variant(data, case):
    data = bytearray(data)
    if case == "valid":
        return data
    if case == "truncated":
        return data[:-1]
    if case == "oversized-count":
        struct.pack_into("<I", data, 0, 0xffffffff)
    elif case == "missing-waypoint":
        struct.pack_into("<I", data, 12, 0xffffffff)
    elif case == "dangling-link":
        offset = 4
        count, = struct.unpack_from("<I", data)
        for _ in range(count):
            offset += 24
            following, = struct.unpack_from("<H", data, offset)
            offset += 2
            if following:
                struct.pack_into("<Q", data, offset, 0xffffffffffffffff)
                return data
            previous, = struct.unpack_from("<H", data, offset)
            offset += 2 + previous * 8 + 22
        raise RuntimeError("Fixture has no next-waypoint links")
    else:
        raise ValueError(case)
    return data


def main():
    import carla

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--tm-port", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=[
        "valid", "stale", "truncated", "oversized-count", "missing-waypoint", "dangling-link"])
    parser.add_argument("--stale-cache", type=Path)
    args = parser.parse_args()
    if args.case == "stale" and args.stale_cache is None:
        parser.error("--case stale requires --stale-cache")
    args.output.mkdir(parents=True, exist_ok=False)
    client = carla.Client("127.0.0.1", args.port)
    client.set_timeout(120)
    if [client.get_client_version(), client.get_server_version()] != ["0.10.0", "0.10.0"]:
        raise RuntimeError("Expected matching 0.10.0 client and server")
    world = client.get_world()
    world_map = world.get_map()
    root = Path(tempfile.mkdtemp(prefix="private-cache-", dir=args.output))
    if not client.set_files_base_folder(str(root.resolve())):
        raise RuntimeError("Cannot select private file cache")
    required = list(client.get_required_files("TM", False))
    if len(required) != 1:
        raise RuntimeError(f"Unexpected cache paths: {required}")
    relative = Path(required[0])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError(relative)
    path = root / client.get_client_version() / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    world_map.cook_in_memory_map(str(path))
    if args.case == "stale":
        data = args.stale_cache.read_bytes()
    else:
        data = cache_variant(path.read_bytes(), args.case)
    path.write_bytes(data)
    original = world.get_settings()
    vehicle = manager = None
    try:
        settings = world.get_settings()
        settings.synchronous_mode = True
        settings.fixed_delta_seconds = 0.05
        world.apply_settings(settings)
        started = time.monotonic()
        manager = client.get_trafficmanager(args.tm_port)
        setup_seconds = time.monotonic() - started
        manager.set_synchronous_mode(True)
        manager.set_random_device_seed(42)
        blueprint = world.get_blueprint_library().filter("vehicle.lincoln.mkz*")[0]
        blueprint.set_attribute("role_name", "hero")
        vehicle = world.spawn_actor(blueprint, world_map.get_spawn_points()[0])
        vehicle.set_autopilot(True, args.tm_port)
        manager.ignore_lights_percentage(vehicle, 100)
        manager.ignore_signs_percentage(vehicle, 100)
        start = vehicle.get_location()
        for _ in range(160):
            world.tick(120)
        distance = start.distance(vehicle.get_location())
        if distance < 5:
            raise RuntimeError(f"TM did not drive after cache initialization: {distance:.3f} m")
        result = dict(case=args.case, passed=True, map=world_map.name,
                      cache_sha256=hashlib.sha256(data).hexdigest(),
                      cache_bytes=len(data), cache_path=str(path),
                      setup_seconds=setup_seconds, distance_m=distance, ticks=160,
                      opendrive_sha256=hashlib.sha256(world_map.to_opendrive().encode()).hexdigest())
        (args.output / "result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result))
    finally:
        if vehicle is not None:
            vehicle.set_autopilot(False, args.tm_port)
            vehicle.destroy()
        if manager is not None:
            manager.set_synchronous_mode(False)
        world.apply_settings(original)


if __name__ == "__main__":
    main()
