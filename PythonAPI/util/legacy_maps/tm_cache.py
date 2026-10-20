"""Build a private TrafficManager cache from the active world's OpenDRIVE."""
import hashlib
import json
import struct
import tempfile
import time
from pathlib import Path


def validate(path, world_map):
    """Check the unversioned CARLA 0.10 binary before native code reads it."""
    data = Path(path).read_bytes()
    offset = 0

    def read(fmt):
        nonlocal offset
        values = struct.unpack_from("<" + fmt, data, offset)
        offset += struct.calcsize("<" + fmt)
        return values

    count, = read("I")
    ids, refs, missing = set(), set(), []
    for index in range(count):
        wid, road, section, lane, distance = read("QIIif")
        ids.add(wid)
        next_count, = read("H")
        refs.update(read("Q" * next_count))
        previous_count, = read("H")
        refs.update(read("Q" * previous_count))
        left, right, grid, junction, option = read("QQi?B")
        refs.update(value for value in (left, right) if value)
        if world_map.get_waypoint_xodr(road, lane, distance) is None:
            missing.append([index, road, section, lane, distance])
    result = {
        "records": count,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "invalid_waypoints": len(missing),
        "invalid_waypoint_examples": missing[:10],
        "missing_references": len(refs - ids),
        "duplicate_ids": count - len(ids),
        "trailing_bytes": len(data) - offset,
    }
    if not count or missing or refs - ids or len(ids) != count or offset != len(data):
        raise ValueError("Invalid generated TrafficManager cache: " + json.dumps(result))
    return result


def prepare(client, world_map, out):
    """Return evidence; never overwrite the package or default client cache."""
    started = time.monotonic()
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="tm-cache-", dir=str(out)))
    version = client.get_client_version()
    # CARLA's setter silently ignores paths already ending with '/'.
    if not client.set_files_base_folder(str(root)):
        raise RuntimeError("Cannot select private CARLA file cache")
    required = list(client.get_required_files("TM", False))
    if len(required) != 1:
        raise RuntimeError("Expected exactly one TM cache for this map: " + repr(required))
    relative = Path(required[0])
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Unexpected server cache path: " + str(relative))
    destination = root / version / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    xodr = world_map.to_opendrive().encode("utf-8")
    (root / "active-map.xodr").write_bytes(xodr)
    world_map.cook_in_memory_map(str(destination))
    checked = validate(destination, world_map)
    result = {
        "method": "cook_in_memory_map from active map; isolated process cache",
        "map": world_map.name,
        "client_version": version,
        "required_files": required,
        "cache_root": str(root),
        "cache_path": str(destination),
        "opendrive_path": str(root / "active-map.xodr"),
        "opendrive_sha256": hashlib.sha256(xodr).hexdigest(),
        "cache_validation": checked,
        "elapsed_seconds": time.monotonic() - started,
    }
    (out / "traffic-manager-cache.json").write_text(json.dumps(result, indent=2))
    return result
