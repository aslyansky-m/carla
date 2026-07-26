// Copyright (c) 2024 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "Carla/Actor/ActorDefinition.h"
#include "Carla/Sensor/ShaderBasedSensor.h"

#include <util/ue-header-guard-begin.h>
#include "Actor/ActorBlueprintFunctionLibrary.h"
#include <util/ue-header-guard-end.h>

#include "SceneCaptureCamera.generated.h"

class UMaterialInstanceDynamic;
class UMaterial;

/// A sensor that captures images from the scene.
UCLASS()
class CARLA_API ASceneCaptureCamera : public AShaderBasedSensor
{
  GENERATED_BODY()

public:


  static FActorDefinition GetSensorDefinition();

  ASceneCaptureCamera(const FObjectInitializer &ObjectInitializer);

  void Set(const FActorDescription &ActorDescription) override;

  /// Serialize the final warped size, not the larger source render size.
  uint32 GetImageWidth() const
  {
    return bFThetaEnabled ? FThetaOutputWidth : Super::GetImageWidth();
  }

  uint32 GetImageHeight() const
  {
    return bFThetaEnabled ? FThetaOutputHeight : Super::GetImageHeight();
  }

protected:
	
#ifdef CARLA_HAS_GBUFFER_API
  virtual void SendGBufferTextures(FGBufferRequest& GBuffer) override;
#endif

  void BeginPlay() override;
  void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  void PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaSeconds) override;
  
  virtual void OnFirstClientConnected() override;
  virtual void OnLastClientDisconnected() override;

private:

  bool bFThetaEnabled = false;
  uint32 FThetaOutputWidth = 800u;
  uint32 FThetaOutputHeight = 600u;
  float FThetaPrincipalPointX = 400.0f;
  float FThetaPrincipalPointY = 300.0f;
  float FThetaMaxAngle = 1.5707963268f;
  float FThetaSourceFocalLength = 400.0f;
  float FThetaBackward[6] = {};

  UPROPERTY()
  UTextureRenderTarget2D *FThetaOutputRenderTarget = nullptr;

  // A serialized hard reference ensures the optional generated material is
  // discovered by the cooker instead of relying on a runtime-only string.
  UPROPERTY()
  UMaterial *FThetaBaseMaterial = nullptr;

  UPROPERTY()
  UMaterialInstanceDynamic *FThetaMaterial = nullptr;
};
