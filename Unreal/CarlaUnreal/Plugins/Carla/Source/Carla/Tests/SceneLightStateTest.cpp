// Distributed under the repository's MIT license.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Carla/Lights/CarlaLight.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaSceneLightStateTest,
    "Carla.Lights.SceneLightCanTurnOff",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FCarlaSceneLightStateTest::RunTest(const FString &Parameters)
{
  UWorld *World = UWorld::CreateWorld(EWorldType::Game, false);
  AActor *Owner = World->SpawnActor<AActor>();
  UCarlaLight *Light = NewObject<UCarlaLight>(Owner);
  Light->SetLightOn(true);
  TestTrue(TEXT("Direct setter enables"), Light->GetLightOn());
  Light->SetLightOn(false);
  TestFalse(TEXT("Direct setter disables"), Light->GetLightOn());
  carla::rpc::LightState State;
  State._active = true;
  Light->SetLightState(State);
  TestTrue(TEXT("RPC state enables"), Light->GetLightOn());
  State._active = false;
  Light->SetLightState(State);
  TestFalse(TEXT("RPC state disables"), Light->GetLightOn());
  World->DestroyWorld(false);
  return true;
}
#endif
