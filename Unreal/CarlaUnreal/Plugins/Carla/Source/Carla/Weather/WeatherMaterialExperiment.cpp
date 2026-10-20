// Distributed under the repository's MIT license.
#include "Carla/Weather/WeatherMaterialExperiment.h"
#include "Carla/Weather/WeatherParameters.h"
#include "Carla/Sensor/SceneCaptureCamera.h"
#include "Carla.h"

#include <util/ue-header-guard-begin.h>
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Distributions/DistributionFloatParameterBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectIterator.h"
#include <util/ue-header-guard-end.h>

namespace
{
bool IsRain(const UParticleSystemComponent &Component)
{
  return Component.Template && Component.Template->GetName() == TEXT("PS_Rain");
}

float FiniteClamp(float Value, float Minimum, float Maximum, float Fallback)
{
  return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
}

void DescribeRain(UParticleSystemComponent &Component)
{
  UE_LOG(LogCarla, Log, TEXT("Weather material experiment rain: component=%s active=%d visible=%d location=%s bounds=%s count=%d"),
      *Component.GetPathName(), Component.IsActive(), Component.IsVisible(),
      *Component.GetComponentLocation().ToString(), *Component.Bounds.BoxExtent.ToString(),
      Component.GetNumActiveParticles());
  if (!Component.Template) return;
  for (UParticleEmitter *Emitter : Component.Template->Emitters)
  {
    if (!Emitter || Emitter->LODLevels.Num() == 0 || !Emitter->LODLevels[0]) continue;
    UParticleModuleSpawn *Spawn = Emitter->LODLevels[0]->SpawnModule;
    if (!Spawn) continue;
    const auto *Parameter = Cast<UDistributionFloatParameterBase>(Spawn->Rate.Distribution);
    UE_LOG(LogCarla, Log, TEXT("Weather material experiment rain spawn: rate=%g parameter=%s input=[%g,%g] output=[%g,%g] mode=%d"),
        Spawn->Rate.GetValue(0.0f, &Component),
        Parameter ? *Parameter->ParameterName.ToString() : TEXT("none"),
        Parameter ? Parameter->MinInput : 0.0f, Parameter ? Parameter->MaxInput : 0.0f,
        Parameter ? Parameter->MinOutput : 0.0f, Parameter ? Parameter->MaxOutput : 0.0f,
        Parameter ? static_cast<int>(Parameter->ParamMode) : -1);
  }
}
}

void ApplyWeatherMaterialExperiment(UWorld &World, const FWeatherParameters &Weather)
{
  const TCHAR *CommandLine = FCommandLine::Get();
  const bool Diagnostics = FParse::Param(CommandLine, TEXT("carla-material-weather-diagnostics"));
  const bool NoLensRain = FParse::Param(CommandLine, TEXT("carla-rain-no-lens"));
  const bool HideRainParticles = FParse::Param(CommandLine, TEXT("carla-rain-hide-particles"));
  const bool WetnessBridge = FParse::Param(CommandLine, TEXT("carla-wetness-bridge"));
  float WetnessScale = 1.0f;
  float PuddlesScale = 1.0f;
  const bool ScaleWetness = FParse::Value(CommandLine, TEXT("carla-wetness-scale="), WetnessScale);
  const bool ScalePuddles = FParse::Value(CommandLine, TEXT("carla-puddles-scale="), PuddlesScale);
  if (!Diagnostics && !WetnessBridge && !ScaleWetness && !ScalePuddles && !NoLensRain && !HideRainParticles) return;

  auto *Collection = LoadObject<UMaterialParameterCollection>(nullptr,
      TEXT("/Game/Carla/Blueprints/Weather/Materials/WeatherMaterialParameters.WeatherMaterialParameters"));
  if (Collection)
  {
    UMaterialParameterCollectionInstance *Instance = World.GetParameterCollectionInstance(Collection);
    if (Instance)
    {
      for (const TCHAR *Name : {TEXT("Wetness"), TEXT("Puddles"), TEXT("Ripples"), TEXT("Precipitation")})
      {
        float Value = 0.0f;
        if (!Instance->GetScalarParameterValue(FName(Name), Value)) continue;
        const float Before = Value;
        if (FCString::Strcmp(Name, TEXT("Wetness")) == 0 && (WetnessBridge || ScaleWetness))
        {
          // Derive from the request, not the previously scaled collection.
          // NotifyWeather can run repeatedly as sensors are registered.
          Value = FiniteClamp(Weather.Wetness, 0.0f, 100.0f, 0.0f) * 0.01f *
              FiniteClamp(WetnessScale, 0.0f, 100.0f, 1.0f);
        }
        if (FCString::Strcmp(Name, TEXT("Puddles")) == 0 && ScalePuddles)
        {
          Value = FiniteClamp(Weather.PrecipitationDeposits, 0.0f, 100.0f, 0.0f) * 0.01f *
              FiniteClamp(PuddlesScale, 0.0f, 1.0f, 1.0f);
        }
        if (Value != Before) Instance->SetScalarParameterValue(FName(Name), Value);
        if (Diagnostics)
          UE_LOG(LogCarla, Log, TEXT("Weather material experiment scalar %s: blueprint=%g applied=%g requested_wet=%g requested_deposits=%g requested_rain=%g"),
              Name, Before, Value, Weather.Wetness, Weather.PrecipitationDeposits, Weather.Precipitation);
      }
    }
  }

  // Isolate the authored world-space precipitation without disabling lighting
  // or particles (CARLA's general postprocess=false also disables particles).
  UMaterial *LensRain = NoLensRain ? LoadObject<UMaterial>(nullptr,
      TEXT("/Game/Carla/Static/GenericMaterials/00_MastersOpt/Screen_posProcess/M_screenDrops.M_screenDrops")) : nullptr;
  for (TActorIterator<ASceneCaptureCamera> It(&World); It; ++It)
  {
    USceneCaptureComponent2D *Capture = It->GetCaptureComponent2D();
    if (NoLensRain && LensRain) Capture->PostProcessSettings.RemoveBlendable(LensRain);
    if (HideRainParticles) Capture->ShowFlags.SetParticles(false);
    if (Diagnostics)
      UE_LOG(LogCarla, Log, TEXT("Weather material experiment camera: %s particles=%d translucency=%d postprocess_material=%d lens_rain_removed=%d particle_control_hidden=%d"),
          *It->GetPathName(), Capture->ShowFlags.Particles, Capture->ShowFlags.Translucency,
          Capture->ShowFlags.PostProcessMaterial, NoLensRain && LensRain != nullptr, HideRainParticles);
  }

  if (Diagnostics)
  {
    for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
      if (It->GetWorld() == &World && IsRain(**It)) DescribeRain(**It);
    for (TActorIterator<ASceneCaptureCamera> It(&World); It; ++It)
    {
      const FVector Start = It->GetActorLocation();
      for (const FVector &Direction : {FVector(0, 0, -1), It->GetActorRightVector(),
          (It->GetActorForwardVector() + It->GetActorRightVector()).GetSafeNormal()})
      {
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(WeatherMaterialDiagnostic), true, *It);
        if (!World.LineTraceSingleByChannel(Hit, Start, Start + Direction * 10000.0, ECC_Visibility, Query)) continue;
        UPrimitiveComponent *Mesh = Hit.GetComponent();
        if (!Mesh) continue;
        UE_LOG(LogCarla, Log, TEXT("Weather material experiment hit: component=%s normal=%s"),
            *Mesh->GetPathName(), *Hit.ImpactNormal.ToString());
        for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
        {
          UMaterialInterface *Material = Mesh->GetMaterial(Index);
          if (!Material) continue;
          TArray<FMaterialParameterInfo> Info;
          TArray<FGuid> Ids;
          Material->GetAllScalarParameterInfo(Info, Ids);
          UE_LOG(LogCarla, Log, TEXT("Weather material experiment material: %s"), *Material->GetPathName());
          for (const FMaterialParameterInfo &Parameter : Info)
          {
            float Value = 0.0f;
            if (Material->GetScalarParameterValue(Parameter, Value))
              UE_LOG(LogCarla, Log, TEXT("Weather material experiment material scalar: %s=%g"), *Parameter.Name.ToString(), Value);
          }
        }
      }
    }
  }
}
