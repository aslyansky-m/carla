// Distributed under the repository's MIT license.

#pragma once

class UWorld;
class AWeather;
struct FWeatherParameters;

/// Optional native daylight rig for UE4 towns whose BP_Sky was not ported.
/// Called after the map's lighting sublevels have finished loading.
void ConfigureLegacyMapLighting(UWorld &World);

/// True only for the opt-in native rig; modern BP_Carla_Sky maps are excluded.
bool HasLegacyMapLighting(UWorld &World);

/// Initial parameters matching the rig's command-line sun defaults.
FWeatherParameters GetLegacyMapInitialWeather();

/// Apply the supported subset (sun direction, day/night intensity, height fog).
/// Returns false when this world does not contain our opt-in rig.
/// Normalizes Weather in place so API readback matches supported/clamped
/// values. Unsupported clouds, rain, wetness, wind and dust become zero;
/// scattering overrides revert to their default values.
bool ApplyLegacyMapWeather(UWorld &World, FWeatherParameters &Weather);

/// Experimental cooked modern-sky migration, gated by
/// -carla-legacy-modern-sky. Call in InitGame before weather discovery.
/// Returns the new controller, or nullptr to preserve normal/fallback logic.
/// Maps already containing BP_Carla_Sky are left unchanged.
AWeather *PrepareLegacyModernWeather(UWorld &World);

/// Hide competing legacy lighting after map layers load, preserving the
/// experimental sky and its children. No-op on ordinary native maps.
void FinalizeLegacyModernSky(UWorld &World);

/// Optional component/PP snapshot after weather application, gated by
/// -carla-weather-diagnostics. Does not change rendering settings.
void LogLegacyWeatherDiagnostics(UWorld &World, const FWeatherParameters &Weather);

/// Opt-in approximation for cooked modern skies whose Blueprint sun/fog updates
/// fail. Runs after RefreshWeather; preserves Blueprint clouds/rain/wetness.
/// Optional isolated experiments: -carla-experimental-sky-fill=N,
/// -carla-experimental-volumetric-fog, -carla-experimental-dust-material.
/// These retain baseline behavior when absent and require rendered validation.
void ApplyModernWeatherBridge(UWorld &World, FWeatherParameters &Weather);
