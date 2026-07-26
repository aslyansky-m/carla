"""Generate CARLA's NVIDIA F-theta GPU resampling material.

Run with UnrealEditor-Cmd after enabling the Python Editor Script Plugin:

  UnrealEditor-Cmd CarlaUnreal.uproject \
    -ExecutePythonScript=Plugins/Carla/Content/Python/generate_nvidia_ftheta_material.py \
    -unattended -nop4
"""

import unreal


ASSET_PATH = "/Game/Carla/PostProcessingMaterials"
ASSET_NAME = "NvidiaFThetaDistortion"

PARAMETERS = {
    "FThetaOutputWidth": 800.0,
    "FThetaOutputHeight": 600.0,
    "FThetaSourceWidth": 800.0,
    "FThetaSourceHeight": 600.0,
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
// NVIDIA/OpenCV camera axes: +X right, +Y down, +Z forward. Calibration
// coordinates use integer pixel centres while Unreal UVs address texel
// centres at (pixel + 0.5) / image_size.
float2 output_size = float2(FThetaOutputWidth, FThetaOutputHeight);
float2 source_size = float2(FThetaSourceWidth, FThetaSourceHeight);
float2 output_pixel = UV * output_size - 0.5;
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
    (source_size - 1.0) * 0.5 +
    FThetaSourceFocalLength * radial_direction * sin_theta /
        max(cos_theta, 1.0e-7);
return (source_pixel + 0.5) / source_size;
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
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property(
        "shading_model", unreal.MaterialShadingModel.MSM_UNLIT
    )

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
    custom_inputs = []
    for name in ("UV", *PARAMETERS):
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
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

    source = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, 0, -300
    )
    source.set_editor_property("parameter_name", "FThetaSourceTexture")
    source.set_editor_property(
        "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    )
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        custom, "", source, ""
    ):
        raise RuntimeError("Failed to connect F-theta UVs to source texture")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        source, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Failed to connect source texture to material output")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {full_path}")
    unreal.log(f"Generated {full_path}")


create_material()
