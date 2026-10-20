#!/usr/bin/env python3
"""Capture identical absolute camera poses using the matching CARLA Python client."""
import argparse
import hashlib
import json
from pathlib import Path
import queue


def serialize(transform):
    return dict(x=transform.location.x, y=transform.location.y, z=transform.location.z,
                pitch=transform.rotation.pitch, yaw=transform.rotation.yaw,
                roll=transform.rotation.roll)


def main():
    import carla

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=2000, type=int)
    parser.add_argument("--map", default="Town03_Opt")
    parser.add_argument("--poses", required=True, type=Path,
                        help="Reuse poses, or create this file from spawn points if absent")
    parser.add_argument("--spawn-indices", type=int, nargs="+", default=[0, 80, 160])
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--wetness", type=float, default=0)
    parser.add_argument("--deposits", type=float, default=0)
    parser.add_argument("--sun-altitude", type=float, default=45)
    parser.add_argument("--warmup-ticks", type=int, default=1200)
    parser.add_argument("--settle-ticks", type=int, default=300)
    args = parser.parse_args()
    if not 0 <= args.wetness <= 100 or not 0 <= args.deposits <= 100:
        parser.error("Wetness and deposits must be in [0, 100]")
    if min(args.warmup_ticks, args.settle_ticks) < 1:
        parser.error("Warm-up and settling ticks must be positive")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Output directory must be empty")
    args.output.mkdir(parents=True, exist_ok=True)
    client = carla.Client(args.host, args.port)
    client.set_timeout(120)
    world = client.get_world()
    if world.get_map().name.split("/")[-1] != args.map.split("/")[-1]:
        world = client.load_world(args.map)
    world_map = world.get_map()
    original_settings = world.get_settings()
    original_weather = world.get_weather()
    original_spectator = world.get_spectator().get_transform()
    actors = []
    camera = None

    def transform(data):
        return carla.Transform(
            carla.Location(x=data["x"], y=data["y"], z=data["z"]),
            carla.Rotation(pitch=data["pitch"], yaw=data["yaw"], roll=data["roll"]))

    try:
        settings = world.get_settings()
        settings.synchronous_mode = True
        settings.fixed_delta_seconds = 0.05
        world.apply_settings(settings)
        weather = carla.WeatherParameters(
            cloudiness=0, precipitation=0, precipitation_deposits=args.deposits,
            wind_intensity=0, sun_azimuth_angle=135,
            sun_altitude_angle=args.sun_altitude, fog_density=0,
            fog_distance=0, fog_falloff=0, wetness=args.wetness)
        if args.poses.exists():
            poses = json.loads(args.poses.read_text())
        else:
            spawns = world_map.get_spawn_points()
            if any(index < 0 or index >= len(spawns) for index in args.spawn_indices):
                parser.error(f"Spawn indices must be below {len(spawns)}")
            poses = []
            for index in args.spawn_indices:
                spawn = spawns[index]
                camera_pose = carla.Transform(
                    spawn.transform(carla.Location(x=1.5, z=2)), spawn.rotation)
                poses.append(dict(id=len(poses), source_spawn_index=index,
                                  vehicle=serialize(spawn), camera=serialize(camera_pose)))
            args.poses.parent.mkdir(parents=True, exist_ok=True)
            args.poses.write_text(json.dumps(poses, indent=2))
        if not poses:
            raise ValueError("At least one pose is required")
        profile = json.loads((Path(__file__).parent / "camera.json").read_text())
        library = world.get_blueprint_library()
        vehicle_bp = library.filter("vehicle.lincoln.mkz*")[0]
        vehicle_bp.set_attribute("role_name", "hero")
        vehicle = world.spawn_actor(vehicle_bp, transform(poses[0]["vehicle"]))
        actors.append(vehicle)
        vehicle.set_simulate_physics(False)
        blueprint = library.find("sensor.camera.rgb")
        requested = dict(image_size_x=1280, image_size_y=720, fov=90, **profile)
        for key, value in requested.items():
            if blueprint.has_attribute(key):
                blueprint.set_attribute(key, str(value))
        camera = world.spawn_actor(blueprint, transform(poses[0]["camera"]))
        actors.append(camera)
        frames = queue.Queue()
        camera.listen(frames.put)
        report = dict(status="running", client_version=client.get_client_version(),
                      server_version=client.get_server_version(), map=world_map.name,
                      opendrive_sha256=hashlib.sha256(world_map.to_opendrive().encode()).hexdigest(),
                      camera_attributes={k: camera.attributes[k] for k in requested
                                         if k in camera.attributes},
                      unsupported_profile_fields=[k for k in requested
                                                  if not blueprint.has_attribute(k)],
                      weather_requested=str(weather), samples=[])
        for index, pose in enumerate(poses):
            vehicle.set_transform(transform(pose["vehicle"]))
            camera.set_transform(transform(pose["camera"]))
            world.get_spectator().set_transform(transform(pose["camera"]))
            world.set_weather(weather)
            ticks = args.warmup_ticks if index == 0 else args.settle_ticks
            for _ in range(ticks):
                frame = world.tick(120)
                while True:
                    image = frames.get(timeout=120)
                    if image.frame >= frame:
                        break
                if image.frame != frame:
                    raise RuntimeError("Camera/world frame mismatch")
            actual = serialize(camera.get_transform())
            if max(abs(actual[k] - pose["camera"][k]) for k in actual) > 0.001:
                raise RuntimeError(f"Camera pose changed: {actual}")
            filename = f"pose-{index}.png"
            image.save_to_disk(str(args.output / filename))
            report["samples"].append(dict(id=index, frame=image.frame, png=filename,
                                          requested_camera=pose["camera"],
                                          actual_camera=actual,
                                          weather_readback=str(world.get_weather())))
            (args.output / "result.json").write_text(json.dumps(report, indent=2))
        report["status"] = "completed"
        (args.output / "result.json").write_text(json.dumps(report, indent=2))
        print(f"Captured {len(poses)} poses in {args.output}")
    finally:
        if camera is not None:
            camera.stop()
        for actor in reversed(actors):
            actor.destroy()
        world.set_weather(original_weather)
        world.get_spectator().set_transform(original_spectator)
        world.apply_settings(original_settings)


if __name__ == "__main__":
    main()
