// Distributed under the repository's MIT license.

#pragma once

class UWorld;
struct FWeatherParameters;

// Opt-in illumination experiment; call after the selected weather bridge.
void ApplyOptInNightLighting(UWorld &World, const FWeatherParameters &Weather);
