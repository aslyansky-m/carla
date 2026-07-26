// Copyright (c) 2024 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Sensor/SceneCaptureCamera.h"
#include "Carla.h"
#include "Carla/Game/CarlaEngine.h"
#include <chrono>

#include <util/ue-header-guard-begin.h>
#include "Actor/ActorBlueprintFunctionLibrary.h"
#include "RenderingThread.h"
#include <util/ue-header-guard-end.h>

FActorDefinition ASceneCaptureCamera::GetSensorDefinition()
{
    constexpr bool bEnableModifyingPostProcessEffects = true;
    return UActorBlueprintFunctionLibrary::MakeCameraDefinition(
        TEXT("rgb"),
        bEnableModifyingPostProcessEffects);
}

ASceneCaptureCamera::ASceneCaptureCamera(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    AddPostProcessingMaterial(
        TEXT("Material'/Carla/PostProcessingMaterials/PhysicLensDistortion.PhysicLensDistortion'"));
}

void ASceneCaptureCamera::Set(const FActorDescription &Description)
{
  Super::Set(Description);
  const bool bEnableFTheta =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToBool(
          "ftheta_enabled", Description.Variations, false);
  if (!bEnableFTheta)
  {
    return;
  }
  if (!ReplacePostProcessingMaterial(
          0u,
          TEXT("Material'/Carla/PostProcessingMaterials/NvidiaFThetaDistortion.NvidiaFThetaDistortion'")))
  {
    UE_LOG(LogCarla, Error, TEXT(
        "NVIDIA F-theta material is missing; ftheta_enabled cannot be used"));
    return;
  }

  constexpr uint8 FThetaShaderIndex = 0u;
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaEnabled"),
      1.0f);
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaImageWidth"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_image_width", Description.Variations, 800.0f));
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaImageHeight"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_image_height", Description.Variations, 600.0f));
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaPrincipalPointX"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_principal_point_x", Description.Variations, 400.0f));
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaPrincipalPointY"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_principal_point_y", Description.Variations, 300.0f));
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaMaxAngle"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_max_angle", Description.Variations, 1.5707963268f));
  SetFloatShaderParameter(
      FThetaShaderIndex,
      TEXT("FThetaSourceFocalLength"),
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_source_focal_length", Description.Variations, 400.0f));
  for (uint8 Index = 0u; Index < 6u; ++Index)
  {
    const FString AttributeName = FString::Printf(
        TEXT("ftheta_pixeldist_to_angle_%u"), Index);
    const FName ParameterName(*FString::Printf(TEXT("FThetaBackward%u"), Index));
    SetFloatShaderParameter(
        FThetaShaderIndex,
        ParameterName,
        UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
            *AttributeName, Description.Variations, 0.0f));
  }
}

void ASceneCaptureCamera::BeginPlay()
{
  Super::BeginPlay();
}

void ASceneCaptureCamera::OnFirstClientConnected()
{
}

void ASceneCaptureCamera::OnLastClientDisconnected()
{
}

void ASceneCaptureCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
}

void ASceneCaptureCamera::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaSeconds)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ASceneCaptureCamera::PostPhysTick);
  Super::PostPhysTick(World, TickType, DeltaSeconds);
  
  ENQUEUE_RENDER_COMMAND(MeasureTime)
  (
    [](auto &InRHICmdList)
    {
      std::chrono::time_point<std::chrono::high_resolution_clock> Time = 
          std::chrono::high_resolution_clock::now();
      auto Duration = std::chrono::duration_cast< std::chrono::milliseconds >(Time.time_since_epoch());
      uint64_t Milliseconds = Duration.count();
      FString ProfilerText = FString("(Render)Frame: ") + FString::FromInt(FCarlaEngine::GetFrameCounter()) + 
          FString(" Time: ") + FString::FromInt(Milliseconds);
      TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ProfilerText);
    }
  );

  if (!AreClientsListening())
      return;

  auto FrameIndex = FCarlaEngine::GetFrameCounter();
  ImageUtil::ReadSensorImageDataAsyncFColor(*this, [this, FrameIndex](
    TArrayView<const FColor> Pixels,
    FIntPoint Size) -> bool
  {
    SendDataToClient(*this, Pixels, FrameIndex);
    return true;
  });
}

#ifdef CARLA_HAS_GBUFFER_API
void ASceneCaptureCamera::SendGBufferTextures(FGBufferRequest& GBuffer)
{
    SendGBufferTexturesInternal(*this, GBuffer);
}
#endif
