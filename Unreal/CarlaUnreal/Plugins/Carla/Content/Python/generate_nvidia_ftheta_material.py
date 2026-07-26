"""Generate CARLA's NVIDIA F-theta post-process material.

Run with UnrealEditor-Cmd after enabling the Python Editor Script Plugin:

  UnrealEditor-Cmd CarlaUnreal.uproject \
    -ExecutePythonScript=Plugins/Carla/Content/Python/generate_nvidia_ftheta_material.py \
    -unattended -nop4
"""

import unreal


ASSET_PATH = "/Game/Carla/PostProcessingMaterials"
ASSET_NAME = "NvidiaFThetaDistortion"

PARAMETERS = {
    "FThetaEnabled": 0.0,
    "FThetaImageWidth": 800.0,
    "FThetaImageHeight": 600.0,
    "FThetaPrincipalPointX": 400.0,
    "FThetaPrincipalPointY": 300.0,
    "FThetaMaxAngle": 1.5707963268,
    "FThetaSourceFocalLength": 400.0,
    "FThetaBackward0": 0.0,
    "FThetaBackward1": 0.0,
    "FThetaBackward2": 0.0,
    "FThetaBackward3": 0.0,
    "FThetaBackward4": 0.0,
    "FThetaBackward5": 0.0,
}

CUSTOM_CODE = r"""
if (FThetaEnabled < 0.5)
{
    return UV;
}

// NVIDIA/OpenCV camera axes: +X right, +Y down, +Z forward. Calibration
// coordinates use integer pixel centres while Unreal UVs address texel
// centres at (pixel + 0.5) / image_size.
float2 image_size = float2(FThetaImageWidth, FThetaImageHeight);
float2 output_pixel = UV * image_size - 0.5;
float2 delta = output_pixel -
    float2(FThetaPrincipalPointX, FThetaPrincipalPointY);
float radius = length(delta);
float theta =
    FThetaBackward0 + radius * (
    FThetaBackward1 + radius * (
    FThetaBackward2 + radius * (
    FThetaBackward3 + radius * (
    FThetaBackward4 + radius * FThetaBackward5))));
theta = min(theta, FThetaMaxAngle);

float2 radial_direction =
    radius > 1.0e-7 ? delta / radius : float2(0.0, 0.0);
float sin_theta;
float cos_theta;
sincos(theta, sin_theta, cos_theta);
float2 source_pixel =
    image_size * 0.5 +
    FThetaSourceFocalLength * radial_direction * sin_theta /
        max(cos_theta, 1.0e-7);
return (source_pixel + 0.5) / image_size;
"""


def create_material():
    full_path = f"{ASSET_PATH}/{ASSET_NAME}"
    existing = unreal.EditorAssetLibrary.load_asset(full_path)
    if existing:
        unreal.EditorAssetLibrary.delete_asset(full_path)

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME,
        ASSET_PATH,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )
    material.set_editor_property(
        "material_domain", unreal.MaterialDomain.MD_POST_PROCESS
    )
    material.set_editor_property(
        "blendable_location",
        unreal.BlendableLocation.BL_BEFORE_TONEMAPPING,
    )
    material.set_editor_property("blendable_priority", 100)

    uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -1000, -500
    )
    custom = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionCustom, -500, -300
    )
    custom.set_editor_property("description", "NVIDIA F-theta output-to-source UV")
    custom.set_editor_property("code", CUSTOM_CODE)
    custom.set_editor_property(
        "output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2
    )
    custom_inputs = [unreal.CustomInput(input_name="UV")]
    custom_inputs.extend(
        unreal.CustomInput(input_name=name) for name in PARAMETERS
    )
    custom.set_editor_property("inputs", custom_inputs)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        uv, "", custom, "UV"
    )

    for index, (name, default) in enumerate(PARAMETERS.items()):
        parameter = unreal.MaterialEditingLibrary.create_material_expression(
            material,
            unreal.MaterialExpressionScalarParameter,
            -1000,
            -350 + index * 90,
        )
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", default)
        if not unreal.MaterialEditingLibrary.connect_material_expressions(
            parameter, "", custom, name
        ):
            raise RuntimeError(f"Failed to connect material parameter {name}")

    scene = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionSceneTexture, 0, -300
    )
    scene.set_editor_property(
        "scene_texture_id",
        unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0,
    )
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        custom, "", scene, "Coordinates"
    ):
        raise RuntimeError("Failed to connect F-theta UVs to scene texture")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        scene, "Color", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Failed to connect scene texture to material output")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {full_path}")
    unreal.log(f"Generated {full_path}")


create_material()
