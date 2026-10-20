"""Run with UnrealEditor-Cmd -run=pythonscript -script=... in a COPY of the project.

Repairs the known migrated sidewalk master. Recook the asset after applying.
CARLA_MATERIAL_REPAIR_REPORT names the required output report; the source
project must be isolated from the published runtime before using this script.
"""

import hashlib
import json
import os
from pathlib import Path

import unreal


def main():
    report_path = Path(os.environ["CARLA_MATERIAL_REPAIR_REPORT"])
    if report_path.exists():
        raise RuntimeError("Refusing to overwrite a material repair report")
    path = "/Game/Carla/Static/GenericMaterials/CheapMaterials/M_SideWalkCheap01"
    relative = "Carla/Static/GenericMaterials/CheapMaterials/M_SideWalkCheap01.uasset"
    source = Path(unreal.Paths.project_content_dir()) / relative
    if source.is_symlink():
        raise RuntimeError("Repair requires a private material copy, not a shared symlink")
    source = source.resolve()
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    material = unreal.load_asset(path)
    library = unreal.MaterialEditingLibrary

    def node(name):
        result = unreal.load_object(None, material.get_path_name() + ":" + name)
        if result is None:
            raise RuntimeError("Unexpected material graph: missing " + name)
        return result

    puddles = node("MaterialExpressionClamp_1")
    flatten = node("MaterialExpressionMaterialFunctionCall_1")
    invert = node("MaterialExpressionOneMinus_3")
    # Fail before changing an unfamiliar or previously repaired graph.
    for downstream in (flatten, invert):
        if puddles not in library.get_inputs_for_material_expression(material, downstream):
            raise RuntimeError("Expected unmodified puddle connection: " + downstream.get_name())

    def create(cls, x, y):
        result = library.create_material_expression(material, cls, x, y)
        result.set_editor_property("desc", "CARLA puddle slope repair: upward surfaces only")
        return result

    def connect(a, b, input_name):
        if not library.connect_material_expressions(a, "", b, input_name):
            raise RuntimeError("Cannot connect slope-mask graph")

    # Use the geometric normal, not PixelNormalWS: this mask also drives the
    # material's Normal output and must not introduce a feedback dependency.
    normal = create(unreal.MaterialExpressionVertexNormalWS, 1024, 1500)
    z = create(unreal.MaterialExpressionComponentMask, 1224, 1500)
    z.set_editor_property("r", False)
    z.set_editor_property("g", False)
    z.set_editor_property("b", True)
    z.set_editor_property("a", False)
    subtract = create(unreal.MaterialExpressionSubtract, 1424, 1500)
    subtract.set_editor_property("const_b", 0.7)
    divide = create(unreal.MaterialExpressionDivide, 1624, 1500)
    divide.set_editor_property("const_b", 0.3)
    slope = create(unreal.MaterialExpressionClamp, 1824, 1500)
    masked = create(unreal.MaterialExpressionMultiply, 2024, 1500)
    connect(normal, z, "")
    connect(z, subtract, "A")
    connect(subtract, divide, "A")
    connect(divide, slope, "")
    connect(puddles, masked, "A")
    connect(slope, masked, "B")
    connect(masked, flatten, "Flatness")
    connect(masked, invert, "")
    library.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Failed to save repaired material")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(dict(
        material=path, source=str(source), before_sha256=before,
        after_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        mask="original_puddles * saturate((VertexNormalWS.z - 0.7) / 0.3)",
        modified_inputs=["FlattenNormal.Flatness", "OneMinus_3.Input"],
        status="source_repaired_requires_cook_and_render_validation"), indent=2))


main()
