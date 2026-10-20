// Distributed under the repository's MIT license.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarlaRGBAttributeAlphaTest,
    "Carla.ActorAttributes.RGBColorIsOpaque",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FCarlaRGBAttributeAlphaTest::RunTest(const FString &Parameters)
{
  FActorAttribute Attribute;
  Attribute.Id = TEXT("color_gamma");
  Attribute.Type = EActorAttributeType::RGBColor;
  for (const FColor Expected : {FColor(0, 0, 0), FColor(255, 255, 255),
      FColor(12, 128, 240), FColor(255, 0, 64)})
  {
    Attribute.Value = FString::Printf(TEXT("%u,%u,%u"), Expected.R, Expected.G, Expected.B);
    const FColor Actual = UActorBlueprintFunctionLibrary::ActorAttributeToColor(
        Attribute, FColor::Black);
    TestEqual(*Attribute.Value, Actual, Expected);
    // Grading uses the alpha channel as its master multiplier after this
    // conversion; an RGB-only attribute must always supply the neutral value.
    TestEqual(TEXT("Linear master grading multiplier"), FLinearColor(Actual).A, 1.0f);
  }
  return true;
}
#endif
