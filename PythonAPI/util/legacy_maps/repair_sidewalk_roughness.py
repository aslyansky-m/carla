"""Restore texture-driven roughness in the migrated Town15 sidewalk instance.

Run via UnrealEditor-Cmd -run=pythonscript in an isolated project copy.
Set CARLA_MATERIAL_REPAIR_REPORT to a new JSON path, then recook and inspect.
"""

import hashlib
import json
import os
from pathlib import Path

import unreal


def main():
    report = Path(os.environ["CARLA_MATERIAL_REPAIR_REPORT"])
    if report.exists():
        raise RuntimeError("Refusing to overwrite a material repair report")
    asset = "/Game/Carla/Static/GenericMaterials/Sidewalk/MI_Sidewalk_Community_v2"
    source = Path(unreal.Paths.project_content_dir()) / (asset.removeprefix("/Game/") + ".uasset")
    if source.is_symlink():
        raise RuntimeError("Repair requires a private material copy")
    source = source.resolve()
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    material = unreal.load_asset(asset)
    library = unreal.MaterialEditingLibrary
    expected = {"RoughnessContrast": 25.0, "RoughnessCorrection": -65.0}
    actual = {key: library.get_material_instance_scalar_parameter_value(material, key)
              for key in expected}
    if actual != expected:
        raise RuntimeError(f"Unexpected or already repaired material: {actual}")
    # The parent computes saturate(pow(ORM.g, contrast) + correction).
    # The migrated overrides force zero roughness for every normalized texel.
    # Parent defaults preserve the authored texture and the wetness subtraction.
    repaired = {"RoughnessContrast": 1.0, "RoughnessCorrection": 0.0}
    for key, value in repaired.items():
        # UE5.5's setter returns false even after setting the value.
        library.set_material_instance_scalar_parameter_value(material, key, value)
        if library.get_material_instance_scalar_parameter_value(material, key) != value:
            raise RuntimeError("Failed to update " + key)
    library.update_material_instance(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Failed to save repaired material")
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(dict(
        material=asset, before_sha256=before,
        after_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        before_parameters=actual, after_parameters=repaired,
        status="source_repaired_requires_cook_and_render_validation"), indent=2))


main()
