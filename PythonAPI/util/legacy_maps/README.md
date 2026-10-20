# Legacy-map inspection utilities

See [the UE5 legacy-map guide](../../../Docs/legacy_maps_ue5.md) for build,
launch, camera comparison, weather controls, Traffic Manager cache repair,
validation, and known visual limitations.

- `launch.py`: launch a cooked UE5 package with explicit package/GPU/port options.
- `capture.py`: record RGB images and actual poses; reuse one poses file across versions.
- `camera.json`: shared inspection camera profile, with unsupported fields recorded.
- `town03_poses.json`: three camera/hero poses generated in CARLA 0.9.16.
- `tm_cache.py`: regenerate and validate a private Traffic Manager cache.

Images, server logs, runtime binaries and caches belong in a separate output
directory and are not generated in this utility directory.
