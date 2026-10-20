// Distributed under the repository's MIT license.

#include "Carla/Weather/NightLighting.h"
#include "Carla/Weather/WeatherParameters.h"
#include "Carla/Lights/CarlaLightSubsystem.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"
#include "Carla.h"

#include <util/ue-header-guard-begin.h>
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include <util/ue-header-guard-end.h>

namespace
{
struct FStreetLightOriginal
{
  bool bOn;
  float Intensity;
};

struct FNightState
{
  bool bNight = false;
  bool bVehicleLights = false;
  bool bStreetLights = true;
  bool bHeadlampFallback = false;
  float StreetIntensityScale = 1.0f;
  float HeadlampLumens = 300.0f;
  float HeadlampInnerCone = 5.0f;
  float HeadlampOuterCone = 25.0f;
  float HeadlampSourceRadius = 5.0f;
  TWeakObjectPtr<ADirectionalLight> Moon;
  FTimerHandle Timer;
  TMap<TWeakObjectPtr<UCarlaLight>, FStreetLightOriginal> StreetLights;
  TMap<TWeakObjectPtr<ACarlaWheeledVehicle>, FVehicleLightState> Vehicles;
  TMap<TWeakObjectPtr<ACarlaWheeledVehicle>, TArray<TWeakObjectPtr<USpotLightComponent>>> Headlamps;
};

void UpdateLocalLights(UWorld &World, FNightState &State)
{
  UCarlaLightSubsystem *Lights = World.GetSubsystem<UCarlaLightSubsystem>();
  if (State.bStreetLights && Lights != nullptr)
  {
    for (const auto &Pair : Lights->GetLights())
    {
      UCarlaLight *Light = Pair.Value;
      if (!IsValid(Light) || Light->GetLightType() != ELightType::Street)
      {
        continue;
      }
      const TWeakObjectPtr<UCarlaLight> Key(Light);
      if (State.bNight)
      {
        if (!State.StreetLights.Contains(Key))
        {
          State.StreetLights.Add(Key, {Light->GetLightOn(), Light->GetLightIntensity()});
          Light->SetLightIntensity(Light->GetLightIntensity() * State.StreetIntensityScale);
          Light->SetLightOn(true);
        }
      }
      else if (const FStreetLightOriginal *Original = State.StreetLights.Find(Key))
      {
        Light->SetLightIntensity(Original->Intensity);
        Light->SetLightOn(Original->bOn);
        State.StreetLights.Remove(Key);
      }
    }
  }
  if (State.bVehicleLights || State.bHeadlampFallback)
  {
    for (TActorIterator<ACarlaWheeledVehicle> It(&World); It; ++It)
    {
      const TWeakObjectPtr<ACarlaWheeledVehicle> Key(*It);
      FVehicleLightState Current = It->GetVehicleLightState();
      if (State.bVehicleLights && State.bNight && !State.Vehicles.Contains(Key))
      {
        State.Vehicles.Add(Key, Current);
        Current.Position = true;
        Current.LowBeam = true;
        It->SetVehicleLightState(Current);
      }
      else if (State.bVehicleLights && !State.bNight)
      {
        if (const FVehicleLightState *Original = State.Vehicles.Find(Key))
        {
          // Preserve brake, indicators and all other caller-controlled bits.
          Current.Position = Original->Position;
          Current.LowBeam = Original->LowBeam;
          It->SetVehicleLightState(Current);
          State.Vehicles.Remove(Key);
        }
      }
      if (State.bHeadlampFallback)
      {
        TArray<TWeakObjectPtr<USpotLightComponent>> &Headlamps = State.Headlamps.FindOrAdd(Key);
        if (Headlamps.IsEmpty())
        {
          TArray<ULightComponent *> AuthoredLights;
          It->GetComponents(AuthoredLights);
          for (ULightComponent *Authored : AuthoredLights)
          {
            UE_LOG(LogCarla, Log,
                TEXT("Authored vehicle lamp %s.%s intensity %.3f visible %d hidden %d"),
                *It->GetName(), *Authored->GetName(), Authored->Intensity,
                Authored->IsVisible(), Authored->bHiddenInGame);
          }
          const FVector Center = It->GetVehicleBoundingBoxTransform().GetLocation();
          const FVector Extent = It->GetVehicleBoundingBoxExtent();
          for (int32 Side : {-1, 1})
          {
            USpotLightComponent *Lamp = NewObject<USpotLightComponent>(*It);
            It->AddInstanceComponent(Lamp);
            Lamp->SetupAttachment(It->GetRootComponent());
            Lamp->SetMobility(EComponentMobility::Movable);
            Lamp->SetRelativeLocation(Center + FVector(
                Extent.X + 5.0f, Side * Extent.Y * 0.65f, -Extent.Z * 0.2f));
            Lamp->SetRelativeRotation(FRotator(-3.0f, Side * 2.0f, 0.0f));
            Lamp->SetIntensityUnits(ELightUnits::Lumens);
            Lamp->SetIntensity(0.0f);
            Lamp->SetLightColor(FLinearColor(1.0f, 0.97f, 0.90f));
            Lamp->SetAttenuationRadius(8000.0f);
            Lamp->SetInnerConeAngle(State.HeadlampInnerCone);
            Lamp->SetOuterConeAngle(State.HeadlampOuterCone);
            Lamp->SetSourceRadius(State.HeadlampSourceRadius);
            // Grazing illumination of the road otherwise shows severe
            // self-shadow striping with these migrated road meshes.
            Lamp->SetShadowBias(1.5f);
            Lamp->SetShadowSlopeBias(2.0f);
            Lamp->SetCastShadows(true);
            Lamp->SetVisibility(false);
            Lamp->RegisterComponent();
            Headlamps.Add(Lamp);
          }
          UE_LOG(LogCarla, Log,
              TEXT("Night headlamp fallback attached to %s at bounding-box front. Paired spot cones; no automotive cutoff/IES calibration."),
              *It->GetName());
        }
        const bool bOn = Current.LowBeam || Current.HighBeam;
        for (const auto &WeakLamp : Headlamps)
        {
          if (USpotLightComponent *Lamp = WeakLamp.Get())
          {
            Lamp->SetIntensity(bOn ? State.HeadlampLumens * (Current.HighBeam ? 2.0f : 1.0f) : 0.0f);
            Lamp->SetVisibility(bOn);
          }
        }
      }
    }
  }
  for (auto It = State.StreetLights.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid()) { It.RemoveCurrent(); }
  }
  for (auto It = State.Vehicles.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid()) { It.RemoveCurrent(); }
  }
  for (auto It = State.Headlamps.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid()) { It.RemoveCurrent(); }
  }
}
}

void ApplyOptInNightLighting(UWorld &World, const FWeatherParameters &Weather)
{
  if (!FParse::Param(FCommandLine::Get(), TEXT("carla-night-lighting")))
  {
    return;
  }
  static TMap<TWeakObjectPtr<UWorld>, TSharedPtr<FNightState>> States;
  for (auto It = States.CreateIterator(); It; ++It)
  {
    if (!It.Key().IsValid()) { It.RemoveCurrent(); }
  }
  const TWeakObjectPtr<UWorld> WorldKey(&World);
  TSharedPtr<FNightState> &Stored = States.FindOrAdd(WorldKey);
  if (!Stored)
  {
    Stored = MakeShared<FNightState>();
    Stored->bVehicleLights = FParse::Param(FCommandLine::Get(), TEXT("carla-night-vehicle-lights"));
    Stored->bHeadlampFallback = FParse::Param(FCommandLine::Get(), TEXT("carla-night-headlamp-fallback"));
    Stored->bStreetLights = !FParse::Param(FCommandLine::Get(), TEXT("carla-night-no-street-lights"));
    FParse::Value(FCommandLine::Get(), TEXT("carla-night-street-scale="), Stored->StreetIntensityScale);
    Stored->StreetIntensityScale = FMath::IsFinite(Stored->StreetIntensityScale)
        ? FMath::Clamp(Stored->StreetIntensityScale, 0.0f, 1.0f) : 1.0f;
    FParse::Value(FCommandLine::Get(), TEXT("carla-night-headlamp-lumens="), Stored->HeadlampLumens);
    FParse::Value(FCommandLine::Get(), TEXT("carla-night-headlamp-inner-cone="), Stored->HeadlampInnerCone);
    FParse::Value(FCommandLine::Get(), TEXT("carla-night-headlamp-outer-cone="), Stored->HeadlampOuterCone);
    FParse::Value(FCommandLine::Get(), TEXT("carla-night-headlamp-source-radius="), Stored->HeadlampSourceRadius);
    Stored->HeadlampLumens = FMath::IsFinite(Stored->HeadlampLumens)
        ? FMath::Clamp(Stored->HeadlampLumens, 0.0f, 5000.0f) : 300.0f;
    Stored->HeadlampOuterCone = FMath::IsFinite(Stored->HeadlampOuterCone)
        ? FMath::Clamp(Stored->HeadlampOuterCone, 5.0f, 60.0f) : 25.0f;
    Stored->HeadlampInnerCone = FMath::IsFinite(Stored->HeadlampInnerCone)
        ? FMath::Clamp(Stored->HeadlampInnerCone, 0.0f, Stored->HeadlampOuterCone) : 5.0f;
    Stored->HeadlampSourceRadius = FMath::IsFinite(Stored->HeadlampSourceRadius)
        ? FMath::Clamp(Stored->HeadlampSourceRadius, 0.0f, 20.0f) : 5.0f;
    const TSharedPtr<FNightState> State = Stored;
    World.GetTimerManager().SetTimer(State->Timer, [WorldKey, State]()
    {
      if (UWorld *LiveWorld = WorldKey.Get())
      {
        UpdateLocalLights(*LiveWorld, *State);
      }
    }, 1.0f, true);
  }
  FNightState &State = *Stored;
  const float Altitude = FMath::IsFinite(Weather.SunAltitudeAngle)
      ? FMath::Clamp(Weather.SunAltitudeAngle, -90.0f, 90.0f) : 45.0f;
  const float NightFactor = FMath::Clamp(-Altitude / 6.0f, 0.0f, 1.0f);
  State.bNight = Altitude < -1.0f;
  if (!State.Moon.IsValid())
  {
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transient;
    Params.Name = TEXT("CarlaExperimentalMoon");
    ADirectionalLight *Moon = World.SpawnActor<ADirectionalLight>(
        FVector::ZeroVector, FRotator(-35.0f, 315.0f, 0.0f), Params);
    if (Moon != nullptr)
    {
      State.Moon = Moon;
      Moon->Tags.Add(TEXT("CarlaNightLighting"));
      UDirectionalLightComponent *Light = CastChecked<UDirectionalLightComponent>(Moon->GetLightComponent());
      Light->SetMobility(EComponentMobility::Movable);
      Light->SetAtmosphereSunLight(true);
      Light->SetAtmosphereSunLightIndex(1);
      Light->SetLightColor(FLinearColor(0.75f, 0.83f, 1.0f));
      Light->SetCastShadows(true);
      Light->SetLightSourceAngle(0.52f);
      Light->SetDynamicShadowDistanceMovableLight(20000.0f);
    }
  }
  float MoonLux = 0.25f;
  float SkyFloor = 0.08f;
  FParse::Value(FCommandLine::Get(), TEXT("carla-night-moon-lux="), MoonLux);
  FParse::Value(FCommandLine::Get(), TEXT("carla-night-sky-intensity="), SkyFloor);
  MoonLux = FMath::IsFinite(MoonLux) ? FMath::Clamp(MoonLux, 0.0f, 1.0f) : 0.25f;
  SkyFloor = FMath::IsFinite(SkyFloor) ? FMath::Clamp(SkyFloor, 0.0f, 1.0f) : 0.08f;
  if (ADirectionalLight *Moon = State.Moon.Get())
  {
    const float Azimuth = FMath::IsFinite(Weather.SunAzimuthAngle) && Weather.SunAzimuthAngle >= 0.0f
        ? FRotator::ClampAxis(Weather.SunAzimuthAngle + 180.0f) : 315.0f;
    Moon->SetActorRotation(FRotator(-35.0f, Azimuth, 0.0f));
    Moon->GetLightComponent()->SetIntensity(MoonLux * NightFactor);
    Moon->GetLightComponent()->SetVisibility(NightFactor > 0.0f);
  }
  if (NightFactor > 0.0f)
  {
    for (TActorIterator<AActor> It(&World); It; ++It)
    {
      if (It->IsHidden() || (!It->ActorHasTag(TEXT("CarlaLegacyLighting")) &&
          It->GetClass()->GetName() != TEXT("BP_Carla_Sky_C")))
      {
        continue;
      }
      if (USkyLightComponent *Sky = It->FindComponentByClass<USkyLightComponent>())
      {
        // Ambient light still comes from the real sky capture; no emissive
        // road, grading override or camera exposure change is introduced.
        Sky->SetIntensity(FMath::Max(Sky->Intensity, SkyFloor * NightFactor));
        Sky->SetVisibility(true);
        Sky->RecaptureSky();
      }
    }
  }
  UpdateLocalLights(World, State);
  float MaximumStreetIntensity = 0.0f;
  for (const auto &Pair : State.StreetLights)
  {
    MaximumStreetIntensity = FMath::Max(MaximumStreetIntensity, Pair.Value.Intensity);
  }
  UE_LOG(LogCarla, Log,
      TEXT("Night lighting experiment: altitude %.1f moon %.3f lux, sky floor %.3f, registered street lights %d (authored max %.3f, scale %.3f), managed vehicles %d, fallback headlamp pairs %d. Fixed 35-degree full moon; not an astronomical model."),
      Altitude, MoonLux * NightFactor, SkyFloor * NightFactor,
      State.StreetLights.Num(), MaximumStreetIntensity, State.StreetIntensityScale,
      State.Vehicles.Num(), State.Headlamps.Num());
}
