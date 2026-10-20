// Distributed under the repository's MIT license.
#pragma once

class UWorld;
struct FWeatherParameters;

// Explicit opt-in experiments. Call after Blueprint RefreshWeather. Does not
// change API readback: scalar multipliers are rendering calibration controls.
void ApplyWeatherMaterialExperiment(UWorld &World, const FWeatherParameters &Weather);
