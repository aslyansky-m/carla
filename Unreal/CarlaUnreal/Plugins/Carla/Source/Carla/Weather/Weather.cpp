// Copyright (c) 2024 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Weather/Weather.h"
#include "Carla/Weather/LegacyMapLighting.h"
#include "Carla/Weather/NightLighting.h"
#include "Carla/Weather/WeatherMaterialExperiment.h"
#include "Carla.h"
#include "Carla/Sensor/SceneCaptureCamera.h"

#include <util/ue-header-guard-begin.h>
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include <util/ue-header-guard-end.h>

AWeather::AWeather(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrecipitationPostProcessMaterial = ConstructorHelpers::FObjectFinder<UMaterial>(
        TEXT("Material'/Game/Carla/Static/GenericMaterials/00_MastersOpt/Screen_posProcess/M_screenDrops.M_screenDrops'")).Object;

    DustStormPostProcessMaterial = ConstructorHelpers::FObjectFinder<UMaterial>(
        TEXT("Material'/Game/Carla/Static/GenericMaterials/00_MastersOpt/Screen_posProcess/M_screenDust_wind.M_screenDust_wind'")).Object;

    PrimaryActorTick.bCanEverTick = false;
    RootComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("RootComponent"));
}

ALegacyMapWeather::ALegacyMapWeather(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void AWeather::CheckWeatherPostProcessEffects()
{
    if (Weather.Precipitation > 0.0f)
        ActiveBlendables.Add(MakeTuple(PrecipitationPostProcessMaterial, Weather.Precipitation / 100.0f));
    else
        ActiveBlendables.Remove(PrecipitationPostProcessMaterial);

    if (Weather.DustStorm > 0.0f)
        ActiveBlendables.Add(MakeTuple(DustStormPostProcessMaterial, Weather.DustStorm / 100.0f));
    else
        ActiveBlendables.Remove(DustStormPostProcessMaterial);

    TArray<AActor*> SensorActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASceneCaptureCamera::StaticClass(), SensorActors);
    for (AActor* SensorActor : SensorActors)
    {
        ASceneCaptureCamera* Sensor = Cast<ASceneCaptureCamera>(SensorActor);
        // Clearing the active map does not remove materials already installed
        // on a sensor. Replace only weather-owned entries, preserving lens and
        // user blendables when rain/dust stop or their weights change.
        FPostProcessSettings& Settings = Sensor->GetCaptureComponent2D()->PostProcessSettings;
        Settings.RemoveBlendable(PrecipitationPostProcessMaterial);
        Settings.RemoveBlendable(DustStormPostProcessMaterial);
        for (auto& ActiveBlendable : ActiveBlendables)
            Settings.AddBlendable(ActiveBlendable.Key, ActiveBlendable.Value);
    }
}

void AWeather::ApplyWeather(const FWeatherParameters& InWeather)
{
    SetWeather(InWeather);
    if (GetWorld() != nullptr && ApplyLegacyMapWeather(*GetWorld(), Weather))
    {
        // This opt-in rig has no compatible weather Blueprint or precipitation
        // material path. Do not execute legacy Blueprint references to BP_Sky.
        ApplyOptInNightLighting(*GetWorld(), Weather);
        LogLegacyWeatherDiagnostics(*GetWorld(), Weather);
        return;
    }
    CheckWeatherPostProcessEffects();

#ifdef CARLA_WEATHER_EXTRA_LOG
    UE_LOG(LogCarla, Log, TEXT("Changing weather:"));
    UE_LOG(LogCarla, Log, TEXT("  - Cloudiness = %.2f"), Weather.Cloudiness);
    UE_LOG(LogCarla, Log, TEXT("  - Precipitation = %.2f"), Weather.Precipitation);
    UE_LOG(LogCarla, Log, TEXT("  - PrecipitationDeposits = %.2f"), Weather.PrecipitationDeposits);
    UE_LOG(LogCarla, Log, TEXT("  - WindIntensity = %.2f"), Weather.WindIntensity);
    UE_LOG(LogCarla, Log, TEXT("  - SunAzimuthAngle = %.2f"), Weather.SunAzimuthAngle);
    UE_LOG(LogCarla, Log, TEXT("  - SunAltitudeAngle = %.2f"), Weather.SunAltitudeAngle);
    UE_LOG(LogCarla, Log, TEXT("  - FogDensity = %.2f"), Weather.FogDensity);
    UE_LOG(LogCarla, Log, TEXT("  - FogDistance = %.2f"), Weather.FogDistance);
    UE_LOG(LogCarla, Log, TEXT("  - FogFalloff = %.2f"), Weather.FogFalloff);
    UE_LOG(LogCarla, Log, TEXT("  - Wetness = %.2f"), Weather.Wetness);
    UE_LOG(LogCarla, Log, TEXT("  - ScatteringIntensity = %.2f"), Weather.ScatteringIntensity);
    UE_LOG(LogCarla, Log, TEXT("  - MieScatteringScale = %.2f"), Weather.MieScatteringScale);
    UE_LOG(LogCarla, Log, TEXT("  - RayleighScatteringScale = %.2f"), Weather.RayleighScatteringScale);
    UE_LOG(LogCarla, Log, TEXT("  - DustStorm = %.2f"), Weather.DustStorm);
#endif // CARLA_WEATHER_EXTRA_LOG

    // Call the blueprint that actually changes the weather.
    RefreshWeather(Weather);
    if (GetWorld() != nullptr)
    {
        ApplyWeatherMaterialExperiment(*GetWorld(), Weather);
        ApplyModernWeatherBridge(*GetWorld(), Weather);
        ApplyOptInNightLighting(*GetWorld(), Weather);
        LogLegacyWeatherDiagnostics(*GetWorld(), Weather);
    }
}

void AWeather::NotifyWeather(ASensor* Sensor)
{
    if (GetWorld() != nullptr && ApplyLegacyMapWeather(*GetWorld(), Weather))
    {
        ApplyOptInNightLighting(*GetWorld(), Weather);
        LogLegacyWeatherDiagnostics(*GetWorld(), Weather);
        return;
    }
    CheckWeatherPostProcessEffects();

    // Call the blueprint that actually changes the weather.
    RefreshWeather(Weather);
    if (GetWorld() != nullptr)
    {
        ApplyWeatherMaterialExperiment(*GetWorld(), Weather);
        ApplyModernWeatherBridge(*GetWorld(), Weather);
        ApplyOptInNightLighting(*GetWorld(), Weather);
        LogLegacyWeatherDiagnostics(*GetWorld(), Weather);
    }
}

void AWeather::SetWeather(const FWeatherParameters& InWeather)
{
    Weather = InWeather;
}

void AWeather::SetDayNightCycle(const bool& active)
{
    DayNightCycle = active;
}

#if WITH_EDITOR
void AWeather::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    ApplyWeather(Weather);
}
#endif
