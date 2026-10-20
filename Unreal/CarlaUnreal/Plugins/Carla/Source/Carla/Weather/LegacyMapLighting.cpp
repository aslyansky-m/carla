// Distributed under the repository's MIT license.

#include "Carla/Weather/LegacyMapLighting.h"
#include "Carla/Weather/WeatherParameters.h"
#include "Carla/Weather/Weather.h"
#include "Carla.h"

#include <util/ue-header-guard-begin.h>
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include <util/ue-header-guard-end.h>

namespace
{
const FName LegacyLightingTag(TEXT("CarlaLegacyLighting"));
const FName LegacyModernSkyTag(TEXT("CarlaLegacyModernSky"));

template <typename T>
T *FindLegacyLightingActor(UWorld &World)
{
  for (TActorIterator<T> It(&World); It; ++It)
  {
    if (It->ActorHasTag(LegacyLightingTag))
    {
      return *It;
    }
  }
  return nullptr;
}

float FiniteClamp(float Value, float Minimum, float Maximum, float Fallback)
{
  return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
}
}

bool HasLegacyMapLighting(UWorld &World)
{
  return (FParse::Param(FCommandLine::Get(), TEXT("carla-legacy-lighting")) ||
      FParse::Param(FCommandLine::Get(), TEXT("carla-force-native-sky"))) &&
      FindLegacyLightingActor<ADirectionalLight>(World) != nullptr &&
      FindLegacyLightingActor<ASkyLight>(World) != nullptr;
}

FWeatherParameters GetLegacyMapInitialWeather()
{
  FWeatherParameters Weather;
  Weather.SunAltitudeAngle = 45.0f;
  Weather.SunAzimuthAngle = 135.0f;
  Weather.WindIntensity = 0.0f;
  FParse::Value(FCommandLine::Get(), TEXT("carla-legacy-sun-altitude="), Weather.SunAltitudeAngle);
  FParse::Value(FCommandLine::Get(), TEXT("carla-legacy-sun-azimuth="), Weather.SunAzimuthAngle);
  Weather.SunAltitudeAngle = FiniteClamp(Weather.SunAltitudeAngle, -90.0f, 90.0f, 45.0f);
  Weather.SunAzimuthAngle = FMath::IsFinite(Weather.SunAzimuthAngle)
      ? FRotator::ClampAxis(Weather.SunAzimuthAngle) : 135.0f;
  return Weather;
}

void ConfigureLegacyMapLighting(UWorld &World)
{
  const bool bForceNativeSky = FParse::Param(FCommandLine::Get(), TEXT("carla-force-native-sky"));
  if (!bForceNativeSky && !FParse::Param(FCommandLine::Get(), TEXT("carla-legacy-lighting")))
  {
    return;
  }
  if (HasLegacyMapLighting(World))
  {
    return;
  }

  // Town10 and Mine already have the modern sky Blueprint. Do not replace it.
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    if (It->GetClass()->GetName() == TEXT("BP_Carla_Sky_C"))
    {
      if (!bForceNativeSky)
      {
        UE_LOG(LogCarla, Log, TEXT("Legacy daylight: preserving modern BP_Carla_Sky."));
        return;
      }
      It->SetActorHiddenInGame(true);
      It->SetActorTickEnabled(false);
      UE_LOG(LogCarla, Warning,
          TEXT("Legacy daylight: explicitly forcing native sky; hid BP_Carla_Sky and disabled its tick."));
    }
  }

  // A legacy lighting sublevel can retain a sun/skylight and postprocess
  // volume even when its BP_Sky failed to load. Replace that incomplete rig
  // as a unit; adding only atmosphere can leave the scene completely dark.
  int32 DisabledComponents = 0;
  int32 DisabledVolumes = 0;
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    TArray<USceneComponent *> Components;
    It->GetComponents(Components);
    for (USceneComponent *Component : Components)
    {
      if (Component->IsA<UDirectionalLightComponent>() ||
          Component->IsA<USkyLightComponent>() ||
          Component->IsA<USkyAtmosphereComponent>() ||
          Component->IsA<UExponentialHeightFogComponent>() ||
          (bForceNativeSky && Component->IsA<UVolumetricCloudComponent>()))
      {
        Component->SetVisibility(false, true);
        ++DisabledComponents;
      }
      if (bForceNativeSky)
      {
        if (UPostProcessComponent *PostProcess = Cast<UPostProcessComponent>(Component))
        {
          PostProcess->bEnabled = false;
          ++DisabledVolumes;
        }
      }
    }
    if (APostProcessVolume *Volume = Cast<APostProcessVolume>(*It))
    {
      if (Volume->bUnbound)
      {
        Volume->bEnabled = false;
        ++DisabledVolumes;
      }
    }
  }

  float SunLux = 75000.0f;
  FWeatherParameters InitialWeather = GetLegacyMapInitialWeather();
  const float SunAltitude = InitialWeather.SunAltitudeAngle;
  const float SunAzimuth = InitialWeather.SunAzimuthAngle;
  FParse::Value(FCommandLine::Get(), TEXT("carla-legacy-sun-lux="), SunLux);
  SunLux = FiniteClamp(SunLux, 0.0f, 150000.0f, 75000.0f);

  FActorSpawnParameters Params;
  Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  Params.ObjectFlags |= RF_Transient;
  Params.Name = TEXT("CarlaLegacyAtmosphere");
  ASkyAtmosphere *Atmosphere = World.SpawnActor<ASkyAtmosphere>(
      FVector::ZeroVector, FRotator::ZeroRotator, Params);
  if (Atmosphere == nullptr)
  {
    UE_LOG(LogCarla, Error, TEXT("Legacy daylight: could not create atmosphere."));
    return;
  }
  Atmosphere->GetComponent()->SetMobility(EComponentMobility::Movable);
  Atmosphere->GetComponent()->SetVisibility(true);
  Atmosphere->Tags.Add(LegacyLightingTag);

  Params.Name = TEXT("CarlaLegacySun");
  ADirectionalLight *Sun = World.SpawnActor<ADirectionalLight>(
      FVector::ZeroVector, FRotator(-SunAltitude, SunAzimuth, 0.0f), Params);
  if (Sun == nullptr)
  {
    UE_LOG(LogCarla, Error, TEXT("Legacy daylight: could not create sun."));
    return;
  }
  UDirectionalLightComponent *SunComponent = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
  Sun->Tags.Add(LegacyLightingTag);
  SunComponent->SetMobility(EComponentMobility::Movable);
  SunComponent->SetAtmosphereSunLight(true);
  SunComponent->SetAtmosphereSunLightIndex(0);
  SunComponent->SetLightColor(FLinearColor::White);
  SunComponent->SetIntensity(SunLux);
  SunComponent->SetLightSourceAngle(0.5357f);
  SunComponent->SetCastShadows(true);
  SunComponent->SetDynamicShadowDistanceMovableLight(20000.0f);
  SunComponent->SetVisibility(true);

  Params.Name = TEXT("CarlaLegacySkyLight");
  ASkyLight *SkyLight = World.SpawnActor<ASkyLight>(
      FVector::ZeroVector, FRotator::ZeroRotator, Params);
  if (SkyLight == nullptr)
  {
    UE_LOG(LogCarla, Error, TEXT("Legacy daylight: could not create skylight."));
    return;
  }
  USkyLightComponent *SkyComponent = SkyLight->GetLightComponent();
  SkyLight->Tags.Add(LegacyLightingTag);
  SkyComponent->SetMobility(EComponentMobility::Movable);
  SkyComponent->SetIntensity(1.0f);
  SkyComponent->SetRealTimeCaptureEnabled(true);
  SkyComponent->SetVisibility(true);
  SkyComponent->RecaptureSky();

  Params.Name = TEXT("CarlaLegacyFog");
  AExponentialHeightFog *Fog = World.SpawnActor<AExponentialHeightFog>(
      FVector::ZeroVector, FRotator::ZeroRotator, Params);
  if (Fog != nullptr)
  {
    Fog->Tags.Add(LegacyLightingTag);
    Fog->GetComponent()->SetMobility(EComponentMobility::Movable);
    Fog->GetComponent()->SetVolumetricFog(false);
    Fog->GetComponent()->SetFogDensity(0.0f);
    Fog->GetComponent()->SetVisibility(false);
  }
  else
  {
    UE_LOG(LogCarla, Error, TEXT("Legacy weather: could not create height fog."));
  }

  ApplyLegacyMapWeather(World, InitialWeather);

  UE_LOG(LogCarla, Log,
      TEXT("Legacy daylight ready: sun %.0f lux, altitude %.1f, azimuth %.1f; replaced %d light/fog components and %d unbound postprocess volumes."),
      SunLux, SunAltitude, SunAzimuth, DisabledComponents, DisabledVolumes);
  UE_LOG(LogCarla, Warning,
      TEXT("Legacy weather supports sun angle/day-night illumination and height fog only; clouds, rain, puddles/wetness, wind, dust, atmospheric scattering overrides and automatic street-light switching are not implemented."));
}

bool ApplyLegacyMapWeather(UWorld &World, FWeatherParameters &Weather)
{
  if (!HasLegacyMapLighting(World))
  {
    return false;
  }

  ADirectionalLight *Sun = FindLegacyLightingActor<ADirectionalLight>(World);
  ASkyLight *SkyLight = FindLegacyLightingActor<ASkyLight>(World);
  const float Altitude = FiniteClamp(Weather.SunAltitudeAngle, -90.0f, 90.0f, 45.0f);
  // CARLA presets use -1 for an unspecified azimuth. Preserve the current
  // direction for that sentinel, rather than rotating to 359 degrees.
  const float Azimuth = FMath::IsFinite(Weather.SunAzimuthAngle) && Weather.SunAzimuthAngle >= 0.0f
      ? FRotator::ClampAxis(Weather.SunAzimuthAngle) : Sun->GetActorRotation().Yaw;
  Weather.SunAltitudeAngle = Altitude;
  Weather.SunAzimuthAngle = FRotator::ClampAxis(Azimuth);
  Weather.FogDensity = FiniteClamp(Weather.FogDensity, 0.0f, 100.0f, 0.0f);
  Weather.FogDistance = FiniteClamp(Weather.FogDistance, 0.0f, 100000.0f, 0.0f);
  Weather.FogFalloff = FiniteClamp(Weather.FogFalloff, 0.001f, 10.0f, 0.2f);
  const FWeatherParameters Defaults;
  if (Weather.Cloudiness != 0.0f || Weather.Precipitation != 0.0f ||
      Weather.PrecipitationDeposits != 0.0f || Weather.Wetness != 0.0f ||
      Weather.WindIntensity != 0.0f || Weather.DustStorm != 0.0f ||
      Weather.ScatteringIntensity != Defaults.ScatteringIntensity ||
      Weather.MieScatteringScale != Defaults.MieScatteringScale ||
      Weather.RayleighScatteringScale != Defaults.RayleighScatteringScale)
  {
    UE_LOG(LogCarla, Warning,
        TEXT("Legacy weather: ignoring unsupported cloud %.1f, rain %.1f, deposits %.1f, wetness %.1f, wind %.1f, dust %.1f and scattering overrides; API readback is normalized to zero/default."),
        Weather.Cloudiness, Weather.Precipitation, Weather.PrecipitationDeposits,
        Weather.Wetness, Weather.WindIntensity, Weather.DustStorm);
  }
  Weather.Cloudiness = 0.0f;
  Weather.Precipitation = 0.0f;
  Weather.PrecipitationDeposits = 0.0f;
  Weather.Wetness = 0.0f;
  Weather.WindIntensity = 0.0f;
  Weather.DustStorm = 0.0f;
  Weather.ScatteringIntensity = Defaults.ScatteringIntensity;
  Weather.MieScatteringScale = Defaults.MieScatteringScale;
  Weather.RayleighScatteringScale = Defaults.RayleighScatteringScale;
  float SunLux = 75000.0f;
  FParse::Value(FCommandLine::Get(), TEXT("carla-legacy-sun-lux="), SunLux);
  SunLux = FiniteClamp(SunLux, 0.0f, 150000.0f, 75000.0f);

  // Preserve the verified 45-degree daylight intensity. Fade direct light
  // over the last five degrees above the horizon, and ambient light through
  // civil twilight; a below-horizon sun must not illuminate roads from below.
  const float DirectFactor = FMath::Clamp(Altitude / 5.0f, 0.0f, 1.0f);
  const float SkyFactor = FMath::Clamp((Altitude + 6.0f) / 6.0f, 0.0f, 1.0f);
  Sun->SetActorRotation(FRotator(-Altitude, Azimuth, 0.0f));
  UDirectionalLightComponent *SunComponent = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
  SunComponent->SetIntensity(SunLux * DirectFactor);
  SkyLight->GetLightComponent()->SetIntensity(SkyFactor);

  if (AExponentialHeightFog *Fog = FindLegacyLightingActor<AExponentialHeightFog>(World))
  {
    UExponentialHeightFogComponent *FogComponent = Fog->GetComponent();
    const float Density = Weather.FogDensity;
    // Explicit native approximation: CARLA percent -> UE height-fog density.
    // Distance is specified in metres by CARLA and centimetres by Unreal.
    FogComponent->SetFogDensity(Density * 0.001f);
    FogComponent->SetFogMaxOpacity(1.0f);
    FogComponent->SetStartDistance(Weather.FogDistance * 100.0f);
    FogComponent->SetFogHeightFalloff(Weather.FogFalloff);
    FogComponent->SetFogInscatteringColor(FLinearColor(0.45f, 0.55f, 0.65f) * SkyFactor);
    FogComponent->SetDirectionalInscatteringColor(FLinearColor::Black);
    FogComponent->SetVisibility(Density > 0.0f);
  }
  return true;
}

AWeather *PrepareLegacyModernWeather(UWorld &World)
{
  if (!FParse::Param(FCommandLine::Get(), TEXT("carla-legacy-modern-sky")) ||
      FParse::Param(FCommandLine::Get(), TEXT("carla-force-native-sky")))
  {
    return nullptr;
  }
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    if (It->GetClass()->GetName() == TEXT("BP_Carla_Sky_C"))
    {
      // This includes the established native-map setup. Do not tag it or
      // remove any of its lighting/controllers during finalization.
      return nullptr;
    }
  }

  UClass *SkyClass = StaticLoadClass(AActor::StaticClass(), nullptr,
      TEXT("/Game/Carla/Blueprints/LevelDesign/BP_Carla_Sky.BP_Carla_Sky_C"));
  UClass *WeatherClass = StaticLoadClass(AWeather::StaticClass(), nullptr,
      TEXT("/Game/Carla/Blueprints/Weather/BP_CarlaWeather.BP_CarlaWeather_C"));
  if (SkyClass == nullptr || WeatherClass == nullptr ||
      SkyClass->HasAnyClassFlags(CLASS_Abstract) || WeatherClass->HasAnyClassFlags(CLASS_Abstract))
  {
    UE_LOG(LogCarla, Error,
        TEXT("Legacy modern sky: required cooked sky/weather classes unavailable; keeping native fallback."));
    return nullptr;
  }

  FActorSpawnParameters Params;
  Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  Params.ObjectFlags |= RF_Transient;
  Params.OverrideLevel = World.PersistentLevel;
  Params.Name = TEXT("CarlaLegacyModernSky");
  AActor *Sky = World.SpawnActor<AActor>(SkyClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
  if (Sky == nullptr)
  {
    UE_LOG(LogCarla, Error, TEXT("Legacy modern sky: sky spawn failed; keeping native fallback."));
    return nullptr;
  }
  Sky->Tags.Add(LegacyModernSkyTag);

  Params.Name = TEXT("CarlaLegacyModernWeather");
  AWeather *Weather = World.SpawnActor<AWeather>(WeatherClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
  if (Weather == nullptr)
  {
    Sky->Destroy();
    UE_LOG(LogCarla, Error,
        TEXT("Legacy modern sky: controller spawn failed; removed experimental sky and kept native fallback."));
    return nullptr;
  }

  // A legacy controller must not compete with the newly created Blueprint.
  // Commit this replacement only after both new actors were created.
  TArray<AWeather *> ReplacedControllers;
  for (TActorIterator<AWeather> It(&World); It; ++It)
  {
    if (*It != Weather)
    {
      ReplacedControllers.Add(*It);
    }
  }
  for (AWeather *OldWeather : ReplacedControllers)
  {
    OldWeather->Destroy();
  }
  UE_LOG(LogCarla, Warning,
      TEXT("Legacy modern sky: experimental cooked BP_Carla_Sky + BP_CarlaWeather created; replaced %d old controllers. Rendered weather effects still require validation."),
      ReplacedControllers.Num());
  return Weather;
}

void FinalizeLegacyModernSky(UWorld &World)
{
  AActor *ModernSky = nullptr;
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    if (It->ActorHasTag(LegacyModernSkyTag))
    {
      ModernSky = *It;
      break;
    }
  }
  if (ModernSky == nullptr)
  {
    return;
  }

  TSet<AActor *> PreservedActors;
  PreservedActors.Add(ModernSky);
  TArray<AActor *> Children;
  ModernSky->GetAllChildActors(Children, true);
  ModernSky->GetAttachedActors(Children, false, true);
  for (AActor *Child : Children)
  {
    PreservedActors.Add(Child);
  }

  int32 DisabledComponents = 0;
  int32 DisabledVolumes = 0;
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    if (PreservedActors.Contains(*It))
    {
      continue;
    }
    TArray<USceneComponent *> Components;
    It->GetComponents(Components);
    for (USceneComponent *Component : Components)
    {
      if (Component->IsA<UDirectionalLightComponent>() ||
          Component->IsA<USkyLightComponent>() ||
          Component->IsA<USkyAtmosphereComponent>() ||
          Component->IsA<UExponentialHeightFogComponent>() ||
          Component->IsA<UVolumetricCloudComponent>())
      {
        Component->SetVisibility(false, true);
        ++DisabledComponents;
      }
    }
    if (APostProcessVolume *Volume = Cast<APostProcessVolume>(*It))
    {
      if (Volume->bUnbound)
      {
        Volume->bEnabled = false;
        ++DisabledVolumes;
      }
    }
    const FString ClassName = It->GetClass()->GetName();
    if (ClassName == TEXT("BP_Sky_C") || ClassName == TEXT("BP_Sky_Sphere_C"))
    {
      It->SetActorHiddenInGame(true);
    }
  }
  UE_LOG(LogCarla, Log,
      TEXT("Legacy modern sky: retained %d sky/child actors; disabled %d competing light/fog/cloud components and %d unbound postprocess volumes."),
      PreservedActors.Num(), DisabledComponents, DisabledVolumes);
}

void ApplyModernWeatherBridge(UWorld &World, FWeatherParameters &Weather)
{
  if (!FParse::Param(FCommandLine::Get(), TEXT("carla-modern-weather-bridge")) ||
      HasLegacyMapLighting(World))
  {
    return;
  }
  float SkyFillMultiplier = 1.0f;
  FParse::Value(FCommandLine::Get(), TEXT("carla-experimental-sky-fill="), SkyFillMultiplier);
  SkyFillMultiplier = FiniteClamp(SkyFillMultiplier, 0.0f, 8.0f, 1.0f);
  const bool bVolumetricFog = FParse::Param(FCommandLine::Get(), TEXT("carla-experimental-volumetric-fog"));
  const bool bDustMaterial = FParse::Param(FCommandLine::Get(), TEXT("carla-experimental-dust-material"));
  const bool bDiagnostics = FParse::Param(FCommandLine::Get(), TEXT("carla-weather-diagnostics"));
  if (bDustMaterial || bDiagnostics)
  {
    UMaterialParameterCollection *Collection = LoadObject<UMaterialParameterCollection>(nullptr,
        TEXT("/Game/Carla/Blueprints/Weather/Materials/WeatherMaterialParameters.WeatherMaterialParameters"));
    UMaterialParameterCollectionInstance *Instance = Collection ? World.GetParameterCollectionInstance(Collection) : nullptr;
    float Previous = 0.0f;
    const bool bFound = Instance && Instance->GetScalarParameterValue(TEXT("DustStorm"), Previous);
    float Scale = 1.0f;
    FParse::Value(FCommandLine::Get(), TEXT("carla-dust-material-scale="), Scale);
    Scale = FiniteClamp(Scale, 0.0f, 100.0f, 1.0f);
    const float Target = FiniteClamp(Weather.DustStorm, 0.0f, 100.0f, 0.0f) * 0.01f * Scale;
    const bool bUpdated = bDustMaterial && bFound && Instance->SetScalarParameterValue(TEXT("DustStorm"), Target);
    UE_LOG(LogCarla, Log,
        TEXT("Atmosphere experiment dust MPC: found %d previous %.4f requested %.1f target %.4f override %d updated %d; screen material only, no volumetric particle claim"),
        bFound, Previous, Weather.DustStorm, Target, bDustMaterial, bUpdated);
  }
  // Weak keys do not retain components across map reloads. Capture authored
  // brightness once, never the faded night value, so night -> day recovers.
  static TMap<TWeakObjectPtr<ULightComponentBase>, float> AuthoredIntensities;
  for (auto It = AuthoredIntensities.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid())
    {
      It.RemoveCurrent();
    }
  }
  const auto AuthoredIntensity = [](ULightComponentBase *Light)
  {
    const TWeakObjectPtr<ULightComponentBase> Key(Light);
    if (const float *Stored = AuthoredIntensities.Find(Key))
    {
      return *Stored;
    }
    const float Value = FiniteClamp(Light->Intensity, 0.0f, 150000.0f, 1.0f);
    AuthoredIntensities.Add(Key, Value);
    return Value;
  };
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    if (It->GetClass()->GetName() != TEXT("BP_Carla_Sky_C") || It->IsHidden())
    {
      continue;
    }
    const FName BridgeTag(TEXT("CarlaModernWeatherBridge"));
    if (!It->ActorHasTag(BridgeTag))
    {
      It->Tags.Add(BridgeTag);
      UE_LOG(LogCarla, Warning,
          TEXT("Modern weather bridge enabled: authored daylight intensity, native sun rotation/twilight fade and nonvolumetric height-fog approximation; sky tick and moon disabled. Blueprint clouds/rain/wetness retained; no moon, street-light or full photometric parity claim."));
    }
    It->SetActorTickEnabled(false);
    Weather.SunAltitudeAngle = FiniteClamp(Weather.SunAltitudeAngle, -90.0f, 90.0f, 45.0f);
    Weather.FogDensity = FiniteClamp(Weather.FogDensity, 0.0f, 100.0f, 0.0f);
    Weather.FogDistance = FiniteClamp(Weather.FogDistance, 0.0f, 100000.0f, 0.0f);
    Weather.FogFalloff = FiniteClamp(Weather.FogFalloff, 0.001f, 10.0f, 0.2f);
    const float DirectFactor = FMath::Clamp(Weather.SunAltitudeAngle / 5.0f, 0.0f, 1.0f);
    const float SkyFactor = FMath::Clamp((Weather.SunAltitudeAngle + 6.0f) / 6.0f, 0.0f, 1.0f);
    TArray<UDirectionalLightComponent *> Lights;
    It->GetComponents(Lights);
    for (UDirectionalLightComponent *Light : Lights)
    {
      if (Light->GetName() == TEXT("DirectionalLightComponentSun"))
      {
        const float Azimuth = FMath::IsFinite(Weather.SunAzimuthAngle) && Weather.SunAzimuthAngle >= 0.0f
            ? FRotator::ClampAxis(Weather.SunAzimuthAngle) : Light->GetComponentRotation().Yaw;
        Weather.SunAzimuthAngle = FRotator::ClampAxis(Azimuth);
        Light->SetWorldRotation(FRotator(-Weather.SunAltitudeAngle, Azimuth, 0.0f));
        Light->SetIntensity(AuthoredIntensity(Light) * DirectFactor);
        Light->SetVisibility(true);
      }
      else if (Light->GetName() == TEXT("DirectionalLightComponentMoon"))
      {
        Light->SetIntensity(0.0f);
        Light->SetVisibility(false);
      }
    }
    if (USkyLightComponent *Sky = It->FindComponentByClass<USkyLightComponent>())
    {
      Sky->SetIntensity(AuthoredIntensity(Sky) * SkyFactor * SkyFillMultiplier);
      Sky->SetRealTimeCaptureEnabled(true);
      Sky->SetVisibility(true);
      Sky->RecaptureSky();
    }
    if (UExponentialHeightFogComponent *Fog = It->FindComponentByClass<UExponentialHeightFogComponent>())
    {
      Fog->SetVolumetricFog(bVolumetricFog && Weather.FogDensity > 0.0f);
      if (bVolumetricFog)
      {
        float ViewDistanceMetres = 200.0f;
        FParse::Value(FCommandLine::Get(), TEXT("carla-volumetric-fog-distance="), ViewDistanceMetres);
        ViewDistanceMetres = FiniteClamp(ViewDistanceMetres, 10.0f, 2000.0f, 200.0f);
        Fog->SetVolumetricFogDistance(ViewDistanceMetres * 100.0f);
        Fog->SetVolumetricFogStartDistance(Weather.FogDistance * 100.0f);
        Fog->SetVolumetricFogNearFadeInDistance(100.0f);
        Fog->SetVolumetricFogScatteringDistribution(0.2f);
        Fog->SetVolumetricFogExtinctionScale(1.0f);
        Fog->SetVolumetricFogAlbedo(FColor(230, 230, 230, 255));
        Fog->SetVolumetricFogEmissive(FLinearColor::Black);
      }
      Fog->SetFogDensity(Weather.FogDensity * 0.001f);
      Fog->SetFogMaxOpacity(1.0f);
      Fog->SetStartDistance(Weather.FogDistance * 100.0f);
      Fog->SetFogHeightFalloff(Weather.FogFalloff);
      Fog->SetFogInscatteringColor(FLinearColor(0.45f, 0.55f, 0.65f) * SkyFactor);
      Fog->SetDirectionalInscatteringColor(FLinearColor::Black);
      Fog->SetVisibility(Weather.FogDensity > 0.0f);
    }
    if (bDiagnostics)
    {
      UE_LOG(LogCarla, Log, TEXT("Atmosphere experiment: sky fill %.3f volumetric fog %d density %.3f start %.1fm falloff %.3f"),
          SkyFillMultiplier, bVolumetricFog, Weather.FogDensity * 0.001f, Weather.FogDistance, Weather.FogFalloff);
    }
  }
}

void LogLegacyWeatherDiagnostics(UWorld &World, const FWeatherParameters &Weather)
{
  if (!FParse::Param(FCommandLine::Get(), TEXT("carla-weather-diagnostics")))
  {
    return;
  }
  UE_LOG(LogCarla, Log,
      TEXT("Weather diagnostics after apply: sun %.2f/%.2f cloud %.1f rain %.1f wet %.1f fog %.1f"),
      Weather.SunAltitudeAngle, Weather.SunAzimuthAngle, Weather.Cloudiness,
      Weather.Precipitation, Weather.Wetness, Weather.FogDensity);
  const auto LogSettings = [](const FString &Name, const FPostProcessSettings &Settings)
  {
    UE_LOG(LogCarla, Log,
        TEXT("Weather diagnostics PP %s: method %d bias %.3f ISO %.1f shutter %.1f fstop %.2f physical %d gamma %s contrast %s saturation %s highlightsGamma %s tint %s overrides(gamma/contrast/saturation/exposure)=%d/%d/%d/%d"),
        *Name, static_cast<int32>(Settings.AutoExposureMethod), Settings.AutoExposureBias,
        Settings.CameraISO, Settings.CameraShutterSpeed, Settings.DepthOfFieldFstop,
        Settings.AutoExposureApplyPhysicalCameraExposure, *Settings.ColorGamma.ToString(),
        *Settings.ColorContrast.ToString(), *Settings.ColorSaturation.ToString(),
        *Settings.ColorGammaHighlights.ToString(), *Settings.SceneColorTint.ToString(),
        Settings.bOverride_ColorGamma, Settings.bOverride_ColorContrast,
        Settings.bOverride_ColorSaturation, Settings.bOverride_AutoExposureMethod);
  };
  for (TActorIterator<AActor> It(&World); It; ++It)
  {
    const bool bSkyActor = It->GetClass()->GetName() == TEXT("BP_Carla_Sky_C") ||
        It->ActorHasTag(LegacyLightingTag) || It->ActorHasTag(LegacyModernSkyTag);
    TArray<USceneComponent *> Components;
    It->GetComponents(Components);
    for (USceneComponent *Component : Components)
    {
      const FString Name = It->GetName() + TEXT(".") + Component->GetName();
      if (bSkyActor)
      {
        if (UDirectionalLightComponent *Light = Cast<UDirectionalLightComponent>(Component))
        {
          UE_LOG(LogCarla, Log,
              TEXT("Weather diagnostics sun %s: intensity %.6g visible %d active %d actorHidden %d color %s rotation %s"),
              *Name, Light->Intensity, Light->IsVisible(), Light->IsActive(), It->IsHidden(),
              *Light->GetLightColor().ToString(), *Light->GetComponentRotation().ToString());
        }
        if (USkyLightComponent *Light = Cast<USkyLightComponent>(Component))
        {
          UE_LOG(LogCarla, Log,
              TEXT("Weather diagnostics skylight %s: intensity %.6g visible %d active %d actorHidden %d realtime %d"),
              *Name, Light->Intensity, Light->IsVisible(), Light->IsActive(), It->IsHidden(), Light->bRealTimeCapture);
        }
        if (UPostProcessComponent *PostProcess = Cast<UPostProcessComponent>(Component))
        {
          UE_LOG(LogCarla, Log,
              TEXT("Weather diagnostics volume %s: enabled %d unbound %d blendWeight %.3f priority %.3f"),
              *Name, PostProcess->bEnabled, PostProcess->bUnbound, PostProcess->BlendWeight, PostProcess->Priority);
          LogSettings(Name, PostProcess->Settings);
        }
      }
      if (USceneCaptureComponent2D *Capture = Cast<USceneCaptureComponent2D>(Component))
      {
        UE_LOG(LogCarla, Log,
            TEXT("Weather diagnostics capture %s: postprocessing %d materialPass %d blendWeight %.3f source %d"),
            *Name, Capture->ShowFlags.PostProcessing, Capture->ShowFlags.PostProcessMaterial,
            Capture->PostProcessBlendWeight, static_cast<int32>(Capture->CaptureSource));
        LogSettings(Name, Capture->PostProcessSettings);
      }
    }
  }
}
