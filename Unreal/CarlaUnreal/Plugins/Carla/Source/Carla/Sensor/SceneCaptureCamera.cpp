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
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "UObject/ConstructorHelpers.h"
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
    FThetaOutputRenderTarget =
        CreateDefaultSubobject<UTextureRenderTarget2D>(
            TEXT("FThetaOutputRenderTarget"));
    FThetaOutputRenderTarget->CompressionSettings =
        TextureCompressionSettings::TC_Default;
    FThetaOutputRenderTarget->SRGB = false;
    FThetaOutputRenderTarget->bAutoGenerateMips = false;
    FThetaOutputRenderTarget->bGPUSharedFlag = true;
    FThetaOutputRenderTarget->AddressX = TextureAddress::TA_Clamp;
    FThetaOutputRenderTarget->AddressY = TextureAddress::TA_Clamp;

    static ConstructorHelpers::FObjectFinder<UMaterial> FThetaMaterialFinder(
        TEXT("/Game/Carla/PostProcessingMaterials/"
             "NvidiaFThetaDistortion.NvidiaFThetaDistortion"));
    FThetaBaseMaterial = FThetaMaterialFinder.Object;
}

void ASceneCaptureCamera::Set(const FActorDescription &Description)
{
  Super::Set(Description);
  bFThetaEnabled =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToBool(
          "ftheta_enabled", Description.Variations, false);
  if (!bFThetaEnabled)
  {
    return;
  }

  // Do not compound CARLA's generic lens pass with the calibrated projection.
  // Normal scene post-processing remains active on the rectilinear source.
  ClearPostProcessingMaterials();
  // Avoid quantizing the image to 8-bit before nonlinear resampling. The
  // dedicated output target performs the only float-to-8-bit conversion.
  Enable16BitFormat(true);

  // image_size_x/y keep their normal public meaning: dimensions delivered to
  // clients. The extra source dimensions are private implementation detail.
  FThetaOutputWidth = Super::GetImageWidth();
  FThetaOutputHeight = Super::GetImageHeight();
  const uint32 FThetaSourceWidth = FMath::Max(
      1,
      FMath::RoundToInt(
          UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
              "ftheta_source_width", Description.Variations, 800.0f)));
  const uint32 FThetaSourceHeight = FMath::Max(
      1,
      FMath::RoundToInt(
          UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
              "ftheta_source_height", Description.Variations, 600.0f)));
  SetImageSize(FThetaSourceWidth, FThetaSourceHeight);
  FThetaPrincipalPointX =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_principal_point_x", Description.Variations, 400.0f);
  FThetaPrincipalPointY =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_principal_point_y", Description.Variations, 300.0f);
  FThetaMaxAngle =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_max_angle", Description.Variations, 1.5707963268f);
  FThetaSourceFocalLength =
      UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
          "ftheta_source_focal_length", Description.Variations, 400.0f);
  for (uint8 Index = 0u; Index < 6u; ++Index)
  {
    const FString AttributeName = FString::Printf(
        TEXT("ftheta_pixeldist_to_angle_%u"), Index);
    FThetaBackward[Index] =
        UActorBlueprintFunctionLibrary::RetrieveActorAttributeToFloat(
            *AttributeName, Description.Variations, 0.0f);
  }
}

void ASceneCaptureCamera::BeginPlay()
{
  if (bFThetaEnabled)
  {
    // The warp has strongly varying minification. Generate source mips and let
    // the material sampler choose a trilinearly filtered level per pixel.
    CaptureRenderTarget->bAutoGenerateMips = true;
    CaptureRenderTarget->Filter = TextureFilter::TF_Trilinear;
    CaptureRenderTarget->MipsSamplerFilter = TextureFilter::TF_Trilinear;
  }
  Super::BeginPlay();

  if (!bFThetaEnabled)
  {
    return;
  }

  check(FThetaOutputRenderTarget != nullptr);
  FThetaOutputRenderTarget->InitCustomFormat(
      FThetaOutputWidth,
      FThetaOutputHeight,
      PF_B8G8R8A8,
      false);
  FThetaOutputRenderTarget->TargetGamma = GetTargetGamma();

  if (FThetaBaseMaterial == nullptr)
  {
    UE_LOG(
        LogCarla,
        Fatal,
        TEXT("NVIDIA F-theta material is missing; run "
             "generate_nvidia_ftheta_material.py before packaging CARLA"));
    return;
  }
  FThetaMaterial = UMaterialInstanceDynamic::Create(FThetaBaseMaterial, this);
  check(FThetaMaterial != nullptr);
  FThetaMaterial->SetTextureParameterValue(
      TEXT("FThetaSourceTexture"), CaptureRenderTarget);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaOutputWidth"), FThetaOutputWidth);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaOutputHeight"), FThetaOutputHeight);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaSourceWidth"), Super::GetImageWidth());
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaSourceHeight"), Super::GetImageHeight());
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaPrincipalPointX"), FThetaPrincipalPointX);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaPrincipalPointY"), FThetaPrincipalPointY);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaMaxAngle"), FThetaMaxAngle);
  FThetaMaterial->SetScalarParameterValue(
      TEXT("FThetaSourceFocalLength"), FThetaSourceFocalLength);
  for (uint8 Index = 0u; Index < 6u; ++Index)
  {
    FThetaMaterial->SetScalarParameterValue(
        FName(*FString::Printf(TEXT("FThetaBackward%u"), Index)),
        FThetaBackward[Index]);
  }
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
  if (bFThetaEnabled)
  {
    check(FThetaOutputRenderTarget != nullptr);
    check(FThetaMaterial != nullptr);
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(
        this, FThetaOutputRenderTarget, FThetaMaterial);
  }
  UTextureRenderTarget2D *ReadbackTarget =
      bFThetaEnabled ? FThetaOutputRenderTarget : GetCaptureRenderTarget();
  ImageUtil::ReadImageDataAsyncFColor(*ReadbackTarget, [this, FrameIndex](
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
